#include "cec_injector/mate/linux/linux_power_session.h"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <future>
#include <mutex>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>

namespace cec_injector::mate::linux_platform {
namespace {

using namespace std::chrono_literals;

constexpr std::string_view kDestination{"org.freedesktop.login1"};
constexpr std::string_view kPath{"/org/freedesktop/login1"};
constexpr std::string_view kInterface{"org.freedesktop.login1.Manager"};

class FdInhibitor final : public LinuxTransitionInhibitor {
 public:
  explicit FdInhibitor(int fd) : fd_(fd) {}
  ~FdInhibitor() override { release(); }
  void release() noexcept override {
    if (fd_ < 0) return;
    close(fd_);
    fd_ = -1;
  }

 private:
  int fd_;
};

class SdBusPowerSession final : public LinuxPowerSessionRuntime {
 public:
  SdBusPowerSession()
      : model_(
            [this](LinuxTransitionKind kind) { return acquire(kind); },
            [this] { return suspend(); },
            [this] { return authorization_checks(); }) {}

  ~SdBusPowerSession() override { stop(); }

  void set_notification_sink(
      LinuxPowerSessionModel::NotificationSink sink) override {
    model_.set_notification_sink(std::move(sink));
  }

  OperationResult start() override {
    std::lock_guard lock(command_mutex_);
    if (worker_.joinable()) return OperationResult::InvalidState;
    stopping_ = false;
    worker_ = std::thread(&SdBusPowerSession::run, this);
    return OperationResult::Success;
  }

  void stop() noexcept override {
    {
      std::lock_guard lock(command_mutex_);
      stopping_ = true;
    }
    command_changed_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  OperationResult request_system_suspend() override {
    if (!connected_) return OperationResult::Disconnected;
    auto command = std::make_shared<std::promise<OperationResult>>();
    auto result = command->get_future();
    {
      std::lock_guard lock(command_mutex_);
      if (stopping_) return OperationResult::Disconnected;
      commands_.push_back(command);
    }
    command_changed_.notify_one();
    if (result.wait_for(2500ms) != std::future_status::ready) {
      return OperationResult::Timeout;
    }
    return result.get();
  }

  AuthorizationState suspend_authorization() const noexcept override {
    return model_.suspend_authorization();
  }

  HostPowerState host_power_state() const noexcept override {
    return model_.host_power_state();
  }

 private:
  struct Signal {
    enum class Kind { Sleep, Shutdown, OwnerLost } kind;
    bool preparing;
  };

  static int on_sleep(sd_bus_message* message, void* userdata, sd_bus_error*) {
    int preparing = 0;
    if (sd_bus_message_read(message, "b", &preparing) >= 0) {
      static_cast<SdBusPowerSession*>(userdata)->signals_.push_back(
          {Signal::Kind::Sleep, preparing != 0});
    }
    return 0;
  }

  static int on_shutdown(sd_bus_message* message, void* userdata,
                         sd_bus_error*) {
    int preparing = 0;
    if (sd_bus_message_read(message, "b", &preparing) >= 0) {
      static_cast<SdBusPowerSession*>(userdata)->signals_.push_back(
          {Signal::Kind::Shutdown, preparing != 0});
    }
    return 0;
  }

  static int on_name_owner_changed(sd_bus_message* message, void* userdata,
                                   sd_bus_error*) {
    const char* name = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner) < 0 ||
        std::string_view(name ? name : "") != kDestination) {
      return 0;
    }
    (void)old_owner;
    if (!new_owner || *new_owner == '\0') {
      static_cast<SdBusPowerSession*>(userdata)->signals_.push_back(
          {Signal::Kind::OwnerLost, false});
    }
    return 0;
  }

  bool connect_bus() {
    close_bus();
    if (sd_bus_open_system(&bus_) < 0) return false;
    const int sleep = sd_bus_add_match(
        bus_, nullptr,
        "type='signal',sender='org.freedesktop.login1',"
        "path='/org/freedesktop/login1',"
        "interface='org.freedesktop.login1.Manager',member='PrepareForSleep'",
        &SdBusPowerSession::on_sleep, this);
    const int shutdown = sd_bus_add_match(
        bus_, nullptr,
        "type='signal',sender='org.freedesktop.login1',"
        "path='/org/freedesktop/login1',"
        "interface='org.freedesktop.login1.Manager',"
        "member='PrepareForShutdown'",
        &SdBusPowerSession::on_shutdown, this);
    const int owner = sd_bus_add_match(
        bus_, nullptr,
        "type='signal',sender='org.freedesktop.DBus',"
        "path='/org/freedesktop/DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='org.freedesktop.login1'",
        &SdBusPowerSession::on_name_owner_changed, this);
    if (sleep < 0 || shutdown < 0 || owner < 0) {
      close_bus();
      return false;
    }
    connected_ = true;
    model_.on_bus_connected();
    return true;
  }

  void close_bus() noexcept {
    if (connected_.exchange(false)) model_.on_bus_disconnected();
    sd_bus_flush_close_unref(bus_);
    bus_ = nullptr;
    signals_.clear();
  }

  std::unique_ptr<LinuxTransitionInhibitor> acquire(
      LinuxTransitionKind kind) {
    if (!bus_) return {};
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const char* what = kind == LinuxTransitionKind::Sleep ? "sleep" : "shutdown";
    const int result = sd_bus_call_method(
        bus_, kDestination.data(), kPath.data(), kInterface.data(), "Inhibit",
        &error, &reply, "ssss", what, "cec-injector-mate",
        "Complete HDMI-CEC host transition policy", "delay");
    int borrowed = -1;
    int owned = -1;
    if (result >= 0 && sd_bus_message_read(reply, "h", &borrowed) >= 0) {
      owned = fcntl(borrowed, F_DUPFD_CLOEXEC, 3);
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    if (owned < 0) return {};
    return std::make_unique<FdInhibitor>(owned);
  }

  LinuxLogindCallResult suspend() {
    if (!bus_) return LinuxLogindCallResult::Unavailable;
    sd_bus_message* request = nullptr;
    sd_bus_message* reply = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int result = sd_bus_message_new_method_call(
        bus_, &request, kDestination.data(), kPath.data(), kInterface.data(),
        "Suspend");
    if (result >= 0) result = sd_bus_message_append(request, "b", 0);
    if (result >= 0) result = sd_bus_call(bus_, request, 2000000, &error, &reply);
    LinuxLogindCallResult mapped = LinuxLogindCallResult::Accepted;
    if (result == -ETIMEDOUT) {
      mapped = LinuxLogindCallResult::Timeout;
    } else if (result == -ENOTCONN || result == -ECONNRESET || result == -EPIPE) {
      mapped = LinuxLogindCallResult::Unavailable;
    } else if (result < 0 &&
               (sd_bus_error_has_name(&error, SD_BUS_ERROR_ACCESS_DENIED) ||
                sd_bus_error_has_name(&error, "org.freedesktop.DBus.Error.AuthFailed"))) {
      mapped = LinuxLogindCallResult::Denied;
    } else if (result < 0) {
      mapped = LinuxLogindCallResult::Rejected;
    }
    sd_bus_message_unref(reply);
    sd_bus_message_unref(request);
    sd_bus_error_free(&error);
    return mapped;
  }

  std::optional<bool> check_authorization(const char* action) {
    const char* unique_name = nullptr;
    if (!bus_ || sd_bus_get_unique_name(bus_, &unique_name) < 0) return std::nullopt;
    sd_bus_message* request = nullptr;
    sd_bus_message* reply = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int result = sd_bus_message_new_method_call(
        bus_, &request, "org.freedesktop.PolicyKit1",
        "/org/freedesktop/PolicyKit1/Authority",
        "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization");
    if (result >= 0) result = sd_bus_message_open_container(request, 'r', "sa{sv}");
    if (result >= 0) result = sd_bus_message_append(request, "s", "system-bus-name");
    if (result >= 0) result = sd_bus_message_open_container(request, 'a', "{sv}");
    if (result >= 0) result = sd_bus_message_open_container(request, 'e', "sv");
    if (result >= 0) result = sd_bus_message_append(request, "s", "name");
    if (result >= 0) result = sd_bus_message_open_container(request, 'v', "s");
    if (result >= 0) result = sd_bus_message_append(request, "s", unique_name);
    for (int i = 0; i < 4 && result >= 0; ++i) {
      result = sd_bus_message_close_container(request);
    }
    if (result >= 0) result = sd_bus_message_append(request, "s", action);
    if (result >= 0) result = sd_bus_message_open_container(request, 'a', "{ss}");
    if (result >= 0) result = sd_bus_message_close_container(request);
    if (result >= 0) result = sd_bus_message_append(request, "us", 0U, "");
    if (result >= 0) result = sd_bus_call(bus_, request, 2000000, &error, &reply);
    int authorized = 0;
    int challenge = 0;
    if (result >= 0) result = sd_bus_message_enter_container(reply, 'r', "bba{ss}");
    if (result >= 0) result = sd_bus_message_read(reply, "bb", &authorized, &challenge);
    (void)challenge;
    sd_bus_message_unref(reply);
    sd_bus_message_unref(request);
    sd_bus_error_free(&error);
    if (result < 0) return std::nullopt;
    return authorized != 0;
  }

  LinuxSuspendAuthorizationChecks authorization_checks() {
    return {check_authorization("org.freedesktop.login1.suspend"),
            check_authorization(
                "org.freedesktop.login1.suspend-multiple-sessions")};
  }

  void dispatch_commands() {
    std::deque<std::shared_ptr<std::promise<OperationResult>>> commands;
    {
      std::lock_guard lock(command_mutex_);
      commands.swap(commands_);
    }
    for (const auto& command : commands) {
      command->set_value(model_.request_system_suspend());
    }
  }

  void run() {
    auto backoff = 1s;
    while (true) {
      {
        std::lock_guard lock(command_mutex_);
        if (stopping_) break;
      }
      if (!connect_bus()) {
        std::unique_lock lock(command_mutex_);
        command_changed_.wait_for(lock, backoff, [this] { return stopping_; });
        backoff = LinuxPowerSessionModel::next_reconnect_backoff(backoff);
        continue;
      }
      backoff = 1s;
      while (connected_) {
        {
          std::lock_guard lock(command_mutex_);
          if (stopping_) break;
        }
        dispatch_commands();
        model_.tick();
        const int processed = sd_bus_process(bus_, nullptr);
        if (processed < 0) break;
        for (const auto signal : std::exchange(signals_, {})) {
          if (signal.kind == Signal::Kind::Sleep) {
            model_.on_prepare_for_sleep(signal.preparing);
          } else if (signal.kind == Signal::Kind::Shutdown) {
            model_.on_prepare_for_shutdown(signal.preparing);
          } else {
            model_.on_bus_disconnected();
            connected_ = false;
          }
        }
        if (processed == 0 && sd_bus_wait(bus_, 100000) < 0) break;
      }
      close_bus();
    }
    close_bus();
    model_.shutdown();
    std::deque<std::shared_ptr<std::promise<OperationResult>>> abandoned;
    {
      std::lock_guard lock(command_mutex_);
      abandoned.swap(commands_);
    }
    for (const auto& command : abandoned) {
      command->set_value(OperationResult::Disconnected);
    }
  }

  LinuxPowerSessionModel model_;
  sd_bus* bus_{};
  std::vector<Signal> signals_;
  std::atomic<bool> connected_{};
  mutable std::mutex command_mutex_;
  std::condition_variable command_changed_;
  bool stopping_{};
  std::deque<std::shared_ptr<std::promise<OperationResult>>> commands_;
  std::thread worker_;
};

}  // namespace

std::unique_ptr<LinuxPowerSessionRuntime> create_linux_power_session_runtime() {
  return std::make_unique<SdBusPowerSession>();
}

}  // namespace cec_injector::mate::linux_platform

#include "cec_injector/mate/linux/linux_power_session.h"

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "test_support.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::linux_platform;

namespace {

struct Counters {
  int sleep_acquired{};
  int shutdown_acquired{};
  int sleep_released{};
  int shutdown_released{};
};

class FakeInhibitor final : public LinuxTransitionInhibitor {
 public:
  FakeInhibitor(int& released) : released_(released) {}
  ~FakeInhibitor() override { release(); }
  void release() noexcept override {
    if (done_) return;
    done_ = true;
    ++released_;
  }

 private:
  int& released_;
  bool done_{};
};

struct Harness {
  explicit Harness(
      LinuxLogindCallResult suspend_result = LinuxLogindCallResult::Accepted,
      LinuxSuspendAuthorizationChecks authorization = {true, true},
      std::chrono::milliseconds deadline =
          LinuxPowerSessionModel::kTransitionDeadline)
      : suspend_result(suspend_result), authorization(authorization),
        model(
            [this](LinuxTransitionKind kind) {
              if (kind == LinuxTransitionKind::Sleep) {
                ++counters.sleep_acquired;
                return std::unique_ptr<LinuxTransitionInhibitor>(
                    std::make_unique<FakeInhibitor>(counters.sleep_released));
              }
              ++counters.shutdown_acquired;
              return std::unique_ptr<LinuxTransitionInhibitor>(
                  std::make_unique<FakeInhibitor>(counters.shutdown_released));
            },
            [this] {
              ++suspend_calls;
              return this->suspend_result;
            },
            [this] { return this->authorization; }, deadline) {
    model.set_notification_sink([this](HostLifecycleNotification notification) {
      notifications.push_back(std::move(notification));
    });
  }

  LinuxLogindCallResult suspend_result;
  LinuxSuspendAuthorizationChecks authorization;
  Counters counters;
  int suspend_calls{};
  std::vector<HostLifecycleNotification> notifications;
  LinuxPowerSessionModel model;
};

}  // namespace

int main() {
  using Event = HostLifecycleEvent;
  const auto start = LinuxPowerSessionModel::Clock::time_point{};

  Harness lifecycle;
  lifecycle.model.on_bus_connected();
  CHECK(lifecycle.notifications.empty());
  CHECK(lifecycle.model.host_power_state() == HostPowerState::Running);

  HostPowerState state_during_resume = HostPowerState::Suspended;
  lifecycle.model.set_notification_sink(
      [&](HostLifecycleNotification notification) {
        if (notification.event == Event::Resumed) {
          state_during_resume = lifecycle.model.host_power_state();
        }
        lifecycle.notifications.push_back(std::move(notification));
      });
  CHECK(lifecycle.model.suspend_authorization() ==
        AuthorizationState::Authorized);
  CHECK(lifecycle.counters.sleep_acquired == 1);
  CHECK(lifecycle.counters.shutdown_acquired == 1);

  lifecycle.model.on_prepare_for_sleep(true, start);
  lifecycle.model.on_prepare_for_sleep(true, start);
  CHECK(lifecycle.notifications.size() == 1);
  CHECK(lifecycle.notifications[0].event == Event::PreparingSuspend);
  CHECK(lifecycle.notifications[0].transition_hold.has_value());
  CHECK(lifecycle.notifications[0].transition_hold->active());
  CHECK(lifecycle.model.host_power_state() == HostPowerState::PreparingSuspend);
  CHECK(lifecycle.notifications[0].transition_hold->complete());
  CHECK(!lifecycle.notifications[0].transition_hold->complete());
  CHECK(lifecycle.counters.sleep_released == 1);
  CHECK(lifecycle.model.host_power_state() == HostPowerState::Suspended);

  lifecycle.model.on_prepare_for_sleep(false, start);
  CHECK(lifecycle.notifications.size() == 2);
  CHECK(lifecycle.notifications[1].event == Event::Resumed);
  CHECK(state_during_resume == HostPowerState::Resuming);
  CHECK(!lifecycle.notifications[1].transition_hold);
  CHECK(lifecycle.counters.sleep_acquired == 2);
  CHECK(lifecycle.model.host_power_state() == HostPowerState::Running);

  lifecycle.model.on_prepare_for_shutdown(true, start);
  CHECK(lifecycle.notifications.size() == 3);
  CHECK(lifecycle.notifications[2].event == Event::PreparingShutdown);
  CHECK(lifecycle.notifications[2].transition_hold->active());
  lifecycle.notifications.erase(lifecycle.notifications.begin() + 2);
  CHECK(lifecycle.counters.shutdown_released == 1);
  lifecycle.model.on_prepare_for_shutdown(false, start);
  CHECK(lifecycle.notifications.back().event == Event::ShutdownCancelled);
  CHECK(lifecycle.counters.shutdown_acquired == 2);

  Harness deadline(LinuxLogindCallResult::Accepted, {true, true},
                   std::chrono::milliseconds(40));
  deadline.model.on_bus_connected();
  deadline.model.on_prepare_for_sleep(true, start);
  deadline.model.tick(start + std::chrono::milliseconds(39));
  CHECK(deadline.counters.sleep_released == 0);
  deadline.model.tick(start + std::chrono::milliseconds(40));
  CHECK(deadline.counters.sleep_released == 1);
  CHECK(!deadline.notifications[0].transition_hold->active());
  CHECK(!deadline.notifications[0].transition_hold->complete());
  CHECK(deadline.counters.sleep_released == 1);

  Harness disconnected;
  disconnected.model.on_bus_connected();
  disconnected.model.on_prepare_for_shutdown(true, start);
  disconnected.model.on_bus_disconnected();
  CHECK(disconnected.counters.sleep_released == 1);
  CHECK(disconnected.counters.shutdown_released == 1);
  CHECK(!disconnected.notifications[0].transition_hold->active());
  CHECK(!disconnected.notifications[0].transition_hold->complete());
  CHECK(!disconnected.model.connected());
  CHECK(disconnected.model.suspend_authorization() ==
        AuthorizationState::Unavailable);
  const auto event_count = disconnected.notifications.size();
  disconnected.model.on_bus_connected();
  CHECK(disconnected.notifications.size() == event_count);
  CHECK(disconnected.counters.sleep_acquired == 2);
  CHECK(disconnected.counters.shutdown_acquired == 2);
  disconnected.model.on_prepare_for_shutdown(false, start);
  CHECK(disconnected.notifications.back().event == Event::ShutdownCancelled);
  disconnected.model.on_prepare_for_sleep(true, start);
  CHECK(disconnected.notifications.back().event == Event::PreparingSuspend);
  disconnected.model.shutdown();
  CHECK(disconnected.counters.sleep_released == 2);
  CHECK(disconnected.counters.shutdown_released == 3);
  CHECK(!disconnected.notifications.back().transition_hold->active());
  CHECK(!disconnected.notifications.back().transition_hold->complete());

  for (const auto [native, expected] : {
           std::pair{LinuxLogindCallResult::Accepted, OperationResult::Success},
           std::pair{LinuxLogindCallResult::Denied,
                     OperationResult::PermissionDenied},
           std::pair{LinuxLogindCallResult::Rejected, OperationResult::Rejected},
           std::pair{LinuxLogindCallResult::Timeout, OperationResult::Timeout},
           std::pair{LinuxLogindCallResult::Unavailable,
                     OperationResult::Disconnected}}) {
    Harness request(native);
    request.model.on_bus_connected();
    CHECK(request.model.request_system_suspend() == expected);
    CHECK(request.suspend_calls == 1);
    CHECK(request.notifications.empty());
  }
  Harness no_bus;
  CHECK(no_bus.model.request_system_suspend() == OperationResult::Disconnected);
  CHECK(no_bus.suspend_calls == 0);

  Harness denied(LinuxLogindCallResult::Accepted, {true, false});
  denied.model.on_bus_connected();
  CHECK(denied.model.suspend_authorization() == AuthorizationState::Denied);
  Harness unavailable_a(LinuxLogindCallResult::Accepted, {std::nullopt, true});
  unavailable_a.model.on_bus_connected();
  CHECK(unavailable_a.model.suspend_authorization() ==
        AuthorizationState::Unavailable);
  Harness unavailable_b(LinuxLogindCallResult::Accepted, {true, std::nullopt});
  unavailable_b.model.on_bus_connected();
  CHECK(unavailable_b.model.suspend_authorization() ==
        AuthorizationState::Unavailable);

  CHECK(LinuxPowerSessionModel::next_reconnect_backoff(
            std::chrono::seconds(0)) == std::chrono::seconds(1));
  CHECK(LinuxPowerSessionModel::next_reconnect_backoff(
            std::chrono::seconds(1)) == std::chrono::seconds(2));
  CHECK(LinuxPowerSessionModel::next_reconnect_backoff(
            std::chrono::seconds(16)) == std::chrono::seconds(30));
  CHECK(LinuxPowerSessionModel::next_reconnect_backoff(
            std::chrono::seconds(30)) == std::chrono::seconds(30));

  return EXIT_SUCCESS;
}

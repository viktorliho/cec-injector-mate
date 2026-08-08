#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::linux_platform {

enum class LinuxTransitionKind { Sleep, Shutdown };

class LinuxTransitionInhibitor {
 public:
  virtual ~LinuxTransitionInhibitor() = default;
  virtual void release() noexcept = 0;
};

enum class LinuxLogindCallResult {
  Accepted,
  Denied,
  Rejected,
  Timeout,
  Unavailable,
};

struct LinuxSuspendAuthorizationChecks {
  std::optional<bool> suspend;
  std::optional<bool> suspend_multiple_sessions;
};

class LinuxPowerSessionModel {
 public:
  using Clock = std::chrono::steady_clock;
  using Acquire = std::function<std::unique_ptr<LinuxTransitionInhibitor>(
      LinuxTransitionKind)>;
  using Suspend = std::function<LinuxLogindCallResult()>;
  using Authorization = std::function<LinuxSuspendAuthorizationChecks()>;
  using NotificationSink = std::function<void(HostLifecycleNotification)>;

  static constexpr std::chrono::milliseconds kTransitionDeadline{3000};
  static constexpr std::chrono::seconds kMaximumReconnectBackoff{30};

  LinuxPowerSessionModel(Acquire acquire, Suspend suspend,
                         Authorization authorization,
                         std::chrono::milliseconds transition_deadline =
                             kTransitionDeadline);
  ~LinuxPowerSessionModel();

  LinuxPowerSessionModel(const LinuxPowerSessionModel&) = delete;
  LinuxPowerSessionModel& operator=(const LinuxPowerSessionModel&) = delete;

  void set_notification_sink(NotificationSink sink);
  void on_bus_connected();
  void on_bus_disconnected();
  void on_prepare_for_sleep(bool preparing,
                            Clock::time_point now = Clock::now());
  void on_prepare_for_shutdown(bool preparing,
                               Clock::time_point now = Clock::now());
  void tick(Clock::time_point now = Clock::now());
  void shutdown();

  [[nodiscard]] OperationResult request_system_suspend();
  [[nodiscard]] AuthorizationState suspend_authorization() const noexcept;
  [[nodiscard]] HostPowerState host_power_state() const noexcept;
  [[nodiscard]] bool connected() const noexcept;

  [[nodiscard]] static std::chrono::seconds next_reconnect_backoff(
      std::chrono::seconds current) noexcept;

 private:
  struct HoldState;
  void notify_prepare(LinuxTransitionKind kind, HostLifecycleEvent event,
                      Clock::time_point now);
  void notify(HostLifecycleEvent event);
  void acquire_inhibitor(LinuxTransitionKind kind);
  void release_all_holds() noexcept;

  struct Impl;
  std::shared_ptr<Impl> impl_;
};

class LinuxPowerSessionRuntime {
 public:
  virtual ~LinuxPowerSessionRuntime() = default;
  virtual void set_notification_sink(
      LinuxPowerSessionModel::NotificationSink sink) = 0;
  virtual OperationResult start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual OperationResult request_system_suspend() = 0;
  [[nodiscard]] virtual AuthorizationState suspend_authorization()
      const noexcept = 0;
  [[nodiscard]] virtual HostPowerState host_power_state() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<LinuxPowerSessionRuntime>
create_linux_power_session_runtime();

}  // namespace cec_injector::mate::linux_platform

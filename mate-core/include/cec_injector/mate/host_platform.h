#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cec_injector/mate/types.h"

namespace cec_injector::mate {

class PlatformDeviceLocator {
 public:
  PlatformDeviceLocator() = default;
  explicit PlatformDeviceLocator(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] bool valid() const noexcept { return !value_.empty(); }
  auto operator<=>(const PlatformDeviceLocator&) const = default;

 private:
  std::string value_;
};

struct DiscoveredDevice {
  DeviceId id;
  PlatformDeviceLocator locator;
  std::string display_name;

  bool operator==(const DiscoveredDevice&) const = default;
};

enum class PhysicalAddressStatus { Available, Unavailable, Ambiguous };

struct PhysicalAddressResolution {
  PhysicalAddressStatus status{PhysicalAddressStatus::Unavailable};
  std::optional<std::uint16_t> address;

  bool operator==(const PhysicalAddressResolution&) const = default;
};

enum class HostPowerState {
  Running,
  PreparingSuspend,
  Suspended,
  Resuming,
  PreparingShutdown,
};

enum class AuthorizationState { Authorized, Denied, Unavailable };

enum class HostLifecycleEvent {
  PreparingSuspend,
  Resumed,
  PreparingShutdown,
  ShutdownCancelled,
};

class HostTransitionHold {
 public:
  using Completion = std::function<void()>;

  HostTransitionHold() = default;
  explicit HostTransitionHold(Completion completion)
      : completion_(std::move(completion)), completed_(!completion_) {}
  // Destruction is safe abandonment: it releases an active hold once.
  ~HostTransitionHold() { (void)complete(); }

  HostTransitionHold(const HostTransitionHold&) = delete;
  HostTransitionHold& operator=(const HostTransitionHold&) = delete;

  HostTransitionHold(HostTransitionHold&& other) noexcept
      : completion_(std::move(other.completion_)),
        completed_(other.completed_) {
    other.completion_ = {};
    other.completed_ = true;
  }

  HostTransitionHold& operator=(HostTransitionHold&& other) noexcept {
    if (this == &other) return *this;
    (void)complete();
    completion_ = std::move(other.completion_);
    completed_ = other.completed_;
    other.completion_ = {};
    other.completed_ = true;
    return *this;
  }

  [[nodiscard]] bool active() const noexcept { return !completed_; }

  // Returns true only for the owner that performs the single completion.
  bool complete() noexcept {
    if (completed_) return false;
    completed_ = true;
    auto completion = std::move(completion_);
    completion_ = {};
    try {
      completion();
    } catch (...) {
      // Releasing a platform transition must remain safe during destruction.
    }
    return true;
  }

 private:
  Completion completion_;
  bool completed_{true};
};

struct HostLifecycleNotification {
  HostLifecycleEvent event;
  // PreparingSuspend and PreparingShutdown may carry a hold. Other events do
  // not require one; platform implementations may also prepare without one.
  std::optional<HostTransitionHold> transition_hold;

  HostLifecycleNotification(HostLifecycleEvent event_value,
                            std::optional<HostTransitionHold> hold = std::nullopt)
      : event(event_value), transition_hold(std::move(hold)) {}

  HostLifecycleNotification(const HostLifecycleNotification&) = delete;
  HostLifecycleNotification& operator=(const HostLifecycleNotification&) = delete;
  HostLifecycleNotification(HostLifecycleNotification&&) noexcept = default;
  HostLifecycleNotification& operator=(HostLifecycleNotification&&) noexcept = default;
};

class HostLifecycleObserver {
 public:
  virtual ~HostLifecycleObserver() = default;
  // HostPlatform delivers notifications in the order observed by the host.
  virtual void on_host_lifecycle_notification(
      HostLifecycleNotification notification) = 0;
};

class HostPlatform {
 public:
  virtual ~HostPlatform() = default;

  virtual void set_lifecycle_observer(HostLifecycleObserver* observer) = 0;
  [[nodiscard]] virtual std::vector<DiscoveredDevice> discovered_devices() = 0;
  [[nodiscard]] virtual PhysicalAddressResolution resolve_physical_address() = 0;
  virtual OperationResult request_system_suspend() = 0;
  [[nodiscard]] virtual HostPowerState host_power_state() const noexcept = 0;
  [[nodiscard]] virtual AuthorizationState suspend_authorization() const = 0;
  [[nodiscard]] virtual AuthorizationState remote_wake_authorization(
      const DeviceId& device) const = 0;
  virtual OperationResult emit_remote_action(SemanticRemoteAction action,
                                             RemoteActionState state) = 0;
  virtual OperationResult reset_remote_input() = 0;
};

}  // namespace cec_injector::mate

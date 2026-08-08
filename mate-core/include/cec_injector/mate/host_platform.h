#pragma once

#include <compare>
#include <cstdint>
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

class HostPlatform {
 public:
  virtual ~HostPlatform() = default;

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

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cec_injector::mate {

class DeviceId {
 public:
  DeviceId() = default;
  explicit DeviceId(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] bool valid() const noexcept { return !value_.empty(); }
  [[nodiscard]] std::string_view diagnostic_value() const noexcept { return value_; }

  auto operator<=>(const DeviceId&) const = default;

 private:
  std::string value_;
};

enum class SessionState {
  Discovered,
  Connecting,
  Connected,
  Degraded,
  Disconnecting,
  Disconnected,
};

enum class OperationResult {
  Success,
  Unsupported,
  InvalidState,
  InvalidConfiguration,
  Rejected,
  Timeout,
  Disconnected,
  MalformedResponse,
  PermissionDenied,
  InternalError,
};

enum class SemanticRemoteAction {
  Up,
  Down,
  Left,
  Right,
  Select,
  Back,
  Home,
  Menu,
  Info,
  Guide,
  Play,
  Pause,
  PlayPause,
  Stop,
  Next,
  Previous,
};

enum class RemoteActionState { Pressed, Released };

struct DeviceCapabilities {
  bool cec{};
  bool wake{};

  bool operator==(const DeviceCapabilities&) const = default;
};

struct AddressState {
  std::optional<std::uint16_t> physical;
  std::optional<std::uint8_t> logical;

  bool operator==(const AddressState&) const = default;
};

struct WakeState {
  bool supported{};
  bool configured{};
  bool armed{};
  bool reason_available{};

  bool operator==(const WakeState&) const = default;
};

struct DeviceStateSnapshot {
  DeviceCapabilities capabilities;
  AddressState addresses;
  WakeState wake;

  bool operator==(const DeviceStateSnapshot&) const = default;
};

struct WakeReason {
  OperationResult result{OperationResult::InvalidState};
  std::vector<std::uint8_t> cec_frame;

  bool operator==(const WakeReason&) const = default;
};

}  // namespace cec_injector::mate

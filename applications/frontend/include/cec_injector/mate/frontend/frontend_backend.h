#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cec_injector/mate/host_platform.h"
#include "cec_injector/mate/policy.h"

namespace cec_injector::mate::frontend {

struct FrontendCapabilities {
  bool configuration{};
  bool remote_input{};
  bool remote_wake{};
  bool tv_power_actions{};
  bool cec_monitor{};

  bool operator==(const FrontendCapabilities&) const = default;
};

struct FrontendConfiguration {
  PolicyConfiguration policy;
  bool remote_input_enabled{};
  std::string remote_profile;

  bool operator==(const FrontendConfiguration&) const = default;
};

struct FrontendStatus {
  bool injector_connected{};
  std::optional<DeviceId> selected_device;
  std::string device_display_name;
  std::string display_name;
  PhysicalAddressStatus physical_address_status{
      PhysicalAddressStatus::Unavailable};
  std::optional<std::uint16_t> physical_address;
  std::optional<std::uint8_t> logical_address;
  SessionState session_state{SessionState::Discovered};
  WakeState wake;
  HostPowerState host_power_state{HostPowerState::Running};
  bool edid_available{};
  std::size_t edid_byte_count{};

  bool operator==(const FrontendStatus&) const = default;
};

enum class FrontendAction { PowerOnTv, MakeActiveSource, StandbyTv };

struct FrontendActionResult {
  FrontendAction action{FrontendAction::PowerOnTv};
  OperationResult result{OperationResult::InvalidState};
  std::chrono::milliseconds duration{};
  std::string message;

  bool operator==(const FrontendActionResult&) const = default;
};

using FrontendActionRequestId = std::uint64_t;

enum class CecDirection { Received, Transmitted };

struct FrontendCecEvent {
  std::chrono::system_clock::time_point timestamp;
  CecDirection direction{CecDirection::Received};
  std::uint8_t source{};
  std::uint8_t destination{};
  std::uint8_t opcode{};
  std::vector<std::uint8_t> parameters;
  std::optional<std::string> semantic;

  bool operator==(const FrontendCecEvent&) const = default;
};

class MateFrontendObserver {
 public:
  virtual ~MateFrontendObserver() = default;
  // Callbacks may be delivered from a non-presentation thread. A backend must
  // not invoke this observer after set_observer(nullptr) returns.
  virtual void on_frontend_status_changed(const FrontendStatus& status) = 0;
  virtual void on_frontend_cec_event(const FrontendCecEvent& event) = 0;
  virtual void on_frontend_action_completed(
      FrontendActionRequestId request,
      const FrontendActionResult& result) = 0;
};

class MateFrontendBackend {
 public:
  virtual ~MateFrontendBackend() = default;

  virtual void set_observer(MateFrontendObserver* observer) = 0;
  [[nodiscard]] virtual FrontendStatus status() const = 0;
  [[nodiscard]] virtual FrontendCapabilities capabilities() const = 0;
  [[nodiscard]] virtual FrontendConfiguration configuration() const = 0;
  virtual OperationResult apply_configuration(
      const FrontendConfiguration& configuration) = 0;
  // Acceptance only means the request was assigned an id. Completion may be
  // delivered later by an in-process worker or daemon IPC implementation.
  [[nodiscard]] virtual FrontendActionRequestId request_action(
      FrontendAction action) = 0;
};

}  // namespace cec_injector::mate::frontend

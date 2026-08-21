#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "cec_injector/mate/device_registry.h"
#include "cec_injector/mate/device_session_factory.h"
#include "cec_injector/mate/host_platform.h"
#include "cec_injector/mate/policy.h"
#include "cec_injector/mate/runtime_configuration.h"

namespace cec_injector::mate {

struct MateServiceConfiguration {
  PolicyConfiguration policy;
  RemoteInputConfiguration remote;
  std::optional<DeviceId> selected_device;
  std::chrono::milliseconds resume_recovery_timeout{
      std::chrono::seconds(30)};
  bool maintain_session_wake{true};
};

struct MateServiceSnapshot {
  bool running{};
  std::size_t device_count{};
  std::optional<DeviceId> selected_device;
  std::string device_display_name;
  DisplaySummary display;
  PhysicalAddressStatus physical_address_status{PhysicalAddressStatus::Unavailable};
  std::optional<std::uint16_t> physical_address;
  SessionState session_state{SessionState::Disconnected};
  DeviceStateSnapshot device_state;
  HostPowerState host_power_state{HostPowerState::Running};
  std::uint64_t session_generation{};
  bool operator==(const MateServiceSnapshot&) const = default;
};

enum class MateManualAction { PowerOnTv, MakeActiveSource, StandbyTv };
using MateManualActionRequestId = std::uint64_t;

struct MateManualActionResult {
  MateManualAction action{MateManualAction::PowerOnTv};
  OperationResult result{OperationResult::InvalidState};
  std::chrono::milliseconds duration{};
  std::string message;
};

struct MateServiceCecEvent {
  std::chrono::system_clock::time_point timestamp;
  bool received{true};
  std::uint8_t source{};
  std::uint8_t destination{};
  std::uint8_t opcode{};
  std::vector<std::uint8_t> parameters;
  std::optional<std::string> semantic;
};

class MateServiceFrontendObserver {
 public:
  virtual ~MateServiceFrontendObserver() = default;
  virtual void on_service_snapshot(const MateServiceSnapshot&) = 0;
  virtual void on_service_cec_event(const MateServiceCecEvent&) = 0;
  virtual void on_manual_action_completed(MateManualActionRequestId,
                                          const MateManualActionResult&) = 0;
};

class MateService final : public HostLifecycleObserver {
 public:
  using DiagnosticSink = std::function<void(std::string_view)>;

  MateService(HostPlatform& host, DeviceSessionFactory& session_factory,
              MateServiceConfiguration configuration = {},
              DiagnosticSink diagnostics = {});
  ~MateService() override;

  MateService(const MateService&) = delete;
  MateService& operator=(const MateService&) = delete;

  OperationResult start();
  void shutdown() noexcept;
  void refresh_discovery();
  void trigger_boot();
  void wait_idle();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] bool has_active_session() const noexcept;
  [[nodiscard]] std::optional<DeviceId> active_device() const;
  [[nodiscard]] std::uint64_t session_generation() const noexcept;
  OperationResult apply_runtime_configuration(
      const MateRuntimeConfiguration& configuration);
  [[nodiscard]] MateRuntimeConfiguration runtime_configuration() const;
  void set_frontend_observer(MateServiceFrontendObserver* observer);
  [[nodiscard]] MateServiceSnapshot frontend_snapshot() const;
  [[nodiscard]] MateManualActionRequestId request_manual_action(
      MateManualAction action);

  void on_host_lifecycle_notification(
      HostLifecycleNotification notification) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cec_injector::mate

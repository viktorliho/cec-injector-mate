#include "cec_injector/mate/frontend/fake_frontend_backend.h"

#include <utility>

namespace cec_injector::mate::frontend {

FakeFrontendBackend::FakeFrontendBackend() {
  status_.injector_connected = true;
  status_.selected_device = DeviceId("synthetic-device-1");
  status_.device_display_name = "CEC Injector (synthetic)";
  status_.display_name = "Development display";
  status_.physical_address_status = PhysicalAddressStatus::Available;
  status_.physical_address = 0x1000;
  status_.logical_address = 4;
  status_.session_state = SessionState::Connected;
  status_.wake = {.supported = true, .configured = true, .armed = true};
  status_.edid_available = true;
  status_.edid_byte_count = 256;

  capabilities_ = {.configuration = true,
                   .remote_input = true,
                   .remote_wake = true,
                   .tv_power_actions = true,
                   .cec_monitor = true};

  configuration_.policy = {.power_on_boot = true,
                           .power_on_resume = true,
                           .standby_on_suspend = true,
                           .standby_on_shutdown = true,
                           .suspend_on_tv_standby = true,
                           .wake_host_on_tv_source_request = true};
  configuration_.remote_input_enabled = true;
  configuration_.remote_profile = "Desktop navigation (synthetic)";

  const auto now = std::chrono::system_clock::now();
  events_ = {
      {.timestamp = now,
       .direction = CecDirection::Received,
       .source = 0,
       .destination = 15,
       .opcode = 0x36,
       .parameters = {},
       .semantic = "TV standby (synthetic)"},
      {.timestamp = now,
       .direction = CecDirection::Transmitted,
       .source = 4,
       .destination = 0,
       .opcode = 0x04,
       .parameters = {},
       .semantic = "Image view on (synthetic)"},
  };

}

void FakeFrontendBackend::set_observer(MateFrontendObserver* observer) {
  std::lock_guard lock(mutex_);
  observer_ = observer;
  if (!observer_) return;
  observer_->on_frontend_status_changed(status_);
  for (const auto& event : events_) observer_->on_frontend_cec_event(event);
}

FrontendStatus FakeFrontendBackend::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

FrontendCapabilities FakeFrontendBackend::capabilities() const {
  std::lock_guard lock(mutex_);
  return capabilities_;
}

FrontendConfiguration FakeFrontendBackend::configuration() const {
  std::lock_guard lock(mutex_);
  return configuration_;
}

OperationResult FakeFrontendBackend::apply_configuration(
    const FrontendConfiguration& configuration) {
  std::lock_guard lock(mutex_);
  configuration_ = configuration;
  if (observer_) observer_->on_frontend_status_changed(status_);
  return OperationResult::Success;
}

FrontendActionRequestId FakeFrontendBackend::request_action(
    FrontendAction action) {
  std::lock_guard lock(mutex_);
  const auto request = next_request_++;
  if (observer_) {
    observer_->on_frontend_action_completed(
        request, {.action = action,
                  .result = OperationResult::Success,
                  .duration = std::chrono::milliseconds(12),
                  .message = "Synthetic action completed"});
  }
  return request;
}

}  // namespace cec_injector::mate::frontend

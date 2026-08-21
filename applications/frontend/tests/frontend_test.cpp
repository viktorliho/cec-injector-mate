#include <cstdlib>
#include <iostream>
#include <string_view>

#include "cec_injector/mate/frontend/fake_frontend_backend.h"
#include "cec_injector/mate/frontend/frontend_state_projector.h"

namespace {

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}

struct Observer final : cec_injector::mate::frontend::MateFrontendObserver {
  int statuses{};
  int events{};
  int actions{};
  cec_injector::mate::frontend::FrontendActionRequestId last_request{};

  void on_frontend_status_changed(
      const cec_injector::mate::frontend::FrontendStatus&) override {
    ++statuses;
  }
  void on_frontend_cec_event(
      const cec_injector::mate::frontend::FrontendCecEvent&) override {
    ++events;
  }
  void on_frontend_action_completed(
      cec_injector::mate::frontend::FrontendActionRequestId request,
      const cec_injector::mate::frontend::FrontendActionResult&) override {
    ++actions;
    last_request = request;
  }
};

}  // namespace

int main() {
  using namespace cec_injector::mate;
  using namespace cec_injector::mate::frontend;
  FakeFrontendBackend backend;
  Observer observer;
  backend.set_observer(&observer);
  Check(observer.statuses == 1 && observer.events == 2,
        "observer receives current synthetic feed");
  const auto status = backend.status();
  Check(status.injector_connected, "representative connection state");
  Check(status.physical_address == 0x1000, "representative PA");
  Check(status.logical_address == 4, "Playback 1 logical address");
  Check(status.session_state == SessionState::Connected, "session state");
  Check(status.wake.supported && status.wake.armed, "Wake state");

  const auto capabilities = backend.capabilities();
  Check(capabilities.configuration && capabilities.cec_monitor,
        "capability state");

  auto snapshot = backend.configuration();
  Check(snapshot.configuration.policy.power_on_boot, "typed policy configuration");
  snapshot.configuration.policy.power_on_boot = false;
  Check(backend.request_configuration(snapshot.revision, snapshot.configuration) != 0,
        "fake configuration edit");
  Check(!backend.configuration().configuration.policy.power_on_boot,
        "configuration retained");

  const auto request = backend.request_action(FrontendAction::PowerOnTv);
  Check(request != 0 && observer.actions == 1 &&
            observer.last_request == request,
        "typed asynchronous fake action result");
  backend.set_observer(nullptr);
  const auto detached_statuses = observer.statuses;
  const auto detached_actions = observer.actions;
  snapshot = backend.configuration();
  (void)backend.request_configuration(snapshot.revision, snapshot.configuration);
  (void)backend.request_action(FrontendAction::StandbyTv);
  Check(observer.statuses == detached_statuses &&
            observer.actions == detached_actions,
        "no callbacks after observer detach");

  MateServiceSnapshot service_snapshot;
  service_snapshot.running = true;
  auto projected = FrontendStateProjector::status(service_snapshot);
  Check(projected.service_available && !projected.selected_device &&
            projected.physical_address_status == PhysicalAddressStatus::Unavailable,
        "coherent no-device projection");
  service_snapshot.device_count = 1;
  service_snapshot.selected_device = DeviceId("projected-device");
  service_snapshot.device_display_name = "Projected Device";
  service_snapshot.physical_address_status = PhysicalAddressStatus::Available;
  service_snapshot.physical_address = 0x2000;
  service_snapshot.session_state = SessionState::Connecting;
  projected = FrontendStateProjector::status(service_snapshot);
  Check(projected.service_available && !projected.injector_connected &&
            projected.session_state == SessionState::Connecting,
        "coherent connecting projection");
  service_snapshot.session_state = SessionState::Connected;
  service_snapshot.device_state.addresses.logical = 4;
  service_snapshot.device_state.wake = {true, true, true, false};
  projected = FrontendStateProjector::status(service_snapshot);
  Check(projected.injector_connected && projected.logical_address == 4 &&
            projected.wake.armed,
        "coherent connected projection");
  service_snapshot.session_state = SessionState::Disconnected;
  service_snapshot.device_state = {};
  projected = FrontendStateProjector::status(service_snapshot);
  Check(!projected.injector_connected && !projected.logical_address,
        "coherent lost projection");
  service_snapshot.physical_address_status = PhysicalAddressStatus::Ambiguous;
  service_snapshot.physical_address.reset();
  projected = FrontendStateProjector::status(service_snapshot);
  Check(projected.physical_address_status == PhysicalAddressStatus::Ambiguous &&
            !projected.physical_address,
        "ambiguous PA remains fail-closed");
  const MateServiceCecEvent raw_event{
      std::chrono::system_clock::now(), true, 0, 15, 0x36, {}, "standby"};
  const auto projected_event = FrontendStateProjector::cec_event(raw_event);
  Check(projected_event.direction == CecDirection::Received &&
            projected_event.opcode == 0x36 &&
            projected_event.semantic == "standby",
        "structured CEC event projection");
}

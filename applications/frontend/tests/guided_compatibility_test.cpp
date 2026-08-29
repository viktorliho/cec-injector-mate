#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "cec_injector/mate/frontend/compatibility_capture.h"
#include "cec_injector/mate/frontend/guided_compatibility_test.h"

namespace {

using namespace cec_injector::mate;
using namespace cec_injector::mate::frontend;

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}

FrontendStatus ConnectedStatus() {
  FrontendStatus status;
  status.service_available = true;
  status.injector_connected = true;
  status.selected_device = DeviceId("guided-device");
  status.display_name = "Guided TV";
  status.device_display_name = "CEC Injector";
  status.physical_address_status = PhysicalAddressStatus::Available;
  status.physical_address = 0x1000;
  status.logical_address = 4;
  status.session_state = SessionState::Connected;
  status.wake = {.supported = true, .configured = true, .armed = false};
  status.edid_available = true;
  status.edid_byte_count = 256;
  return status;
}

CompatibilityCaptureApplicationInfo AppInfo() {
  return {.application_name = "CEC Injector Mate",
          .platform = "macOS",
          .version = "1.0.0",
          .git_revision = "guided"};
}

FrontendCapabilities Capabilities() {
  return {.configuration = true,
          .remote_input = false,
          .remote_wake = false,
          .tv_power_actions = true,
          .cec_monitor = true};
}

FrontendConfigurationSnapshot Configuration() {
  FrontendConfigurationSnapshot snapshot;
  snapshot.revision = 3;
  snapshot.configuration = production_runtime_defaults();
  return snapshot;
}

}  // namespace

int main() {
  const auto start = std::chrono::system_clock::time_point{
      std::chrono::milliseconds(1724893200000LL)};

  {
    GuidedCompatibilityTestSession session;
    Check(session.start(CompatibilityGuidedTestScenario::ActiveSource,
                        ConnectedStatus(), false, start),
          "active source test starts");
    auto view = session.view();
    Check(view.primary && view.primary->label == "Continue",
          "active source starts with explicit continue");
    Check(!session.choose_primary(start + std::chrono::milliseconds(1))
                .action_to_send,
          "no command on initial continue");
    view = session.view();
    Check(view.primary &&
              view.primary->label == "Send Make Active Source",
          "explicit send required");
    const auto send =
        session.choose_primary(start + std::chrono::milliseconds(2));
    Check(send.action_to_send &&
              *send.action_to_send == FrontendAction::MakeActiveSource,
          "active source sends exactly one action");
    session.on_action_request_accepted(77, start + std::chrono::milliseconds(2));
    session.on_action_completed(
        77,
        {.action = FrontendAction::MakeActiveSource,
         .result = OperationResult::Timeout,
         .duration = std::chrono::milliseconds(2500),
         .message = "Timed out"},
        start + std::chrono::milliseconds(2502));
    view = session.view();
    Check(view.show_physical_choices,
          "physical confirmation stays separate from backend result");
    session.on_physical_confirmation(
        CompatibilityPhysicalResult::Worked, "TV switched",
        start + std::chrono::milliseconds(2600));
    Check(session.view().state == GuidedCompatibilityTestState::Completed,
          "timeout plus worked still completes");
    const auto* record = session.record();
    Check(record && record->related_action_request_ids.size() == 1 &&
              record->related_action_request_ids.front() == 77,
          "action request id correlated");
    Check(record->outcome == CompatibilityGuidedTestOutcome::Completed,
          "active source outcome completed");
  }

  {
    GuidedCompatibilityTestSession session;
    Check(session.start(CompatibilityGuidedTestScenario::StandbyPowerOn,
                        ConnectedStatus(), true, start),
          "standby power test starts");
    session.choose_primary(start + std::chrono::milliseconds(1));
    auto send = session.choose_primary(start + std::chrono::milliseconds(2));
    Check(send.action_to_send &&
              *send.action_to_send == FrontendAction::StandbyTv,
          "standby action explicit");
    session.on_action_request_accepted(91, start + std::chrono::milliseconds(2));
    session.on_action_completed(
        91,
        {.action = FrontendAction::StandbyTv,
         .result = OperationResult::Success,
         .duration = std::chrono::milliseconds(100),
         .message = "ok"},
        start + std::chrono::milliseconds(120));
    session.on_physical_confirmation(
        CompatibilityPhysicalResult::Failed, "TV stayed on",
        start + std::chrono::milliseconds(130));
    auto view = session.view();
    Check(view.secondary && view.secondary->label == "Continue Anyway",
          "failed standby does not auto-send power on");
    auto no_send = session.choose_secondary(start + std::chrono::milliseconds(140));
    Check(!no_send.action_to_send, "continue anyway itself sends nothing");
    view = session.view();
    Check(view.primary && view.primary->label == "Send Power On",
          "power on still explicit");
    send = session.choose_primary(start + std::chrono::milliseconds(150));
    Check(send.action_to_send &&
              *send.action_to_send == FrontendAction::PowerOnTv,
          "power on explicit");
    session.on_action_request_accepted(92, start + std::chrono::milliseconds(150));
    session.on_action_completed(
        92,
        {.action = FrontendAction::PowerOnTv,
         .result = OperationResult::Success,
         .duration = std::chrono::milliseconds(90),
         .message = "ok"},
        start + std::chrono::milliseconds(240));
    session.on_physical_confirmation(
        CompatibilityPhysicalResult::Worked, "TV woke",
        start + std::chrono::milliseconds(250));
    Check(session.view().state == GuidedCompatibilityTestState::Completed,
          "standby power scenario completes");
  }

  {
    GuidedCompatibilityTestSession session;
    Check(session.start(CompatibilityGuidedTestScenario::UsbReconnect,
                        ConnectedStatus(), true, start),
          "usb reconnect starts");
    session.choose_primary(start + std::chrono::milliseconds(1));
    auto lost = ConnectedStatus();
    lost.injector_connected = false;
    lost.session_state = SessionState::Disconnected;
    session.on_status(lost, start + std::chrono::milliseconds(200));
    auto view = session.view();
    Check(view.state == GuidedCompatibilityTestState::WaitingForObservation,
          "usb reconnect waits for return after loss");
    auto returned = ConnectedStatus();
    returned.selected_device = DeviceId("different-device");
    session.on_status(returned, start + std::chrono::milliseconds(1000));
    Check(session.view().state == GuidedCompatibilityTestState::Completed,
          "usb reconnect completes with collected evidence");
    Check(session.record() &&
              session.record()->reason == "USB reconnect evidence collected",
          "usb reconnect result recorded");
  }

  {
    GuidedCompatibilityTestSession session;
    Check(session.start(CompatibilityGuidedTestScenario::HdmiReconnect,
                        ConnectedStatus(), true, start),
          "hdmi reconnect starts");
    session.choose_tertiary(start + std::chrono::milliseconds(1));
    Check(session.view().state == GuidedCompatibilityTestState::Inconclusive,
          "unsupported setup is inconclusive");
  }

  {
    GuidedCompatibilityTestSession session;
    Check(session.start(CompatibilityGuidedTestScenario::UsbReconnect,
                        ConnectedStatus(), true, start),
          "timeout scenario starts");
    session.choose_primary(start + std::chrono::milliseconds(1));
    session.tick(start + GuidedCompatibilityTestSession::kObservationTimeout +
                 std::chrono::seconds(1));
    auto view = session.view();
    Check(view.primary && view.primary->label == "Retry Observation",
          "timeout offers retry");
    Check(view.secondary &&
              view.secondary->label == "Continue as Inconclusive",
          "timeout offers inconclusive path");
    session.choose_secondary(start + GuidedCompatibilityTestSession::kObservationTimeout +
                             std::chrono::seconds(2));
    Check(session.view().state == GuidedCompatibilityTestState::Inconclusive,
          "timeout can end inconclusive");
  }

  {
    CompatibilityCapture capture;
    capture.start(AppInfo(), ConnectedStatus(), Capabilities(), Configuration(),
                  start);
    GuidedCompatibilityTestSession session;
    session.start(CompatibilityGuidedTestScenario::ActiveSource,
                  ConnectedStatus(), true, start);
    session.choose_primary(start + std::chrono::milliseconds(1));
    session.choose_primary(start + std::chrono::milliseconds(2));
    session.on_action_request_accepted(55, start + std::chrono::milliseconds(2));
    session.on_action_completed(
        55,
        {.action = FrontendAction::MakeActiveSource,
         .result = OperationResult::Success,
         .duration = std::chrono::milliseconds(120),
         .message = "ok"},
        start + std::chrono::milliseconds(122));
    session.on_physical_confirmation(
        CompatibilityPhysicalResult::Worked, "Switched",
        start + std::chrono::milliseconds(140));
    capture.upsert_guided_test(*session.record());
    capture.stop(start + std::chrono::milliseconds(150));
    const auto export_data = capture.build_export("Guided Fixture");
    Check(export_data.markdown.find("## Guided Tests") != std::string::npos,
          "markdown guided section present");
    Check(export_data.markdown.find("Active Source") != std::string::npos,
          "guided scenario rendered");
    Check(export_data.json.find("\"guided_tests\": [") != std::string::npos,
          "json guided tests present");
    Check(export_data.json.find("\"request_id\": 55") != std::string::npos,
          "json request id correlated");
  }
}

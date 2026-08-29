#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#include "cec_injector/mate/frontend/compatibility_capture.h"

namespace {

using namespace cec_injector::mate;
using namespace cec_injector::mate::frontend;

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}

FrontendStatus BaseStatus() {
  FrontendStatus status;
  status.service_available = true;
  status.injector_connected = true;
  status.selected_device = DeviceId("compat-device");
  status.device_display_name = "CEC Injector";
  status.display_name = "Guest TV";
  status.physical_address_status = PhysicalAddressStatus::Available;
  status.physical_address = 0x1000;
  status.logical_address = 4;
  status.session_state = SessionState::Connected;
  status.wake = {.supported = true, .configured = true, .armed = false};
  status.edid_available = true;
  status.edid_byte_count = 256;
  return status;
}

FrontendCapabilities BaseCapabilities() {
  return {.configuration = true,
          .remote_input = false,
          .remote_wake = false,
          .tv_power_actions = true,
          .cec_monitor = true};
}

FrontendConfigurationSnapshot BaseConfiguration() {
  FrontendConfigurationSnapshot snapshot;
  snapshot.revision = 7;
  snapshot.configuration = production_runtime_defaults();
  snapshot.configuration.policy.power_on_boot = true;
  snapshot.configuration.remote.enabled = false;
  return snapshot;
}

CompatibilityCaptureApplicationInfo BaseApplication() {
  return {.application_name = "CEC Injector Mate",
          .platform = "macOS",
          .version = "0.0.99",
          .git_revision = "deadbeef"};
}

std::string ReadFixture(std::string_view name) {
  const auto path = std::filesystem::path(MATE_FRONTEND_TESTS_SOURCE_DIR) /
                    "fixtures" / std::string(name);
  std::ifstream stream(path);
  Check(stream.good(), "fixture opens");
  std::ostringstream content;
  content << stream.rdbuf();
  return content.str();
}

}  // namespace

int main() {
  const auto start = std::chrono::system_clock::time_point{
      std::chrono::milliseconds(1724889600000LL)};
  const auto rx = start + std::chrono::milliseconds(100);
  const auto request_time = start + std::chrono::milliseconds(200);
  const auto completion_time = start + std::chrono::milliseconds(350);
  const auto note_time = start + std::chrono::milliseconds(400);
  const auto marker_time = start + std::chrono::milliseconds(450);
  const auto stop_time = start + std::chrono::milliseconds(600);

  CompatibilityCapture capture;
  Check(capture.state() == CompatibilityCaptureState::Idle, "starts idle");
  Check(capture.start(BaseApplication(), BaseStatus(), BaseCapabilities(),
                      BaseConfiguration(), start),
        "start succeeds");
  Check(!capture.start(BaseApplication(), BaseStatus(), BaseCapabilities(),
                       BaseConfiguration(), start),
        "double start rejected while capturing");

  auto changed = BaseStatus();
  changed.session_state = SessionState::Disconnected;
  capture.record_status_transition(BaseStatus(), start + std::chrono::milliseconds(10));
  Check(capture.counts().status_transition_count == 0,
        "duplicate status ignored");
  capture.record_status_transition(changed,
                                   start + std::chrono::milliseconds(20));
  Check(capture.counts().status_transition_count == 1,
        "status transition recorded");

  capture.record_cec_event(
      {.timestamp = rx,
       .direction = CecDirection::Received,
       .source = 0,
       .destination = 15,
       .opcode = 0x36,
       .parameters = {},
       .semantic = "TV standby"});
  capture.record_cec_event(
      {.timestamp = start + std::chrono::milliseconds(120),
       .direction = CecDirection::Transmitted,
       .source = 4,
       .destination = 0,
       .opcode = 0x82,
       .parameters = {0x10, 0x00},
       .semantic = "Active Source"});
  Check(capture.counts().retained_cec_events == 2 &&
            capture.counts().total_cec_events == 2,
        "CEC events recorded");

  capture.record_action_requested(41, FrontendAction::MakeActiveSource,
                                  request_time);
  capture.record_action_completed(
      41,
      {.action = FrontendAction::MakeActiveSource,
       .result = OperationResult::Timeout,
       .duration = std::chrono::milliseconds(135),
       .message = "TV switched anyway"},
      completion_time);
  const auto pending = capture.latest_action_needing_physical_result();
  Check(pending && pending->request_id == 41, "action completion tracked");
  capture.record_action_physical_result(
      41, CompatibilityPhysicalResult::Worked, "TV switched to Mac HDMI",
      completion_time + std::chrono::milliseconds(25));
  const auto physical_done = capture.latest_action_needing_physical_result();
  Check(!physical_done, "physical result clears pending confirmation");

  capture.record_note("TV switched physically", note_time);
  capture.record_marker("Active Source test", marker_time);
  Check(capture.counts().note_count == 1 && capture.counts().marker_count == 1,
        "notes and markers recorded");

  Check(capture.stop(stop_time), "stop succeeds");
  Check(!capture.stop(stop_time), "double stop rejected");

  const auto export_data = capture.build_export("Philips Living Room");
  Check(export_data.suggested_basename ==
            "cec-compatibility-20240829-000000-philips-living-room",
        "default export filename is stable");
  Check(export_data.markdown == ReadFixture("compatibility_capture_sample.md"),
        "markdown fixture matches");
  Check(export_data.json == ReadFixture("compatibility_capture_sample.json"),
        "json fixture matches");

  const auto preview = capture.recent_timeline_lines(8);
  Check(preview.size() == 8, "recent timeline keeps expected rows");
  Check(preview.front().find("Status: Session: Connected -> Disconnected") !=
            std::string::npos,
        "timeline status readable");
  Check(preview.back().find("Marker: Active Source test") != std::string::npos,
        "timeline marker readable");

  capture.clear();
  Check(capture.state() == CompatibilityCaptureState::Idle &&
            capture.counts().retained_cec_events == 0 &&
            capture.counts().action_count == 0,
        "clear resets capture");

  CompatibilityCapture bounded_capture;
  Check(bounded_capture.start(BaseApplication(), BaseStatus(), BaseCapabilities(),
                              BaseConfiguration(), start),
        "bounded capture starts");
  for (std::size_t index = 0; index < CompatibilityCapture::kMaximumCecEvents + 5;
       ++index) {
    bounded_capture.record_cec_event(
        {.timestamp = start + std::chrono::milliseconds(1000 + index),
         .direction = CecDirection::Received,
         .source = 4,
         .destination = 0,
         .opcode = 0x82,
         .parameters = {0x10, 0x00},
         .semantic = "Active Source"});
  }
  Check(bounded_capture.counts().retained_cec_events ==
            CompatibilityCapture::kMaximumCecEvents,
        "CEC retention bounded");
  Check(bounded_capture.counts().dropped_cec_events == 5,
        "oldest CEC events evicted with drop count");
  Check(bounded_capture.recent_timeline_lines(4).back().find("RX 4:0 82") !=
            std::string::npos,
        "bounded timeline remains readable");
}

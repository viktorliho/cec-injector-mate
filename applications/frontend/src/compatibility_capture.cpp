#include "cec_injector/mate/frontend/compatibility_capture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace cec_injector::mate::frontend {
namespace {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

std::string SessionName(SessionState state) {
  switch (state) {
    case SessionState::Discovered: return "Discovered";
    case SessionState::Connecting: return "Connecting";
    case SessionState::Connected: return "Connected";
    case SessionState::Degraded: return "Degraded";
    case SessionState::Disconnecting: return "Disconnecting";
    case SessionState::Disconnected: return "Disconnected";
  }
  return "Unknown";
}

std::string OperationResultName(OperationResult result) {
  switch (result) {
    case OperationResult::Success: return "Success";
    case OperationResult::Unsupported: return "Unsupported";
    case OperationResult::InvalidState: return "InvalidState";
    case OperationResult::InvalidConfiguration:
      return "InvalidConfiguration";
    case OperationResult::Rejected: return "Rejected";
    case OperationResult::Timeout: return "Timeout";
    case OperationResult::Disconnected: return "Disconnected";
    case OperationResult::MalformedResponse: return "MalformedResponse";
    case OperationResult::PermissionDenied: return "PermissionDenied";
    case OperationResult::InternalError: return "InternalError";
  }
  return "Unknown";
}

std::string ActionName(FrontendAction action) {
  switch (action) {
    case FrontendAction::PowerOnTv: return "PowerOnTv";
    case FrontendAction::MakeActiveSource: return "MakeActiveSource";
    case FrontendAction::StandbyTv: return "StandbyTv";
  }
  return "Unknown";
}

std::string DirectionName(CecDirection direction) {
  return direction == CecDirection::Received ? "RX" : "TX";
}

std::string PhysicalAddressStatusName(PhysicalAddressStatus status) {
  switch (status) {
    case PhysicalAddressStatus::Available: return "Available";
    case PhysicalAddressStatus::Unavailable: return "Unavailable";
    case PhysicalAddressStatus::Ambiguous: return "Ambiguous";
  }
  return "Unknown";
}

std::string RemoteProfileName(RemoteInputProfile profile) {
  switch (profile) {
    case RemoteInputProfile::SteamOsParity: return "SteamOsParity";
  }
  return "Unknown";
}

std::string HostPowerStateName(HostPowerState state) {
  switch (state) {
    case HostPowerState::Running: return "Running";
    case HostPowerState::PreparingSuspend: return "PreparingSuspend";
    case HostPowerState::Suspended: return "Suspended";
    case HostPowerState::Resuming: return "Resuming";
    case HostPowerState::PreparingShutdown: return "PreparingShutdown";
  }
  return "Unknown";
}

std::string BoolName(bool value) { return value ? "yes" : "no"; }

std::string DeviceName(const std::optional<DeviceId>& device) {
  return device ? std::string(device->diagnostic_value()) : "none";
}

std::string PhysicalAddressName(const std::optional<std::uint16_t>& address) {
  if (!address) return "Unavailable";
  std::ostringstream out;
  out << ((*address >> 12) & 0xf) << '.' << ((*address >> 8) & 0xf) << '.'
      << ((*address >> 4) & 0xf) << '.' << (*address & 0xf);
  return out.str();
}

std::string LogicalAddressName(const std::optional<std::uint8_t>& address) {
  if (!address) return "Unavailable";
  std::ostringstream out;
  out << "Playback 1 (" << static_cast<unsigned>(*address) << ')';
  return out.str();
}

std::string TimestampString(TimePoint timestamp) {
  const auto whole = Clock::to_time_t(timestamp);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &whole);
#else
  gmtime_r(&whole, &tm);
#endif
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          timestamp.time_since_epoch()) %
                      1000;
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
      << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
  return out.str();
}

std::string FilenameTimestamp(TimePoint timestamp) {
  const auto whole = Clock::to_time_t(timestamp);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &whole);
#else
  gmtime_r(&whole, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return out.str();
}

std::string RelativeTimestamp(TimePoint start, TimePoint current) {
  const auto delta = current - start;
  const auto total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
  const auto minutes = total_ms / 60000;
  const auto seconds = (total_ms / 1000) % 60;
  const auto millis = total_ms % 1000;
  std::ostringstream out;
  out << std::setw(2) << std::setfill('0') << minutes << ':'
      << std::setw(2) << std::setfill('0') << seconds << '.'
      << std::setw(3) << std::setfill('0') << millis;
  return out.str();
}

std::string HexByte(std::uint8_t value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
      << static_cast<unsigned>(value);
  return out.str();
}

std::string HexPayload(const std::vector<std::uint8_t>& values) {
  if (values.empty()) return "-";
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) out << ' ';
    out << HexByte(values[i]);
  }
  return out.str();
}

std::string JsonString(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  out << '"';
  return out.str();
}

std::string MarkdownText(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char ch : value) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      result.push_back(' ');
    } else {
      result.push_back(static_cast<char>(ch));
    }
  }
  return result;
}

void AddStatusDifference(std::vector<std::string>& lines,
                         std::string label, std::string before,
                         std::string after) {
  if (before == after) return;
  lines.push_back(std::move(label) + ": " + std::move(before) + " -> " +
                  std::move(after));
}

std::string DescribeStatusChange(const FrontendStatus& previous,
                                 const FrontendStatus& current) {
  std::vector<std::string> differences;
  AddStatusDifference(differences, "Service",
                      previous.service_available ? "available" : "unavailable",
                      current.service_available ? "available" : "unavailable");
  AddStatusDifference(differences, "Injector",
                      previous.injector_connected ? "connected"
                                                  : "not connected",
                      current.injector_connected ? "connected"
                                                 : "not connected");
  AddStatusDifference(differences, "Device", DeviceName(previous.selected_device),
                      DeviceName(current.selected_device));
  AddStatusDifference(differences, "Display", previous.display_name,
                      current.display_name);
  AddStatusDifference(differences, "PA status",
                      PhysicalAddressStatusName(previous.physical_address_status),
                      PhysicalAddressStatusName(current.physical_address_status));
  AddStatusDifference(differences, "PA",
                      PhysicalAddressName(previous.physical_address),
                      PhysicalAddressName(current.physical_address));
  AddStatusDifference(differences, "Session", SessionName(previous.session_state),
                      SessionName(current.session_state));
  AddStatusDifference(differences, "Logical address",
                      LogicalAddressName(previous.logical_address),
                      LogicalAddressName(current.logical_address));
  AddStatusDifference(differences, "Wake support",
                      BoolName(previous.wake.supported),
                      BoolName(current.wake.supported));
  AddStatusDifference(differences, "Wake configured",
                      BoolName(previous.wake.configured),
                      BoolName(current.wake.configured));
  AddStatusDifference(differences, "Wake armed", BoolName(previous.wake.armed),
                      BoolName(current.wake.armed));
  AddStatusDifference(differences, "Host power",
                      HostPowerStateName(previous.host_power_state),
                      HostPowerStateName(current.host_power_state));
  AddStatusDifference(differences, "EDID",
                      previous.edid_available
                          ? "available (" + std::to_string(previous.edid_byte_count) +
                                " bytes)"
                          : "unavailable",
                      current.edid_available
                          ? "available (" + std::to_string(current.edid_byte_count) +
                                " bytes)"
                          : "unavailable");
  if (differences.empty()) return "No relevant status change";
  std::ostringstream out;
  for (std::size_t i = 0; i < differences.size(); ++i) {
    if (i != 0) out << "; ";
    out << differences[i];
  }
  return out.str();
}

std::string EventSummary(const FrontendCecEvent& event) {
  std::ostringstream out;
  out << DirectionName(event.direction) << ' ' << std::uppercase << std::hex
      << static_cast<unsigned>(event.source) << ':'
      << static_cast<unsigned>(event.destination) << ' ' << HexByte(event.opcode)
      << std::dec;
  if (!event.parameters.empty()) out << ' ' << HexPayload(event.parameters);
  if (event.semantic && !event.semantic->empty()) out << ' ' << *event.semantic;
  return out.str();
}

std::string ActionSummary(const CompatibilityCaptureActionRecord& action) {
  std::ostringstream out;
  out << "Action #" << action.request_id << ' ' << ActionName(action.action);
  if (action.completion) {
    out << " -> " << OperationResultName(action.completion->result) << " ("
        << action.completion->duration.count() << " ms)";
    if (!action.completion->message.empty()) {
      out << " " << MarkdownText(action.completion->message);
    }
  } else {
    out << " requested";
  }
  return out.str();
}

std::string PhysicalSummary(const CompatibilityCaptureActionRecord& action) {
  std::ostringstream out;
  out << "Physical result for #" << action.request_id << ": "
      << compatibility_physical_result_name(action.physical_result);
  if (!action.physical_note.empty()) {
    out << " — " << MarkdownText(action.physical_note);
  }
  return out.str();
}

}  // namespace

std::string compatibility_capture_state_name(CompatibilityCaptureState state) {
  switch (state) {
    case CompatibilityCaptureState::Idle: return "Idle";
    case CompatibilityCaptureState::Capturing: return "Capturing";
    case CompatibilityCaptureState::Completed: return "Completed";
  }
  return "Unknown";
}

std::string compatibility_physical_result_name(
    CompatibilityPhysicalResult result) {
  switch (result) {
    case CompatibilityPhysicalResult::Unknown: return "Unknown";
    case CompatibilityPhysicalResult::Worked: return "Worked";
    case CompatibilityPhysicalResult::Failed: return "Failed";
    case CompatibilityPhysicalResult::NotChecked: return "NotChecked";
  }
  return "Unknown";
}

std::string compatibility_guided_test_scenario_name(
    CompatibilityGuidedTestScenario scenario) {
  switch (scenario) {
    case CompatibilityGuidedTestScenario::ActiveSource:
      return "Active Source";
    case CompatibilityGuidedTestScenario::StandbyPowerOn:
      return "Standby + Power On";
    case CompatibilityGuidedTestScenario::UsbReconnect:
      return "USB Reconnect";
    case CompatibilityGuidedTestScenario::HdmiReconnect:
      return "HDMI / Display Reconnect";
  }
  return "Unknown";
}

std::string compatibility_guided_test_outcome_name(
    CompatibilityGuidedTestOutcome outcome) {
  switch (outcome) {
    case CompatibilityGuidedTestOutcome::Completed: return "Completed";
    case CompatibilityGuidedTestOutcome::Failed: return "Failed";
    case CompatibilityGuidedTestOutcome::Inconclusive: return "Inconclusive";
    case CompatibilityGuidedTestOutcome::Cancelled: return "Cancelled";
  }
  return "Unknown";
}

std::string compatibility_guided_observation_source_name(
    CompatibilityGuidedObservationSource source) {
  switch (source) {
    case CompatibilityGuidedObservationSource::Automatic: return "Automatic";
    case CompatibilityGuidedObservationSource::User: return "User";
  }
  return "Unknown";
}

CompatibilityCaptureCounts CompatibilityCapture::counts() const noexcept {
  return {.retained_cec_events = cec_events_.size(),
          .total_cec_events = received_cec_events_ + transmitted_cec_events_,
          .dropped_cec_events = dropped_cec_events_,
          .action_count = actions_.size(),
          .status_transition_count = status_transitions_.size(),
          .marker_count = markers_.size(),
          .note_count = notes_.size()};
}

std::optional<std::chrono::milliseconds> CompatibilityCapture::elapsed(
    TimePoint now) const noexcept {
  if (!started_at_) return std::nullopt;
  const auto end = ended_at_.value_or(now);
  return std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                               *started_at_);
}

std::optional<CompatibilityCaptureActionRecord>
CompatibilityCapture::latest_action_needing_physical_result() const {
  for (auto it = actions_.rbegin(); it != actions_.rend(); ++it) {
    if (it->completion &&
        it->physical_result == CompatibilityPhysicalResult::Unknown) {
      return *it;
    }
  }
  return std::nullopt;
}

bool CompatibilityCapture::start(
    const CompatibilityCaptureApplicationInfo& application,
    const FrontendStatus& status, const FrontendCapabilities& capabilities,
    const FrontendConfigurationSnapshot& configuration, TimePoint now) {
  if (state_ == CompatibilityCaptureState::Capturing) return false;
  clear();
  state_ = CompatibilityCaptureState::Capturing;
  application_ = application;
  initial_status_ = status;
  current_status_ = status;
  capabilities_ = capabilities;
  configuration_ = configuration;
  started_at_ = now;
  ended_at_.reset();
  return true;
}

bool CompatibilityCapture::stop(TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing) return false;
  ended_at_ = now;
  state_ = CompatibilityCaptureState::Completed;
  return true;
}

void CompatibilityCapture::clear() noexcept {
  state_ = CompatibilityCaptureState::Idle;
  application_.reset();
  initial_status_.reset();
  current_status_.reset();
  capabilities_.reset();
  configuration_.reset();
  started_at_.reset();
  ended_at_.reset();
  cec_events_.clear();
  actions_.clear();
  status_transitions_.clear();
  markers_.clear();
  notes_.clear();
  guided_tests_.clear();
  dropped_cec_events_ = 0;
  received_cec_events_ = 0;
  transmitted_cec_events_ = 0;
  opcode_frequency_.clear();
  semantic_frequency_.clear();
  next_sequence_ = 1;
}

void CompatibilityCapture::record_status_transition(const FrontendStatus& status,
                                                    TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  if (current_status_ && *current_status_ == status) return;
  current_status_ = status;
  status_transitions_.push_back(
      {.sequence = next_sequence_++, .timestamp = now, .status = status});
}

void CompatibilityCapture::record_cec_event(const FrontendCecEvent& event) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  if (event.direction == CecDirection::Received) {
    ++received_cec_events_;
  } else {
    ++transmitted_cec_events_;
  }
  std::ostringstream opcode_key;
  opcode_key << HexByte(event.opcode);
  ++opcode_frequency_[opcode_key.str()];
  if (event.semantic && !event.semantic->empty()) {
    ++semantic_frequency_[*event.semantic];
  }
  cec_events_.push_back(event);
  if (cec_events_.size() > kMaximumCecEvents) {
    cec_events_.erase(cec_events_.begin());
    ++dropped_cec_events_;
  }
}

void CompatibilityCapture::record_action_requested(
    FrontendActionRequestId request_id, FrontendAction action, TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing || request_id == 0) return;
  actions_.push_back({.sequence = next_sequence_++,
                      .request_id = request_id,
                      .action = action,
                      .requested_at = now});
}

void CompatibilityCapture::record_action_completed(
    FrontendActionRequestId request_id, const FrontendActionResult& result,
    TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  auto* record = [&]() -> CompatibilityCaptureActionRecord* {
    for (auto& action : actions_) {
      if (action.request_id == request_id) return &action;
    }
    actions_.push_back({.sequence = next_sequence_++,
                        .request_id = request_id,
                        .action = result.action,
                        .requested_at = now});
    return &actions_.back();
  }();
  record->action = result.action;
  record->completed_at = now;
  record->completion = result;
}

void CompatibilityCapture::record_action_physical_result(
    FrontendActionRequestId request_id, CompatibilityPhysicalResult result,
    std::string note, TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  for (auto& action : actions_) {
    if (action.request_id != request_id) continue;
    action.physical_result = result;
    action.physical_note = sanitize_note_like(std::move(note));
    action.physical_recorded_at = now;
    return;
  }
}

void CompatibilityCapture::record_marker(std::string name, TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  const auto clean = sanitize_note_like(std::move(name));
  if (clean.empty()) return;
  markers_.push_back(
      {.sequence = next_sequence_++, .timestamp = now, .name = clean});
}

void CompatibilityCapture::record_note(std::string text, TimePoint now) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  const auto clean = sanitize_note_like(std::move(text));
  if (clean.empty()) return;
  notes_.push_back(
      {.sequence = next_sequence_++, .timestamp = now, .text = clean});
}

void CompatibilityCapture::upsert_guided_test(
    CompatibilityGuidedTestRecord record) {
  if (state_ != CompatibilityCaptureState::Capturing) return;
  for (auto& existing : guided_tests_) {
    if (existing.test_id != record.test_id) continue;
    existing = std::move(record);
    return;
  }
  guided_tests_.push_back(std::move(record));
}

std::vector<std::string> CompatibilityCapture::recent_timeline_lines(
    std::size_t limit) const {
  std::vector<std::string> lines;
  const auto timeline = timeline_entries();
  const auto start =
      timeline.size() > limit ? timeline.size() - limit : std::size_t{0};
  for (std::size_t i = start; i < timeline.size(); ++i) {
    lines.push_back(timeline[i].line);
  }
  return lines;
}

CompatibilityCaptureExport CompatibilityCapture::build_export(
    std::string_view label) const {
  CompatibilityCaptureExport output;
  output.suggested_basename = default_basename(label);
  if (!started_at_ || !application_ || !initial_status_ || !capabilities_ ||
      !configuration_) {
    return output;
  }

  const auto summary = counts();
  const auto end = ended_at_.value_or(*started_at_);
  const auto final_status = current_status_.value_or(*initial_status_);
  const auto timeline = timeline_entries();

  std::ostringstream markdown;
  markdown << "# CEC Injector Mate Compatibility Capture\n\n";
  markdown << "## Capture\n\n";
  markdown << "- State: " << compatibility_capture_state_name(state_) << '\n';
  markdown << "- Start: " << TimestampString(*started_at_) << '\n';
  markdown << "- End: " << TimestampString(end) << '\n';
  markdown << "- Duration: "
           << std::chrono::duration_cast<std::chrono::seconds>(end -
                                                               *started_at_)
                  .count()
           << " s\n";
  markdown << "- Platform: " << MarkdownText(application_->platform) << '\n';
  markdown << "- Mate revision: " << MarkdownText(application_->git_revision)
           << '\n';
  markdown << "- Mate version: " << MarkdownText(application_->version) << "\n\n";

  markdown << "## Device\n\n";
  markdown << "- DeviceId: " << DeviceName(initial_status_->selected_device)
           << '\n';
  markdown << "- Session: " << SessionName(initial_status_->session_state) << '\n';
  markdown << "- Logical address: "
           << LogicalAddressName(initial_status_->logical_address) << '\n';
  markdown << "- Wake supported: " << BoolName(initial_status_->wake.supported)
           << '\n';
  markdown << "- Wake configured: "
           << BoolName(initial_status_->wake.configured) << '\n';
  markdown << "- Wake armed: " << BoolName(initial_status_->wake.armed) << "\n\n";

  markdown << "## Display\n\n";
  markdown << "- Display summary: " << MarkdownText(initial_status_->display_name)
           << '\n';
  markdown << "- EDID available: " << BoolName(initial_status_->edid_available)
           << '\n';
  markdown << "- EDID bytes: " << initial_status_->edid_byte_count << '\n';
  markdown << "- Physical Address status: "
           << PhysicalAddressStatusName(initial_status_->physical_address_status)
           << '\n';
  markdown << "- Physical Address: "
           << PhysicalAddressName(initial_status_->physical_address) << "\n\n";

  markdown << "## Configuration\n\n";
  markdown << "- Revision: " << configuration_->revision << '\n';
  markdown << "- power_on_boot: "
           << BoolName(configuration_->configuration.policy.power_on_boot) << '\n';
  markdown << "- power_on_resume: "
           << BoolName(configuration_->configuration.policy.power_on_resume)
           << '\n';
  markdown << "- standby_on_suspend: "
           << BoolName(configuration_->configuration.policy.standby_on_suspend)
           << '\n';
  markdown << "- standby_on_shutdown: "
           << BoolName(configuration_->configuration.policy.standby_on_shutdown)
           << '\n';
  markdown << "- suspend_on_tv_standby: "
           << BoolName(
                  configuration_->configuration.policy.suspend_on_tv_standby)
           << '\n';
  markdown << "- wake_host_on_tv_source_request: "
           << BoolName(configuration_->configuration.policy
                           .wake_host_on_tv_source_request)
           << '\n';
  markdown << "- remote_enabled: "
           << BoolName(configuration_->configuration.remote.enabled) << '\n';
  markdown << "- remote_profile: "
           << RemoteProfileName(configuration_->configuration.remote.profile)
           << "\n\n";

  markdown << "## Capabilities\n\n";
  markdown << "- configuration: " << BoolName(capabilities_->configuration)
           << '\n';
  markdown << "- remote_input: " << BoolName(capabilities_->remote_input)
           << '\n';
  markdown << "- remote_wake: " << BoolName(capabilities_->remote_wake) << '\n';
  markdown << "- tv_power_actions: " << BoolName(capabilities_->tv_power_actions)
           << '\n';
  markdown << "- cec_monitor: " << BoolName(capabilities_->cec_monitor)
           << "\n\n";

  markdown << "## Timeline\n\n";
  if (timeline.empty()) {
    markdown << "- No captured activity\n";
  } else {
    for (const auto& entry : timeline) markdown << "- " << entry.line << '\n';
  }
  markdown << '\n';

  markdown << "## Action Summary\n\n";
  if (actions_.empty()) {
    markdown << "- No manual actions recorded\n";
  } else {
    for (const auto& action : actions_) {
      markdown << "- " << ActionSummary(action) << '\n';
      if (action.physical_result != CompatibilityPhysicalResult::Unknown ||
          !action.physical_note.empty()) {
        markdown << "  Physical: "
                 << compatibility_physical_result_name(action.physical_result);
        if (!action.physical_note.empty()) {
          markdown << " — " << MarkdownText(action.physical_note);
        }
        markdown << '\n';
      }
    }
  }
  markdown << '\n';

  markdown << "## CEC Summary\n\n";
  markdown << "- RX frames: " << received_cec_events_ << '\n';
  markdown << "- TX frames: " << transmitted_cec_events_ << '\n';
  markdown << "- Retained event rows: " << summary.retained_cec_events << '\n';
  markdown << "- Dropped event rows: " << summary.dropped_cec_events << '\n';
  markdown << "- Opcode frequency:\n";
  if (opcode_frequency_.empty()) {
    markdown << "  None\n";
  } else {
    for (const auto& [opcode, count] : opcode_frequency_) {
      markdown << "  " << opcode << ": " << count << '\n';
    }
  }
  markdown << "- Semantic frequency:\n";
  if (semantic_frequency_.empty()) {
    markdown << "  None\n";
  } else {
    for (const auto& [semantic, count] : semantic_frequency_) {
      markdown << "  " << MarkdownText(semantic) << ": " << count << '\n';
    }
  }
  markdown << '\n';

  markdown << "## Notes\n\n";
  if (notes_.empty()) {
    markdown << "- No user annotations\n";
  } else {
    for (const auto& note : notes_) {
      markdown << "- [" << RelativeTimestamp(*started_at_, note.timestamp)
               << "] " << MarkdownText(note.text) << '\n';
    }
  }
  markdown << '\n';

  markdown << "## Guided Tests\n\n";
  if (guided_tests_.empty()) {
    markdown << "- No guided tests recorded\n";
  } else {
    for (const auto& test : guided_tests_) {
      markdown << "### "
               << compatibility_guided_test_scenario_name(test.scenario)
               << "\n\n";
      markdown << "- Outcome: "
               << compatibility_guided_test_outcome_name(test.outcome) << '\n';
      markdown << "- Started: " << TimestampString(test.started_at) << '\n';
      markdown << "- Completed: "
               << (test.completed_at ? TimestampString(*test.completed_at)
                                     : std::string{"InProgress"})
               << '\n';
      markdown << "- Display: " << MarkdownText(test.display_name) << '\n';
      if (!test.reason.empty()) {
        markdown << "- Reason: " << MarkdownText(test.reason) << '\n';
      }
      if (!test.related_action_request_ids.empty()) {
        markdown << "- Related actions:";
        for (const auto request_id : test.related_action_request_ids) {
          markdown << ' ' << '#' << request_id;
        }
        markdown << '\n';
      }
      if (!test.steps.empty()) {
        markdown << "- Steps:\n";
        for (const auto& step : test.steps) {
          markdown << "  - Step " << step.index << ": "
                   << MarkdownText(step.title) << " ["
                   << MarkdownText(step.state) << "]\n";
          if (step.action_completion) {
            markdown << "    backend: "
                     << OperationResultName(step.action_completion->result)
                     << ", " << step.action_completion->duration.count()
                     << " ms\n";
          }
          if (step.physical_result) {
            markdown << "    physical: "
                     << compatibility_physical_result_name(*step.physical_result);
            if (!step.physical_note.empty()) {
              markdown << " — " << MarkdownText(step.physical_note);
            }
            markdown << '\n';
          }
        }
      }
      if (!test.observations.empty()) {
        markdown << "- Evidence:\n";
        for (const auto& observation : test.observations) {
          markdown << "  - ["
                   << RelativeTimestamp(*started_at_, observation.timestamp)
                   << "] "
                   << compatibility_guided_observation_source_name(
                          observation.source)
                   << ' ' << MarkdownText(observation.kind) << ": "
                   << MarkdownText(observation.detail) << '\n';
        }
      }
      markdown << '\n';
    }
  }
  markdown << '\n';

  markdown << "## Compatibility Summary\n\n";
  markdown << "- Final session state: " << SessionName(final_status.session_state)
           << '\n';
  markdown << "- Final Physical Address status: "
           << PhysicalAddressStatusName(final_status.physical_address_status)
           << '\n';
  markdown << "- Final Physical Address: "
           << PhysicalAddressName(final_status.physical_address) << '\n';
  if (actions_.empty()) {
    markdown << "- No manual actions attempted during this capture\n";
  } else {
    for (const auto& action : actions_) {
      markdown << "- " << ActionName(action.action) << " backend: ";
      if (action.completion) {
        markdown << OperationResultName(action.completion->result);
      } else {
        markdown << "NotCompleted";
      }
      if (action.physical_result != CompatibilityPhysicalResult::Unknown) {
        markdown << "; physical: "
                 << compatibility_physical_result_name(action.physical_result);
      }
      markdown << '\n';
    }
  }
  for (const auto& test : guided_tests_) {
    markdown << "- "
             << compatibility_guided_test_scenario_name(test.scenario) << ": "
             << compatibility_guided_test_outcome_name(test.outcome);
    if (!test.reason.empty()) {
      markdown << " (" << MarkdownText(test.reason) << ')';
    }
    markdown << '\n';
  }

  std::ostringstream json;
  json << "{\n";
  json << "  \"schema_version\": 1,\n";
  json << "  \"capture\": {\n";
  json << "    \"state\": "
       << JsonString(compatibility_capture_state_name(state_)) << ",\n";
  json << "    \"started_at\": " << JsonString(TimestampString(*started_at_))
       << ",\n";
  json << "    \"ended_at\": " << JsonString(TimestampString(end)) << ",\n";
  json << "    \"duration_ms\": "
       << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                *started_at_)
              .count()
       << "\n";
  json << "  },\n";
  json << "  \"application\": {\n";
  json << "    \"name\": " << JsonString(application_->application_name)
       << ",\n";
  json << "    \"platform\": " << JsonString(application_->platform) << ",\n";
  json << "    \"version\": " << JsonString(application_->version) << ",\n";
  json << "    \"git_revision\": "
       << JsonString(application_->git_revision) << "\n";
  json << "  },\n";
  json << "  \"device\": {\n";
  json << "    \"selected_device\": "
       << JsonString(DeviceName(initial_status_->selected_device)) << ",\n";
  json << "    \"device_display_name\": "
       << JsonString(initial_status_->device_display_name) << ",\n";
  json << "    \"injector_connected\": "
       << (initial_status_->injector_connected ? "true" : "false") << ",\n";
  json << "    \"session_state\": "
       << JsonString(SessionName(initial_status_->session_state)) << ",\n";
  json << "    \"logical_address\": "
       << (initial_status_->logical_address
               ? std::to_string(*initial_status_->logical_address)
               : "null")
       << ",\n";
  json << "    \"wake\": {\n";
  json << "      \"supported\": "
       << (initial_status_->wake.supported ? "true" : "false") << ",\n";
  json << "      \"configured\": "
       << (initial_status_->wake.configured ? "true" : "false") << ",\n";
  json << "      \"armed\": "
       << (initial_status_->wake.armed ? "true" : "false") << ",\n";
  json << "      \"reason_available\": "
       << (initial_status_->wake.reason_available ? "true" : "false") << "\n";
  json << "    }\n";
  json << "  },\n";
  json << "  \"display\": {\n";
  json << "    \"display_name\": " << JsonString(initial_status_->display_name)
       << ",\n";
  json << "    \"edid_available\": "
       << (initial_status_->edid_available ? "true" : "false") << ",\n";
  json << "    \"edid_byte_count\": " << initial_status_->edid_byte_count
       << ",\n";
  json << "    \"physical_address_status\": "
       << JsonString(
              PhysicalAddressStatusName(initial_status_->physical_address_status))
       << ",\n";
  json << "    \"physical_address\": "
       << (initial_status_->physical_address
               ? JsonString(PhysicalAddressName(initial_status_->physical_address))
               : "null")
       << "\n";
  json << "  },\n";
  json << "  \"configuration\": {\n";
  json << "    \"revision\": " << configuration_->revision << ",\n";
  json << "    \"policy\": {\n";
  json << "      \"power_on_boot\": "
       << (configuration_->configuration.policy.power_on_boot ? "true" : "false")
       << ",\n";
  json << "      \"power_on_resume\": "
       << (configuration_->configuration.policy.power_on_resume ? "true"
                                                                : "false")
       << ",\n";
  json << "      \"standby_on_suspend\": "
       << (configuration_->configuration.policy.standby_on_suspend ? "true"
                                                                   : "false")
       << ",\n";
  json << "      \"standby_on_shutdown\": "
       << (configuration_->configuration.policy.standby_on_shutdown ? "true"
                                                                    : "false")
       << ",\n";
  json << "      \"suspend_on_tv_standby\": "
       << (configuration_->configuration.policy.suspend_on_tv_standby ? "true"
                                                                      : "false")
       << ",\n";
  json << "      \"wake_host_on_tv_source_request\": "
       << (configuration_->configuration.policy
                   .wake_host_on_tv_source_request
               ? "true"
               : "false")
       << "\n";
  json << "    },\n";
  json << "    \"remote\": {\n";
  json << "      \"enabled\": "
       << (configuration_->configuration.remote.enabled ? "true" : "false")
       << ",\n";
  json << "      \"profile\": "
       << JsonString(
              RemoteProfileName(configuration_->configuration.remote.profile))
       << "\n";
  json << "    }\n";
  json << "  },\n";
  json << "  \"capabilities\": {\n";
  json << "    \"configuration\": "
       << (capabilities_->configuration ? "true" : "false") << ",\n";
  json << "    \"remote_input\": "
       << (capabilities_->remote_input ? "true" : "false") << ",\n";
  json << "    \"remote_wake\": "
       << (capabilities_->remote_wake ? "true" : "false") << ",\n";
  json << "    \"tv_power_actions\": "
       << (capabilities_->tv_power_actions ? "true" : "false") << ",\n";
  json << "    \"cec_monitor\": "
       << (capabilities_->cec_monitor ? "true" : "false") << "\n";
  json << "  },\n";

  json << "  \"events\": [\n";
  for (std::size_t i = 0; i < cec_events_.size(); ++i) {
    const auto& event = cec_events_[i];
    json << "    {\n";
    json << "      \"timestamp\": " << JsonString(TimestampString(event.timestamp))
         << ",\n";
    json << "      \"direction\": " << JsonString(DirectionName(event.direction))
         << ",\n";
    json << "      \"source\": " << static_cast<unsigned>(event.source) << ",\n";
    json << "      \"destination\": "
         << static_cast<unsigned>(event.destination) << ",\n";
    json << "      \"opcode\": " << JsonString(HexByte(event.opcode)) << ",\n";
    json << "      \"payload\": [";
    for (std::size_t j = 0; j < event.parameters.size(); ++j) {
      if (j != 0) json << ", ";
      json << JsonString(HexByte(event.parameters[j]));
    }
    json << "],\n";
    json << "      \"semantic\": "
         << (event.semantic ? JsonString(*event.semantic) : "null") << "\n";
    json << "    }";
    if (i + 1 != cec_events_.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n";

  json << "  \"actions\": [\n";
  for (std::size_t i = 0; i < actions_.size(); ++i) {
    const auto& action = actions_[i];
    json << "    {\n";
    json << "      \"request_id\": " << action.request_id << ",\n";
    json << "      \"action\": " << JsonString(ActionName(action.action)) << ",\n";
    json << "      \"requested_at\": "
         << JsonString(TimestampString(action.requested_at)) << ",\n";
    json << "      \"completed_at\": "
         << (action.completed_at ? JsonString(TimestampString(*action.completed_at))
                                 : "null")
         << ",\n";
    json << "      \"result\": "
         << (action.completion
                 ? JsonString(OperationResultName(action.completion->result))
                 : "null")
         << ",\n";
    json << "      \"duration_ms\": "
         << (action.completion
                 ? std::to_string(action.completion->duration.count())
                 : "null")
         << ",\n";
    json << "      \"message\": "
         << (action.completion ? JsonString(action.completion->message) : "null")
         << ",\n";
    json << "      \"physical_result\": "
         << JsonString(compatibility_physical_result_name(action.physical_result))
         << ",\n";
    json << "      \"physical_recorded_at\": "
         << (action.physical_recorded_at
                 ? JsonString(TimestampString(*action.physical_recorded_at))
                 : "null")
         << ",\n";
    json << "      \"physical_note\": " << JsonString(action.physical_note)
         << "\n";
    json << "    }";
    if (i + 1 != actions_.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n";

  json << "  \"status_transitions\": [\n";
  for (std::size_t i = 0; i < status_transitions_.size(); ++i) {
    const auto& transition = status_transitions_[i];
    json << "    {\n";
    json << "      \"timestamp\": "
         << JsonString(TimestampString(transition.timestamp)) << ",\n";
    json << "      \"service_available\": "
         << (transition.status.service_available ? "true" : "false") << ",\n";
    json << "      \"injector_connected\": "
         << (transition.status.injector_connected ? "true" : "false") << ",\n";
    json << "      \"selected_device\": "
         << JsonString(DeviceName(transition.status.selected_device)) << ",\n";
    json << "      \"display_name\": "
         << JsonString(transition.status.display_name) << ",\n";
    json << "      \"physical_address_status\": "
         << JsonString(PhysicalAddressStatusName(
                transition.status.physical_address_status))
         << ",\n";
    json << "      \"physical_address\": "
         << (transition.status.physical_address
                 ? JsonString(
                       PhysicalAddressName(transition.status.physical_address))
                 : "null")
         << ",\n";
    json << "      \"logical_address\": "
         << (transition.status.logical_address
                 ? std::to_string(*transition.status.logical_address)
                 : "null")
         << ",\n";
    json << "      \"session_state\": "
         << JsonString(SessionName(transition.status.session_state)) << ",\n";
    json << "      \"wake_supported\": "
         << (transition.status.wake.supported ? "true" : "false") << ",\n";
    json << "      \"wake_configured\": "
         << (transition.status.wake.configured ? "true" : "false") << ",\n";
    json << "      \"wake_armed\": "
         << (transition.status.wake.armed ? "true" : "false") << ",\n";
    json << "      \"host_power_state\": "
         << JsonString(HostPowerStateName(transition.status.host_power_state))
         << ",\n";
    json << "      \"edid_available\": "
         << (transition.status.edid_available ? "true" : "false") << ",\n";
    json << "      \"edid_byte_count\": "
         << transition.status.edid_byte_count << "\n";
    json << "    }";
    if (i + 1 != status_transitions_.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n";

  json << "  \"markers\": [\n";
  for (std::size_t i = 0; i < markers_.size(); ++i) {
    const auto& marker = markers_[i];
    json << "    {\"timestamp\": " << JsonString(TimestampString(marker.timestamp))
         << ", \"name\": " << JsonString(marker.name) << "}";
    if (i + 1 != markers_.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n";

  json << "  \"notes\": [\n";
  for (std::size_t i = 0; i < notes_.size(); ++i) {
    const auto& note = notes_[i];
    json << "    {\"timestamp\": " << JsonString(TimestampString(note.timestamp))
         << ", \"text\": " << JsonString(note.text) << "}";
    if (i + 1 != notes_.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n";

  json << "  \"guided_tests\": [\n";
  for (std::size_t i = 0; i < guided_tests_.size(); ++i) {
    const auto& test = guided_tests_[i];
    json << "    {\n";
    json << "      \"test_id\": " << JsonString(test.test_id) << ",\n";
    json << "      \"scenario\": "
         << JsonString(compatibility_guided_test_scenario_name(test.scenario))
         << ",\n";
    json << "      \"display_name\": " << JsonString(test.display_name)
         << ",\n";
    json << "      \"started_at\": " << JsonString(TimestampString(test.started_at))
         << ",\n";
    json << "      \"completed_at\": "
         << (test.completed_at ? JsonString(TimestampString(*test.completed_at))
                               : "null")
         << ",\n";
    json << "      \"outcome\": "
         << JsonString(compatibility_guided_test_outcome_name(test.outcome))
         << ",\n";
    json << "      \"reason\": " << JsonString(test.reason) << ",\n";
    json << "      \"related_action_request_ids\": [";
    for (std::size_t j = 0; j < test.related_action_request_ids.size(); ++j) {
      if (j != 0) json << ", ";
      json << test.related_action_request_ids[j];
    }
    json << "],\n";
    json << "      \"steps\": [\n";
    for (std::size_t j = 0; j < test.steps.size(); ++j) {
      const auto& step = test.steps[j];
      json << "        {\n";
      json << "          \"index\": " << step.index << ",\n";
      json << "          \"title\": " << JsonString(step.title) << ",\n";
      json << "          \"instruction\": " << JsonString(step.instruction)
           << ",\n";
      json << "          \"state\": " << JsonString(step.state) << ",\n";
      json << "          \"started_at\": "
           << JsonString(TimestampString(step.started_at)) << ",\n";
      json << "          \"completed_at\": "
           << (step.completed_at ? JsonString(TimestampString(*step.completed_at))
                                 : "null")
           << ",\n";
      json << "          \"action_request_id\": "
           << (step.action_request_id
                   ? std::to_string(*step.action_request_id)
                   : "null")
           << ",\n";
      json << "          \"action_result\": "
           << (step.action_completion
                   ? JsonString(
                         OperationResultName(step.action_completion->result))
                   : "null")
           << ",\n";
      json << "          \"action_duration_ms\": "
           << (step.action_completion
                   ? std::to_string(step.action_completion->duration.count())
                   : "null")
           << ",\n";
      json << "          \"physical_result\": "
           << (step.physical_result
                   ? JsonString(
                         compatibility_physical_result_name(*step.physical_result))
                   : "null")
           << ",\n";
      json << "          \"physical_note\": "
           << JsonString(step.physical_note) << "\n";
      json << "        }";
      if (j + 1 != test.steps.size()) json << ',';
      json << '\n';
    }
    json << "      ],\n";
    json << "      \"observations\": [\n";
    for (std::size_t j = 0; j < test.observations.size(); ++j) {
      const auto& observation = test.observations[j];
      json << "        {\n";
      json << "          \"timestamp\": "
           << JsonString(TimestampString(observation.timestamp)) << ",\n";
      json << "          \"source\": "
           << JsonString(compatibility_guided_observation_source_name(
                  observation.source))
           << ",\n";
      json << "          \"kind\": " << JsonString(observation.kind) << ",\n";
      json << "          \"detail\": " << JsonString(observation.detail)
           << ",\n";
      json << "          \"request_id\": "
           << (observation.request_id ? std::to_string(*observation.request_id)
                                      : "null")
           << "\n";
      json << "        }";
      if (j + 1 != test.observations.size()) json << ',';
      json << '\n';
    }
    json << "      ],\n";
    json << "      \"physical_confirmations\": [\n";
    for (std::size_t j = 0; j < test.physical_confirmations.size(); ++j) {
      const auto& confirmation = test.physical_confirmations[j];
      json << "        {\n";
      json << "          \"timestamp\": "
           << JsonString(TimestampString(confirmation.timestamp)) << ",\n";
      json << "          \"request_id\": " << confirmation.request_id << ",\n";
      json << "          \"result\": "
           << JsonString(
                  compatibility_physical_result_name(confirmation.result))
           << ",\n";
      json << "          \"note\": " << JsonString(confirmation.note) << "\n";
      json << "        }";
      if (j + 1 != test.physical_confirmations.size()) json << ',';
      json << '\n';
    }
    json << "      ]\n";
    json << "    }";
    if (i + 1 != guided_tests_.size()) json << ',';
    json << '\n';
  }
  json << "  ],\n";

  json << "  \"counters\": {\n";
  json << "    \"rx_events\": " << received_cec_events_ << ",\n";
  json << "    \"tx_events\": " << transmitted_cec_events_ << ",\n";
  json << "    \"retained_events\": " << cec_events_.size() << ",\n";
  json << "    \"dropped_events\": " << dropped_cec_events_ << ",\n";
  json << "    \"status_transition_count\": " << status_transitions_.size()
       << ",\n";
  json << "    \"action_count\": " << actions_.size() << ",\n";
  json << "    \"marker_count\": " << markers_.size() << ",\n";
  json << "    \"note_count\": " << notes_.size() << ",\n";
  json << "    \"opcode_frequency\": {\n";
  for (auto it = opcode_frequency_.begin(); it != opcode_frequency_.end(); ++it) {
    json << "      " << JsonString(it->first) << ": " << it->second;
    if (std::next(it) != opcode_frequency_.end()) json << ',';
    json << '\n';
  }
  json << "    },\n";
  json << "    \"semantic_frequency\": {\n";
  for (auto it = semantic_frequency_.begin(); it != semantic_frequency_.end();
       ++it) {
    json << "      " << JsonString(it->first) << ": " << it->second;
    if (std::next(it) != semantic_frequency_.end()) json << ',';
    json << '\n';
  }
  json << "    }\n";
  json << "  }\n";
  json << "}\n";

  output.markdown = markdown.str();
  output.json = json.str();
  return output;
}

std::string CompatibilityCapture::default_basename(std::string_view label) const {
  const auto stamp = started_at_ ? FilenameTimestamp(*started_at_)
                                 : FilenameTimestamp(Clock::now());
  const auto clean = sanitize_label(label);
  return clean.empty() ? "cec-compatibility-" + stamp
                       : "cec-compatibility-" + stamp + "-" + clean;
}

std::vector<CompatibilityCapture::TimelineEntry>
CompatibilityCapture::timeline_entries() const {
  std::vector<TimelineEntry> entries;
  if (!started_at_) return entries;

  entries.reserve(status_transitions_.size() + cec_events_.size() +
                  actions_.size() * 2 + markers_.size() + notes_.size());

  const auto push = [&](std::uint64_t sequence, TimePoint timestamp,
                        std::string text) {
    entries.push_back({sequence, timestamp,
                       "[" + RelativeTimestamp(*started_at_, timestamp) + "] " +
                           std::move(text)});
  };

  FrontendStatus previous = initial_status_.value_or(FrontendStatus{});
  for (const auto& transition : status_transitions_) {
    push(transition.sequence, transition.timestamp,
         "Status: " + DescribeStatusChange(previous, transition.status));
    previous = transition.status;
  }
  for (std::size_t i = 0; i < cec_events_.size(); ++i) {
    push(1000000000ULL + i, cec_events_[i].timestamp, EventSummary(cec_events_[i]));
  }
  for (const auto& action : actions_) {
    push(action.sequence, action.requested_at,
         "Action request #" + std::to_string(action.request_id) + ' ' +
             ActionName(action.action));
    if (action.completed_at) {
      push(action.sequence + 1, *action.completed_at, ActionSummary(action));
    }
    if (action.physical_recorded_at) {
      push(action.sequence + 2, *action.physical_recorded_at,
           PhysicalSummary(action));
    }
  }
  for (const auto& marker : markers_) {
    push(marker.sequence, marker.timestamp, "Marker: " + marker.name);
  }
  for (const auto& note : notes_) {
    push(note.sequence, note.timestamp, "Note: " + note.text);
  }

  std::sort(entries.begin(), entries.end(),
            [](const TimelineEntry& lhs, const TimelineEntry& rhs) {
              if (lhs.timestamp != rhs.timestamp) return lhs.timestamp < rhs.timestamp;
              return lhs.sequence < rhs.sequence;
            });
  return entries;
}

std::string CompatibilityCapture::sanitize_note_like(std::string text) {
  std::string clean;
  clean.reserve(text.size());
  bool previous_space = false;
  for (const unsigned char ch : text) {
    const bool space = std::isspace(ch) != 0;
    if (space) {
      if (!previous_space) clean.push_back(' ');
    } else {
      clean.push_back(static_cast<char>(ch));
    }
    previous_space = space;
  }
  while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());
  while (!clean.empty() && clean.back() == ' ') clean.pop_back();
  return clean;
}

std::string CompatibilityCapture::sanitize_label(std::string_view value) {
  std::string clean;
  bool previous_dash = false;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      clean.push_back(static_cast<char>(std::tolower(ch)));
      previous_dash = false;
      continue;
    }
    if (ch == '-' || ch == '_' || std::isspace(ch) != 0) {
      if (!clean.empty() && !previous_dash) {
        clean.push_back('-');
        previous_dash = true;
      }
    }
  }
  while (!clean.empty() && clean.back() == '-') clean.pop_back();
  return clean;
}

}  // namespace cec_injector::mate::frontend

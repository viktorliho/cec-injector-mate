#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cec_injector/mate/frontend/frontend_backend.h"

namespace cec_injector::mate::frontend {

enum class CompatibilityCaptureState { Idle, Capturing, Completed };

enum class CompatibilityPhysicalResult { Unknown, Worked, Failed, NotChecked };
enum class CompatibilityGuidedTestScenario {
  ActiveSource,
  StandbyPowerOn,
  UsbReconnect,
  HdmiReconnect,
};
enum class CompatibilityGuidedTestOutcome {
  Completed,
  Failed,
  Inconclusive,
  Cancelled,
};
enum class CompatibilityGuidedObservationSource { Automatic, User };

struct CompatibilityCaptureApplicationInfo {
  std::string application_name;
  std::string platform;
  std::string version;
  std::string git_revision;

  bool operator==(const CompatibilityCaptureApplicationInfo&) const = default;
};

struct CompatibilityCaptureCounts {
  std::size_t retained_cec_events{};
  std::size_t total_cec_events{};
  std::size_t dropped_cec_events{};
  std::size_t action_count{};
  std::size_t status_transition_count{};
  std::size_t marker_count{};
  std::size_t note_count{};
};

struct CompatibilityCaptureNote {
  std::uint64_t sequence{};
  std::chrono::system_clock::time_point timestamp;
  std::string text;
};

struct CompatibilityCaptureMarker {
  std::uint64_t sequence{};
  std::chrono::system_clock::time_point timestamp;
  std::string name;
};

struct CompatibilityCaptureStatusTransition {
  std::uint64_t sequence{};
  std::chrono::system_clock::time_point timestamp;
  FrontendStatus status;
};

struct CompatibilityCaptureActionRecord {
  std::uint64_t sequence{};
  FrontendActionRequestId request_id{};
  FrontendAction action{FrontendAction::PowerOnTv};
  std::chrono::system_clock::time_point requested_at;
  std::optional<std::chrono::system_clock::time_point> completed_at;
  std::optional<FrontendActionResult> completion;
  CompatibilityPhysicalResult physical_result{
      CompatibilityPhysicalResult::Unknown};
  std::optional<std::chrono::system_clock::time_point> physical_recorded_at;
  std::string physical_note;
};

struct CompatibilityGuidedTestStepRecord {
  std::size_t index{};
  std::string title;
  std::string instruction;
  std::string state;
  std::chrono::system_clock::time_point started_at;
  std::optional<std::chrono::system_clock::time_point> completed_at;
  std::optional<FrontendActionRequestId> action_request_id;
  std::optional<FrontendActionResult> action_completion;
  std::optional<CompatibilityPhysicalResult> physical_result;
  std::string physical_note;
};

struct CompatibilityGuidedTestObservation {
  std::chrono::system_clock::time_point timestamp;
  CompatibilityGuidedObservationSource source{
      CompatibilityGuidedObservationSource::Automatic};
  std::string kind;
  std::string detail;
  std::optional<FrontendActionRequestId> request_id;
};

struct CompatibilityGuidedTestPhysicalConfirmation {
  std::chrono::system_clock::time_point timestamp;
  FrontendActionRequestId request_id{};
  CompatibilityPhysicalResult result{CompatibilityPhysicalResult::Unknown};
  std::string note;
};

struct CompatibilityGuidedTestRecord {
  std::string test_id;
  CompatibilityGuidedTestScenario scenario{
      CompatibilityGuidedTestScenario::ActiveSource};
  std::string display_name;
  std::chrono::system_clock::time_point started_at;
  std::optional<std::chrono::system_clock::time_point> completed_at;
  CompatibilityGuidedTestOutcome outcome{
      CompatibilityGuidedTestOutcome::Inconclusive};
  std::string reason;
  std::vector<CompatibilityGuidedTestStepRecord> steps;
  std::vector<CompatibilityGuidedTestObservation> observations;
  std::vector<CompatibilityGuidedTestPhysicalConfirmation>
      physical_confirmations;
  std::vector<FrontendActionRequestId> related_action_request_ids;
};

struct CompatibilityCaptureExport {
  std::string suggested_basename;
  std::string markdown;
  std::string json;
};

class CompatibilityCapture final {
 public:
  static constexpr std::size_t kMaximumCecEvents = 10000;
  static constexpr std::size_t kRecentTimelineLimit = 200;

  [[nodiscard]] CompatibilityCaptureState state() const noexcept {
    return state_;
  }
  [[nodiscard]] const FrontendStatus* initial_status() const noexcept {
    return initial_status_ ? &*initial_status_ : nullptr;
  }
  [[nodiscard]] const FrontendStatus* current_status() const noexcept {
    return current_status_ ? &*current_status_ : nullptr;
  }
  [[nodiscard]] const FrontendCapabilities* capabilities() const noexcept {
    return capabilities_ ? &*capabilities_ : nullptr;
  }
  [[nodiscard]] const FrontendConfigurationSnapshot* configuration() const
      noexcept {
    return configuration_ ? &*configuration_ : nullptr;
  }
  [[nodiscard]] const CompatibilityCaptureApplicationInfo* application() const
      noexcept {
    return application_ ? &*application_ : nullptr;
  }
  [[nodiscard]] const std::vector<CompatibilityGuidedTestRecord>&
  guided_tests() const noexcept {
    return guided_tests_;
  }
  [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>&
  started_at() const noexcept {
    return started_at_;
  }
  [[nodiscard]] const std::optional<std::chrono::system_clock::time_point>&
  ended_at() const noexcept {
    return ended_at_;
  }

  [[nodiscard]] CompatibilityCaptureCounts counts() const noexcept;
  [[nodiscard]] std::optional<std::chrono::milliseconds> elapsed(
      std::chrono::system_clock::time_point now) const noexcept;
  [[nodiscard]] std::optional<CompatibilityCaptureActionRecord>
  latest_action_needing_physical_result() const;

  bool start(const CompatibilityCaptureApplicationInfo& application,
             const FrontendStatus& status,
             const FrontendCapabilities& capabilities,
             const FrontendConfigurationSnapshot& configuration,
             std::chrono::system_clock::time_point now);
  bool stop(std::chrono::system_clock::time_point now);
  void clear() noexcept;

  void record_status_transition(const FrontendStatus& status,
                                std::chrono::system_clock::time_point now);
  void record_cec_event(const FrontendCecEvent& event);
  void record_action_requested(FrontendActionRequestId request_id,
                               FrontendAction action,
                               std::chrono::system_clock::time_point now);
  void record_action_completed(FrontendActionRequestId request_id,
                               const FrontendActionResult& result,
                               std::chrono::system_clock::time_point now);
  void record_action_physical_result(
      FrontendActionRequestId request_id, CompatibilityPhysicalResult result,
      std::string note, std::chrono::system_clock::time_point now);
  void record_marker(std::string name, std::chrono::system_clock::time_point now);
  void record_note(std::string text, std::chrono::system_clock::time_point now);
  void upsert_guided_test(CompatibilityGuidedTestRecord record);

  [[nodiscard]] std::vector<std::string> recent_timeline_lines(
      std::size_t limit = kRecentTimelineLimit) const;
  [[nodiscard]] CompatibilityCaptureExport build_export(
      std::string_view label = {}) const;

 private:
  struct TimelineEntry {
    std::uint64_t sequence{};
    std::chrono::system_clock::time_point timestamp;
    std::string line;
  };

  [[nodiscard]] std::string default_basename(std::string_view label) const;
  [[nodiscard]] std::vector<TimelineEntry> timeline_entries() const;

  static std::string sanitize_note_like(std::string text);
  static std::string sanitize_label(std::string_view value);

  CompatibilityCaptureState state_{CompatibilityCaptureState::Idle};
  std::optional<CompatibilityCaptureApplicationInfo> application_;
  std::optional<FrontendStatus> initial_status_;
  std::optional<FrontendStatus> current_status_;
  std::optional<FrontendCapabilities> capabilities_;
  std::optional<FrontendConfigurationSnapshot> configuration_;
  std::optional<std::chrono::system_clock::time_point> started_at_;
  std::optional<std::chrono::system_clock::time_point> ended_at_;
  std::vector<FrontendCecEvent> cec_events_;
  std::vector<CompatibilityCaptureActionRecord> actions_;
  std::vector<CompatibilityCaptureStatusTransition> status_transitions_;
  std::vector<CompatibilityCaptureMarker> markers_;
  std::vector<CompatibilityCaptureNote> notes_;
  std::vector<CompatibilityGuidedTestRecord> guided_tests_;
  std::size_t dropped_cec_events_{};
  std::size_t received_cec_events_{};
  std::size_t transmitted_cec_events_{};
  std::map<std::string, std::size_t> opcode_frequency_;
  std::map<std::string, std::size_t> semantic_frequency_;
  std::uint64_t next_sequence_{1};
};

[[nodiscard]] std::string compatibility_capture_state_name(
    CompatibilityCaptureState state);
[[nodiscard]] std::string compatibility_physical_result_name(
    CompatibilityPhysicalResult result);
[[nodiscard]] std::string compatibility_guided_test_scenario_name(
    CompatibilityGuidedTestScenario scenario);
[[nodiscard]] std::string compatibility_guided_test_outcome_name(
    CompatibilityGuidedTestOutcome outcome);
[[nodiscard]] std::string compatibility_guided_observation_source_name(
    CompatibilityGuidedObservationSource source);

}  // namespace cec_injector::mate::frontend

#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "cec_injector/mate/frontend/compatibility_capture.h"

namespace cec_injector::mate::frontend {

enum class GuidedCompatibilityTestState {
  NotStarted,
  WaitingForUser,
  WaitingForObservation,
  WaitingForActionCompletion,
  WaitingForPhysicalConfirmation,
  Completed,
  Failed,
  Inconclusive,
  Cancelled,
};

struct GuidedCompatibilityTestButton {
  std::string label;
  bool enabled{true};
};

struct GuidedCompatibilityTestView {
  bool active{};
  CompatibilityGuidedTestScenario scenario{
      CompatibilityGuidedTestScenario::ActiveSource};
  GuidedCompatibilityTestState state{GuidedCompatibilityTestState::NotStarted};
  std::string title;
  std::string description;
  std::size_t step_index{};
  std::size_t step_count{};
  std::string instruction;
  std::string detail;
  std::optional<GuidedCompatibilityTestButton> primary;
  std::optional<GuidedCompatibilityTestButton> secondary;
  std::optional<GuidedCompatibilityTestButton> tertiary;
  std::optional<GuidedCompatibilityTestButton> back;
  std::optional<GuidedCompatibilityTestButton> cancel;
  bool show_physical_choices{};
  std::string physical_prompt;
};

struct GuidedCompatibilityTestEffect {
  bool start_capture{};
  std::optional<FrontendAction> action_to_send;
};

class GuidedCompatibilityTestSession final {
 public:
  static constexpr std::chrono::seconds kObservationTimeout{20};

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool capture_started_by_test() const noexcept {
    return capture_started_by_test_;
  }
  [[nodiscard]] GuidedCompatibilityTestView view() const;
  [[nodiscard]] const CompatibilityGuidedTestRecord* record() const noexcept {
    return active_ ? &record_ : nullptr;
  }

  bool start(CompatibilityGuidedTestScenario scenario,
             const FrontendStatus& status, bool capture_is_active,
             std::chrono::system_clock::time_point now);
  GuidedCompatibilityTestEffect choose_primary(
      std::chrono::system_clock::time_point now);
  GuidedCompatibilityTestEffect choose_secondary(
      std::chrono::system_clock::time_point now);
  GuidedCompatibilityTestEffect choose_tertiary(
      std::chrono::system_clock::time_point now);
  GuidedCompatibilityTestEffect choose_back(
      std::chrono::system_clock::time_point now);
  void cancel(std::chrono::system_clock::time_point now);
  void on_status(const FrontendStatus& status,
                 std::chrono::system_clock::time_point now);
  void on_action_request_accepted(FrontendActionRequestId request_id,
                                  std::chrono::system_clock::time_point now);
  void on_action_completed(FrontendActionRequestId request_id,
                           const FrontendActionResult& result,
                           std::chrono::system_clock::time_point now);
  void on_physical_confirmation(CompatibilityPhysicalResult result,
                                std::string note,
                                std::chrono::system_clock::time_point now);
  void tick(std::chrono::system_clock::time_point now);

 private:
  enum class Phase {
    Idle,
    ActiveSourceIntro,
    ActiveSourceSend,
    ActiveSourceWaitAction,
    ActiveSourceAskPhysical,
    StandbyPowerIntro,
    StandbySend,
    StandbyWaitAction,
    StandbyAskPhysical,
    StandbyOverrideDecision,
    PowerOnSend,
    PowerOnWaitAction,
    PowerOnAskPhysical,
    UsbReconnectIntro,
    UsbReconnectWaitLoss,
    UsbReconnectWaitReturn,
    HdmiReconnectIntro,
    HdmiReconnectWaitLoss,
    HdmiReconnectWaitReturn,
    ObservationTimeoutDecision,
    BackendInterruptedDecision,
    Finished,
  };

  void begin_step(std::size_t index, std::string title, std::string instruction,
                  GuidedCompatibilityTestState state,
                  std::chrono::system_clock::time_point now);
  void complete_step(std::chrono::system_clock::time_point now);
  void record_observation(CompatibilityGuidedObservationSource source,
                          std::string kind, std::string detail,
                          std::chrono::system_clock::time_point now,
                          std::optional<FrontendActionRequestId> request_id =
                              std::nullopt);
  void set_wait_deadline(std::chrono::system_clock::time_point now);
  void clear_wait_deadline();
  void finish(CompatibilityGuidedTestOutcome outcome, std::string reason,
              std::chrono::system_clock::time_point now);
  void enter_interrupted(std::chrono::system_clock::time_point now);
  [[nodiscard]] bool session_ready() const;
  [[nodiscard]] bool has_display_loss(const FrontendStatus& status) const;
  [[nodiscard]] bool has_display_return(const FrontendStatus& status) const;
  [[nodiscard]] bool has_usb_loss(const FrontendStatus& status) const;
  [[nodiscard]] bool has_usb_return(const FrontendStatus& status) const;
  void remember_action(FrontendActionRequestId request_id);
  [[nodiscard]] std::string scenario_title() const;
  [[nodiscard]] std::string scenario_description() const;

  bool active_{};
  bool capture_started_by_test_{};
  bool action_sent_{};
  CompatibilityGuidedTestScenario scenario_{
      CompatibilityGuidedTestScenario::ActiveSource};
  GuidedCompatibilityTestState state_{GuidedCompatibilityTestState::NotStarted};
  Phase phase_{Phase::Idle};
  Phase resume_phase_{Phase::Idle};
  std::size_t current_step_index_{};
  std::size_t total_steps_{};
  std::optional<std::chrono::system_clock::time_point> wait_deadline_;
  std::optional<FrontendActionRequestId> pending_action_request_;
  std::optional<FrontendStatus> initial_status_;
  FrontendStatus latest_status_{};
  CompatibilityGuidedTestRecord record_;
};

[[nodiscard]] std::string guided_compatibility_test_state_name(
    GuidedCompatibilityTestState state);

}  // namespace cec_injector::mate::frontend

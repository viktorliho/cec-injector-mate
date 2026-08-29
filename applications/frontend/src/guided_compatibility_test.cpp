#include "cec_injector/mate/frontend/guided_compatibility_test.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace cec_injector::mate::frontend {
namespace {

using TimePoint = std::chrono::system_clock::time_point;

std::string ActionName(FrontendAction action) {
  switch (action) {
    case FrontendAction::PowerOnTv: return "PowerOnTv";
    case FrontendAction::MakeActiveSource: return "MakeActiveSource";
    case FrontendAction::StandbyTv: return "StandbyTv";
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

std::string PhysicalAddressStatusName(PhysicalAddressStatus status) {
  switch (status) {
    case PhysicalAddressStatus::Available: return "Available";
    case PhysicalAddressStatus::Unavailable: return "Unavailable";
    case PhysicalAddressStatus::Ambiguous: return "Ambiguous";
  }
  return "Unknown";
}

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

std::string StatusSummary(const FrontendStatus& status) {
  std::ostringstream out;
  out << "Device=" << DeviceName(status.selected_device)
      << ", Session=" << SessionName(status.session_state)
      << ", PA=" << PhysicalAddressStatusName(status.physical_address_status);
  if (status.physical_address_status == PhysicalAddressStatus::Available) {
    out << " " << PhysicalAddressName(status.physical_address);
  }
  if (status.logical_address) {
    out << ", Logical=" << static_cast<unsigned>(*status.logical_address);
  }
  return out.str();
}

std::string NextGuidedTestId(CompatibilityGuidedTestScenario scenario,
                             TimePoint now) {
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch())
                          .count();
  return compatibility_guided_test_scenario_name(scenario) + "-" +
         std::to_string(millis);
}

}  // namespace

std::string guided_compatibility_test_state_name(
    GuidedCompatibilityTestState state) {
  switch (state) {
    case GuidedCompatibilityTestState::NotStarted: return "NotStarted";
    case GuidedCompatibilityTestState::WaitingForUser: return "WaitingForUser";
    case GuidedCompatibilityTestState::WaitingForObservation:
      return "WaitingForObservation";
    case GuidedCompatibilityTestState::WaitingForActionCompletion:
      return "WaitingForActionCompletion";
    case GuidedCompatibilityTestState::WaitingForPhysicalConfirmation:
      return "WaitingForPhysicalConfirmation";
    case GuidedCompatibilityTestState::Completed: return "Completed";
    case GuidedCompatibilityTestState::Failed: return "Failed";
    case GuidedCompatibilityTestState::Inconclusive: return "Inconclusive";
    case GuidedCompatibilityTestState::Cancelled: return "Cancelled";
  }
  return "Unknown";
}

GuidedCompatibilityTestView GuidedCompatibilityTestSession::view() const {
  GuidedCompatibilityTestView view;
  if (!active_) return view;

  view.active = true;
  view.scenario = scenario_;
  view.state = state_;
  view.title = scenario_title();
  view.description = scenario_description();
  view.step_index = current_step_index_;
  view.step_count = total_steps_;
  view.cancel = GuidedCompatibilityTestButton{"Cancel", true};

  switch (phase_) {
    case Phase::ActiveSourceIntro:
      view.instruction =
          "Switch the TV to another HDMI/input so the MacBook/Injector input "
          "is not currently active, then continue.";
      view.detail = "No CEC command will be sent until you press the explicit "
                    "send button in the next step.";
      view.primary = GuidedCompatibilityTestButton{"Continue", true};
      break;
    case Phase::ActiveSourceSend:
      view.instruction =
          "Mate is ready to send Make Active Source once.";
      view.detail = StatusSummary(latest_status_);
      view.primary =
          GuidedCompatibilityTestButton{"Send Make Active Source", session_ready()};
      view.back = GuidedCompatibilityTestButton{"Back", !action_sent_};
      break;
    case Phase::ActiveSourceWaitAction:
      view.instruction = "Waiting for Make Active Source completion.";
      view.detail = pending_action_request_
                        ? ("Request #" + std::to_string(*pending_action_request_))
                        : std::string{"Awaiting typed backend result"};
      break;
    case Phase::ActiveSourceAskPhysical:
      view.instruction =
          "Did the TV switch to the MacBook / Injector HDMI input?";
      view.detail = "Choose the observed physical result. Backend and physical "
                    "results are recorded separately.";
      view.show_physical_choices = true;
      view.physical_prompt = "Physical switch result";
      break;
    case Phase::StandbyPowerIntro:
      view.instruction =
          "This guided test will check Standby and then Power On. Each CEC "
          "command requires a separate explicit click.";
      view.detail = StatusSummary(latest_status_);
      view.primary = GuidedCompatibilityTestButton{"Continue", true};
      break;
    case Phase::StandbySend:
      view.instruction = "Mate is ready to send Standby once.";
      view.detail = StatusSummary(latest_status_);
      view.primary = GuidedCompatibilityTestButton{"Send Standby", session_ready()};
      view.back = GuidedCompatibilityTestButton{"Back", !action_sent_};
      break;
    case Phase::StandbyWaitAction:
      view.instruction = "Waiting for Standby completion.";
      break;
    case Phase::StandbyAskPhysical:
      view.instruction = "Did the TV enter Standby?";
      view.detail = "If the TV did not enter Standby, the test can end or "
                    "continue with a user override.";
      view.show_physical_choices = true;
      view.physical_prompt = "Standby physical result";
      break;
    case Phase::StandbyOverrideDecision:
      view.instruction =
          "The TV was not confirmed in Standby. Choose whether to continue to "
          "Power On anyway or end this guided test as inconclusive.";
      view.primary = GuidedCompatibilityTestButton{"End Inconclusive", true};
      view.secondary = GuidedCompatibilityTestButton{"Continue Anyway", true};
      break;
    case Phase::PowerOnSend:
      view.instruction = "Mate is ready to send Power On once.";
      view.detail = StatusSummary(latest_status_);
      view.primary = GuidedCompatibilityTestButton{"Send Power On", session_ready()};
      break;
    case Phase::PowerOnWaitAction:
      view.instruction = "Waiting for Power On completion.";
      break;
    case Phase::PowerOnAskPhysical:
      view.instruction = "Did the TV power on?";
      view.show_physical_choices = true;
      view.physical_prompt = "Power On physical result";
      break;
    case Phase::UsbReconnectIntro:
      view.instruction =
          "Unplug the Injector USB cable from the MacBook, then press I'm ready.";
      view.detail = "No CEC commands will be sent during this guided test.";
      view.primary = GuidedCompatibilityTestButton{"I'm ready", true};
      break;
    case Phase::UsbReconnectWaitLoss:
      view.instruction = "Waiting for Injector loss observation.";
      view.detail =
          "Expected: device absent and/or session disconnected. This is based "
          "on typed frontend status transitions.";
      break;
    case Phase::UsbReconnectWaitReturn:
      view.instruction =
          "Reconnect the Injector USB cable. Waiting for device/session return.";
      view.detail = "Expected: device present, same DeviceId if possible, and "
                    "healthy session recovery.";
      break;
    case Phase::HdmiReconnectIntro:
      view.instruction =
          "Disconnect the MacBook display/HDMI path to the TV while leaving "
          "the Injector USB connection present, if your setup allows this.";
      view.primary = GuidedCompatibilityTestButton{"I'm ready", true};
      view.tertiary =
          GuidedCompatibilityTestButton{"Cannot perform this test", true};
      break;
    case Phase::HdmiReconnectWaitLoss:
      view.instruction =
          "Waiting for display topology evidence such as PA loss, ambiguity, "
          "or EDID/display change.";
      break;
    case Phase::HdmiReconnectWaitReturn:
      view.instruction =
          "Reconnect the display/HDMI path. Waiting for display evidence and "
          "PA recovery.";
      break;
    case Phase::ObservationTimeoutDecision:
      view.instruction =
          "Expected observation did not arrive within the time window.";
      view.primary = GuidedCompatibilityTestButton{"Retry Observation", true};
      view.secondary =
          GuidedCompatibilityTestButton{"Continue as Inconclusive", true};
      break;
    case Phase::BackendInterruptedDecision:
      view.instruction =
          "Backend/service became unavailable during the guided test.";
      view.primary = GuidedCompatibilityTestButton{
          "Resume Observation", latest_status_.service_available};
      view.secondary =
          GuidedCompatibilityTestButton{"End Inconclusive", true};
      break;
    case Phase::Finished:
      view.instruction =
          "Guided test finished. Review the collected evidence or start "
          "another scenario.";
      break;
    case Phase::Idle: break;
  }

  return view;
}

bool GuidedCompatibilityTestSession::start(
    CompatibilityGuidedTestScenario scenario, const FrontendStatus& status,
    bool capture_is_active, TimePoint now) {
  if (active_) return false;
  active_ = true;
  capture_started_by_test_ = !capture_is_active;
  action_sent_ = false;
  scenario_ = scenario;
  initial_status_ = status;
  latest_status_ = status;
  pending_action_request_.reset();
  wait_deadline_.reset();
  resume_phase_ = Phase::Idle;
  record_ = {};
  record_.test_id = NextGuidedTestId(scenario, now);
  record_.scenario = scenario;
  record_.display_name = status.display_name;
  record_.started_at = now;
  record_.outcome = CompatibilityGuidedTestOutcome::Inconclusive;
  total_steps_ = scenario == CompatibilityGuidedTestScenario::StandbyPowerOn ? 5
               : scenario == CompatibilityGuidedTestScenario::ActiveSource   ? 3
               : 3;
  record_observation(CompatibilityGuidedObservationSource::Automatic, "Start",
                     StatusSummary(status), now);

  switch (scenario_) {
    case CompatibilityGuidedTestScenario::ActiveSource:
      phase_ = Phase::ActiveSourceIntro;
      begin_step(1, "Prepare TV input", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
    case CompatibilityGuidedTestScenario::StandbyPowerOn:
      phase_ = Phase::StandbyPowerIntro;
      begin_step(1, "Prepare power cycle", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
    case CompatibilityGuidedTestScenario::UsbReconnect:
      phase_ = Phase::UsbReconnectIntro;
      begin_step(1, "Disconnect USB", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
    case CompatibilityGuidedTestScenario::HdmiReconnect:
      phase_ = Phase::HdmiReconnectIntro;
      begin_step(1, "Disconnect display path", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
  }
  return true;
}

GuidedCompatibilityTestEffect GuidedCompatibilityTestSession::choose_primary(
    TimePoint now) {
  GuidedCompatibilityTestEffect effect;
  if (!active_) return effect;

  switch (phase_) {
    case Phase::ActiveSourceIntro:
      complete_step(now);
      phase_ = Phase::ActiveSourceSend;
      begin_step(2, "Send Make Active Source", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      record_observation(CompatibilityGuidedObservationSource::User, "Step",
                         "User confirmed the TV is on another input", now);
      break;
    case Phase::ActiveSourceSend:
      if (!session_ready()) {
        finish(CompatibilityGuidedTestOutcome::Inconclusive,
               "Production session is not connected", now);
        break;
      }
      action_sent_ = true;
      state_ = GuidedCompatibilityTestState::WaitingForActionCompletion;
      if (!record_.steps.empty()) {
        record_.steps.back().state =
            guided_compatibility_test_state_name(state_);
      }
      phase_ = Phase::ActiveSourceWaitAction;
      effect.action_to_send = FrontendAction::MakeActiveSource;
      record_observation(CompatibilityGuidedObservationSource::User, "Send",
                         "Requested MakeActiveSource", now);
      break;
    case Phase::StandbyPowerIntro:
      complete_step(now);
      phase_ = Phase::StandbySend;
      begin_step(2, "Send Standby", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
    case Phase::StandbySend:
      if (!session_ready()) {
        finish(CompatibilityGuidedTestOutcome::Inconclusive,
               "Production session is not connected", now);
        break;
      }
      action_sent_ = true;
      state_ = GuidedCompatibilityTestState::WaitingForActionCompletion;
      if (!record_.steps.empty()) {
        record_.steps.back().state =
            guided_compatibility_test_state_name(state_);
      }
      phase_ = Phase::StandbyWaitAction;
      effect.action_to_send = FrontendAction::StandbyTv;
      record_observation(CompatibilityGuidedObservationSource::User, "Send",
                         "Requested StandbyTv", now);
      break;
    case Phase::StandbyOverrideDecision:
      finish(CompatibilityGuidedTestOutcome::Inconclusive,
             "Power On was not attempted after failed physical standby "
             "confirmation",
             now);
      break;
    case Phase::PowerOnSend:
      if (!session_ready()) {
        finish(CompatibilityGuidedTestOutcome::Inconclusive,
               "Production session is not connected", now);
        break;
      }
      state_ = GuidedCompatibilityTestState::WaitingForActionCompletion;
      if (!record_.steps.empty()) {
        record_.steps.back().state =
            guided_compatibility_test_state_name(state_);
      }
      phase_ = Phase::PowerOnWaitAction;
      effect.action_to_send = FrontendAction::PowerOnTv;
      record_observation(CompatibilityGuidedObservationSource::User, "Send",
                         "Requested PowerOnTv", now);
      break;
    case Phase::UsbReconnectIntro:
      complete_step(now);
      phase_ = Phase::UsbReconnectWaitLoss;
      begin_step(2, "Observe USB loss", view().instruction,
                 GuidedCompatibilityTestState::WaitingForObservation, now);
      set_wait_deadline(now);
      record_observation(CompatibilityGuidedObservationSource::User, "Step",
                         "User started USB disconnect observation", now);
      break;
    case Phase::HdmiReconnectIntro:
      complete_step(now);
      phase_ = Phase::HdmiReconnectWaitLoss;
      begin_step(2, "Observe display loss", view().instruction,
                 GuidedCompatibilityTestState::WaitingForObservation, now);
      set_wait_deadline(now);
      record_observation(CompatibilityGuidedObservationSource::User, "Step",
                         "User started HDMI/display disconnect observation",
                         now);
      break;
    case Phase::ObservationTimeoutDecision:
      phase_ = resume_phase_;
      state_ = GuidedCompatibilityTestState::WaitingForObservation;
      if (!record_.steps.empty()) {
        record_.steps.back().state =
            guided_compatibility_test_state_name(state_);
      }
      set_wait_deadline(now);
      record_observation(CompatibilityGuidedObservationSource::User, "Retry",
                         "Retry observation after timeout", now);
      break;
    case Phase::BackendInterruptedDecision:
      if (!latest_status_.service_available) break;
      phase_ = resume_phase_;
      state_ = phase_ == Phase::ActiveSourceWaitAction ||
                       phase_ == Phase::StandbyWaitAction ||
                       phase_ == Phase::PowerOnWaitAction
                   ? GuidedCompatibilityTestState::WaitingForActionCompletion
                   : GuidedCompatibilityTestState::WaitingForObservation;
      if (!record_.steps.empty()) {
        record_.steps.back().state =
            guided_compatibility_test_state_name(state_);
      }
      record_observation(CompatibilityGuidedObservationSource::User, "Resume",
                         "Resumed after backend recovery", now);
      break;
    case Phase::Finished:
    case Phase::ActiveSourceWaitAction:
    case Phase::ActiveSourceAskPhysical:
    case Phase::StandbyWaitAction:
    case Phase::StandbyAskPhysical:
    case Phase::PowerOnWaitAction:
    case Phase::PowerOnAskPhysical:
    case Phase::UsbReconnectWaitLoss:
    case Phase::UsbReconnectWaitReturn:
    case Phase::HdmiReconnectWaitLoss:
    case Phase::HdmiReconnectWaitReturn:
    case Phase::Idle:
      break;
  }
  return effect;
}

GuidedCompatibilityTestEffect GuidedCompatibilityTestSession::choose_secondary(
    TimePoint now) {
  GuidedCompatibilityTestEffect effect;
  if (!active_) return effect;
  switch (phase_) {
    case Phase::StandbyOverrideDecision:
      complete_step(now);
      phase_ = Phase::PowerOnSend;
      begin_step(4, "Send Power On", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      record_observation(CompatibilityGuidedObservationSource::User, "Override",
                         "User continued to Power On without confirmed standby",
                         now);
      break;
    case Phase::ObservationTimeoutDecision:
      finish(CompatibilityGuidedTestOutcome::Inconclusive,
             "Expected observation was not seen before timeout", now);
      break;
    case Phase::BackendInterruptedDecision:
      finish(CompatibilityGuidedTestOutcome::Inconclusive,
             "Backend became unavailable during the guided test", now);
      break;
    default: break;
  }
  return effect;
}

GuidedCompatibilityTestEffect GuidedCompatibilityTestSession::choose_tertiary(
    TimePoint now) {
  GuidedCompatibilityTestEffect effect;
  if (!active_) return effect;
  if (phase_ == Phase::HdmiReconnectIntro) {
    finish(CompatibilityGuidedTestOutcome::Inconclusive,
           "UnsupportedBySetup: display path cannot be disconnected "
           "independently from USB",
           now);
  }
  return effect;
}

GuidedCompatibilityTestEffect GuidedCompatibilityTestSession::choose_back(
    TimePoint now) {
  GuidedCompatibilityTestEffect effect;
  if (!active_ || action_sent_) return effect;

  switch (phase_) {
    case Phase::ActiveSourceSend:
      complete_step(now);
      phase_ = Phase::ActiveSourceIntro;
      begin_step(1, "Prepare TV input", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
    case Phase::StandbySend:
      complete_step(now);
      phase_ = Phase::StandbyPowerIntro;
      begin_step(1, "Prepare power cycle", view().instruction,
                 GuidedCompatibilityTestState::WaitingForUser, now);
      break;
    default: break;
  }
  return effect;
}

void GuidedCompatibilityTestSession::cancel(TimePoint now) {
  if (!active_) return;
  finish(CompatibilityGuidedTestOutcome::Cancelled,
         "User cancelled the guided test", now);
}

void GuidedCompatibilityTestSession::on_status(const FrontendStatus& status,
                                               TimePoint now) {
  if (!active_) return;
  latest_status_ = status;
  if ((phase_ == Phase::UsbReconnectWaitLoss ||
       phase_ == Phase::UsbReconnectWaitReturn ||
       phase_ == Phase::HdmiReconnectWaitLoss ||
       phase_ == Phase::HdmiReconnectWaitReturn ||
       phase_ == Phase::ActiveSourceWaitAction ||
       phase_ == Phase::StandbyWaitAction ||
       phase_ == Phase::PowerOnWaitAction) &&
      !status.service_available) {
    enter_interrupted(now);
    return;
  }

  switch (phase_) {
    case Phase::UsbReconnectWaitLoss:
      if (!has_usb_loss(status)) break;
      complete_step(now);
      record_observation(CompatibilityGuidedObservationSource::Automatic,
                         "Observation", "USB device/session loss observed", now);
      phase_ = Phase::UsbReconnectWaitReturn;
      begin_step(3, "Observe USB return", view().instruction,
                 GuidedCompatibilityTestState::WaitingForObservation, now);
      set_wait_deadline(now);
      break;
    case Phase::UsbReconnectWaitReturn:
      if (!has_usb_return(status)) break;
      complete_step(now);
      {
        const auto same_device =
            initial_status_ && initial_status_->selected_device == status.selected_device;
        const auto pa_recovered =
            status.physical_address_status == PhysicalAddressStatus::Available;
        std::ostringstream detail;
        detail << "USB return observed; same_device="
               << (same_device ? "yes" : "no")
               << "; session=" << SessionName(status.session_state)
               << "; pa_recovered=" << (pa_recovered ? "yes" : "no");
        record_observation(CompatibilityGuidedObservationSource::Automatic,
                           "Observation", detail.str(), now);
      }
      finish(CompatibilityGuidedTestOutcome::Completed,
             "USB reconnect evidence collected", now);
      break;
    case Phase::HdmiReconnectWaitLoss:
      if (!has_display_loss(status)) break;
      complete_step(now);
      record_observation(CompatibilityGuidedObservationSource::Automatic,
                         "Observation", "Display topology loss observed", now);
      phase_ = Phase::HdmiReconnectWaitReturn;
      begin_step(3, "Observe display return", view().instruction,
                 GuidedCompatibilityTestState::WaitingForObservation, now);
      set_wait_deadline(now);
      break;
    case Phase::HdmiReconnectWaitReturn:
      if (!has_display_return(status)) break;
      complete_step(now);
      {
        std::ostringstream detail;
        detail << "Display return observed; session="
               << SessionName(status.session_state)
               << "; pa_status="
               << PhysicalAddressStatusName(status.physical_address_status)
               << "; pa=" << PhysicalAddressName(status.physical_address);
        record_observation(CompatibilityGuidedObservationSource::Automatic,
                           "Observation", detail.str(), now);
      }
      finish(CompatibilityGuidedTestOutcome::Completed,
             "Display reconnection evidence collected", now);
      break;
    case Phase::BackendInterruptedDecision:
    case Phase::ObservationTimeoutDecision:
    case Phase::ActiveSourceIntro:
    case Phase::ActiveSourceSend:
    case Phase::ActiveSourceWaitAction:
    case Phase::ActiveSourceAskPhysical:
    case Phase::StandbyPowerIntro:
    case Phase::StandbySend:
    case Phase::StandbyWaitAction:
    case Phase::StandbyAskPhysical:
    case Phase::StandbyOverrideDecision:
    case Phase::PowerOnSend:
    case Phase::PowerOnWaitAction:
    case Phase::PowerOnAskPhysical:
    case Phase::UsbReconnectIntro:
    case Phase::HdmiReconnectIntro:
    case Phase::Finished:
    case Phase::Idle:
      break;
  }
}

void GuidedCompatibilityTestSession::on_action_completed(
    FrontendActionRequestId request_id, const FrontendActionResult& result,
    TimePoint now) {
  if (!active_ || !pending_action_request_ || *pending_action_request_ != request_id) {
    return;
  }
  pending_action_request_.reset();
  if (record_.steps.empty()) return;
  auto& step = record_.steps.back();
  step.action_completion = result;
  step.completed_at = now;
  record_observation(CompatibilityGuidedObservationSource::Automatic,
                     "ActionCompletion",
                     ActionName(result.action) + " -> " +
                         OperationResultName(result.result),
                     now, request_id);

  switch (phase_) {
    case Phase::ActiveSourceWaitAction:
      phase_ = Phase::ActiveSourceAskPhysical;
      begin_step(3, "Confirm physical switch", view().instruction,
                 GuidedCompatibilityTestState::WaitingForPhysicalConfirmation,
                 now);
      record_.steps.back().action_request_id = request_id;
      record_.steps.back().action_completion = result;
      break;
    case Phase::StandbyWaitAction:
      phase_ = Phase::StandbyAskPhysical;
      begin_step(3, "Confirm standby", view().instruction,
                 GuidedCompatibilityTestState::WaitingForPhysicalConfirmation,
                 now);
      record_.steps.back().action_request_id = request_id;
      record_.steps.back().action_completion = result;
      break;
    case Phase::PowerOnWaitAction:
      phase_ = Phase::PowerOnAskPhysical;
      begin_step(5, "Confirm power on", view().instruction,
                 GuidedCompatibilityTestState::WaitingForPhysicalConfirmation,
                 now);
      record_.steps.back().action_request_id = request_id;
      record_.steps.back().action_completion = result;
      break;
    default: break;
  }
}

void GuidedCompatibilityTestSession::on_action_request_accepted(
    FrontendActionRequestId request_id, TimePoint now) {
  if (!active_) return;
  remember_action(request_id);
  if (request_id != 0) return;
  record_observation(CompatibilityGuidedObservationSource::Automatic,
                     "RequestRejected",
                     "Backend rejected guided test action request", now);
  finish(CompatibilityGuidedTestOutcome::Failed,
         "Backend rejected the explicit guided test action request", now);
}

void GuidedCompatibilityTestSession::on_physical_confirmation(
    CompatibilityPhysicalResult result, std::string note, TimePoint now) {
  if (!active_ || record_.steps.empty()) return;
  auto& step = record_.steps.back();
  if (phase_ != Phase::ActiveSourceAskPhysical &&
      phase_ != Phase::StandbyAskPhysical && phase_ != Phase::PowerOnAskPhysical) {
    return;
  }
  step.physical_result = result;
  step.physical_note = std::move(note);
  step.completed_at = now;
  if (step.action_request_id) {
    record_.physical_confirmations.push_back(
        {.timestamp = now,
         .request_id = *step.action_request_id,
         .result = result,
         .note = step.physical_note});
  }
  record_observation(CompatibilityGuidedObservationSource::User,
                     "PhysicalConfirmation",
                     compatibility_physical_result_name(result), now,
                     step.action_request_id);

  switch (phase_) {
    case Phase::ActiveSourceAskPhysical:
      finish(CompatibilityGuidedTestOutcome::Completed,
             "Active Source evidence collected", now);
      break;
    case Phase::StandbyAskPhysical:
      if (result == CompatibilityPhysicalResult::Worked) {
        phase_ = Phase::PowerOnSend;
        begin_step(4, "Send Power On", view().instruction,
                   GuidedCompatibilityTestState::WaitingForUser, now);
      } else {
        phase_ = Phase::StandbyOverrideDecision;
        begin_step(4, "Decide how to continue", view().instruction,
                   GuidedCompatibilityTestState::WaitingForUser, now);
      }
      break;
    case Phase::PowerOnAskPhysical:
      finish(CompatibilityGuidedTestOutcome::Completed,
             "Standby/Power On evidence collected", now);
      break;
    default: break;
  }
}

void GuidedCompatibilityTestSession::tick(TimePoint now) {
  if (!active_ || !wait_deadline_ || now < *wait_deadline_) return;
  if (phase_ != Phase::UsbReconnectWaitLoss &&
      phase_ != Phase::UsbReconnectWaitReturn &&
      phase_ != Phase::HdmiReconnectWaitLoss &&
      phase_ != Phase::HdmiReconnectWaitReturn) {
    return;
  }
  resume_phase_ = phase_;
  phase_ = Phase::ObservationTimeoutDecision;
  state_ = GuidedCompatibilityTestState::WaitingForUser;
  if (!record_.steps.empty()) {
    record_.steps.back().state = guided_compatibility_test_state_name(state_);
  }
  clear_wait_deadline();
  record_observation(CompatibilityGuidedObservationSource::Automatic, "Timeout",
                     "Observation window elapsed", now);
}

void GuidedCompatibilityTestSession::begin_step(
    std::size_t index, std::string title, std::string instruction,
    GuidedCompatibilityTestState state, TimePoint now) {
  current_step_index_ = index;
  state_ = state;
  record_.steps.push_back({.index = index,
                           .title = std::move(title),
                           .instruction = std::move(instruction),
                           .state = guided_compatibility_test_state_name(state),
                           .started_at = now});
}

void GuidedCompatibilityTestSession::complete_step(TimePoint now) {
  if (record_.steps.empty() || record_.steps.back().completed_at) return;
  record_.steps.back().completed_at = now;
}

void GuidedCompatibilityTestSession::record_observation(
    CompatibilityGuidedObservationSource source, std::string kind,
    std::string detail, TimePoint now,
    std::optional<FrontendActionRequestId> request_id) {
  record_.observations.push_back({.timestamp = now,
                                  .source = source,
                                  .kind = std::move(kind),
                                  .detail = std::move(detail),
                                  .request_id = request_id});
}

void GuidedCompatibilityTestSession::set_wait_deadline(TimePoint now) {
  wait_deadline_ = now + kObservationTimeout;
}

void GuidedCompatibilityTestSession::clear_wait_deadline() {
  wait_deadline_.reset();
}

void GuidedCompatibilityTestSession::finish(CompatibilityGuidedTestOutcome outcome,
                                            std::string reason, TimePoint now) {
  clear_wait_deadline();
  complete_step(now);
  record_.completed_at = now;
  record_.outcome = outcome;
  record_.reason = std::move(reason);
  phase_ = Phase::Finished;
  state_ = outcome == CompatibilityGuidedTestOutcome::Completed
               ? GuidedCompatibilityTestState::Completed
           : outcome == CompatibilityGuidedTestOutcome::Failed
               ? GuidedCompatibilityTestState::Failed
           : outcome == CompatibilityGuidedTestOutcome::Cancelled
               ? GuidedCompatibilityTestState::Cancelled
               : GuidedCompatibilityTestState::Inconclusive;
}

void GuidedCompatibilityTestSession::enter_interrupted(TimePoint now) {
  if (phase_ == Phase::BackendInterruptedDecision) return;
  resume_phase_ = phase_;
  phase_ = Phase::BackendInterruptedDecision;
  state_ = GuidedCompatibilityTestState::WaitingForUser;
  if (!record_.steps.empty()) {
    record_.steps.back().state = guided_compatibility_test_state_name(state_);
  }
  record_observation(CompatibilityGuidedObservationSource::Automatic,
                     "Interruption", "Backend unavailable", now);
}

bool GuidedCompatibilityTestSession::session_ready() const {
  return latest_status_.service_available && latest_status_.injector_connected &&
         latest_status_.selected_device.has_value() &&
         latest_status_.session_state == SessionState::Connected &&
         latest_status_.physical_address_status == PhysicalAddressStatus::Available;
}

bool GuidedCompatibilityTestSession::has_display_loss(
    const FrontendStatus& status) const {
  if (!initial_status_) return false;
  return status.physical_address_status != PhysicalAddressStatus::Available ||
         !status.edid_available || status.display_name != initial_status_->display_name;
}

bool GuidedCompatibilityTestSession::has_display_return(
    const FrontendStatus& status) const {
  return status.edid_available &&
         status.physical_address_status == PhysicalAddressStatus::Available;
}

bool GuidedCompatibilityTestSession::has_usb_loss(
    const FrontendStatus& status) const {
  return !status.injector_connected || !status.selected_device ||
         status.session_state == SessionState::Disconnected;
}

bool GuidedCompatibilityTestSession::has_usb_return(
    const FrontendStatus& status) const {
  return status.injector_connected && status.selected_device &&
         status.session_state == SessionState::Connected;
}

void GuidedCompatibilityTestSession::remember_action(
    FrontendActionRequestId request_id) {
  pending_action_request_ = request_id;
  if (request_id == 0) return;
  if (std::find(record_.related_action_request_ids.begin(),
                record_.related_action_request_ids.end(),
                request_id) == record_.related_action_request_ids.end()) {
    record_.related_action_request_ids.push_back(request_id);
  }
  if (!record_.steps.empty()) record_.steps.back().action_request_id = request_id;
}

std::string GuidedCompatibilityTestSession::scenario_title() const {
  return compatibility_guided_test_scenario_name(scenario_);
}

std::string GuidedCompatibilityTestSession::scenario_description() const {
  switch (scenario_) {
    case CompatibilityGuidedTestScenario::ActiveSource:
      return "Guided Make Active Source compatibility check.";
    case CompatibilityGuidedTestScenario::StandbyPowerOn:
      return "Guided Standby followed by Power On compatibility check.";
    case CompatibilityGuidedTestScenario::UsbReconnect:
      return "Observation-only USB disconnect/reconnect recovery check.";
    case CompatibilityGuidedTestScenario::HdmiReconnect:
      return "Observation-only display topology / PA recovery check.";
  }
  return {};
}

}  // namespace cec_injector::mate::frontend

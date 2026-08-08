#include "cec_injector/mate/policy.h"

namespace cec_injector::mate {
namespace {

void AddWakeAndActivate(PolicyPlan& plan) {
  const auto power = plan.steps.size();
  plan.steps.push_back({PolicyIntent::PowerOnTv, std::nullopt});
  plan.steps.push_back({PolicyIntent::MakeActiveSource, power});
}

}  // namespace

PolicyPlan PolicyStateMachine::handle(LifecycleEvent event) {
  PolicyPlan plan{event, {}};
  switch (event) {
    case LifecycleEvent::BootRequested:
      if (configuration_.power_on_boot) AddWakeAndActivate(plan);
      break;

    case LifecycleEvent::ExternalTvStandby:
      if (configuration_.suspend_on_tv_standby &&
          state_ == LifecycleState::Running) {
        state_ = LifecycleState::SuspendRequested;
        plan.steps.push_back({PolicyIntent::RequestHostSuspend, std::nullopt});
      }
      break;

    case LifecycleEvent::HostPreparingSuspend:
      state_ = LifecycleState::PreparingSuspend;
      plan.steps.push_back({PolicyIntent::ResetRemoteInput, std::nullopt});
      if (configuration_.wake_host_on_tv_source_request) {
        plan.steps.push_back({PolicyIntent::PrepareWake, std::nullopt});
      }
      if (configuration_.standby_on_suspend) {
        plan.steps.push_back({PolicyIntent::StandbyTv, std::nullopt});
      }
      plan.steps.push_back({PolicyIntent::CompleteHostTransition, std::nullopt});
      break;

    case LifecycleEvent::HostResumed: {
      state_ = LifecycleState::Resuming;
      plan.steps.push_back({PolicyIntent::ResetRemoteInput, std::nullopt});
      const auto refresh = plan.steps.size();
      plan.steps.push_back({PolicyIntent::RefreshDeviceSession, std::nullopt});
      if (configuration_.power_on_resume) {
        const auto power = plan.steps.size();
        plan.steps.push_back({PolicyIntent::PowerOnTv, refresh});
        plan.steps.push_back({PolicyIntent::MakeActiveSource, power});
      }
      if (configuration_.wake_host_on_tv_source_request) {
        plan.steps.push_back({PolicyIntent::ReadWakeReason, refresh});
      }
      plan.steps.push_back({PolicyIntent::CompleteHostTransition, std::nullopt});
      break;
    }

    case LifecycleEvent::HostPreparingShutdown:
      state_ = LifecycleState::PreparingShutdown;
      plan.steps.push_back({PolicyIntent::ResetRemoteInput, std::nullopt});
      if (configuration_.standby_on_shutdown) {
        plan.steps.push_back({PolicyIntent::StandbyTv, std::nullopt});
      }
      plan.steps.push_back({PolicyIntent::CompleteHostTransition, std::nullopt});
      break;

    case LifecycleEvent::SessionConnected:
      session_connected_ = true;
      plan.steps.push_back({configuration_.wake_host_on_tv_source_request
                                ? PolicyIntent::PrepareWake
                                : PolicyIntent::DisarmWake,
                            std::nullopt});
      break;

    case LifecycleEvent::SessionLost:
      session_connected_ = false;
      plan.steps.push_back({PolicyIntent::ResetRemoteInput, std::nullopt});
      break;
  }
  return plan;
}

void PolicyStateMachine::record_result(PolicyIntent intent,
                                       OperationResult result) {
  if (intent == PolicyIntent::RequestHostSuspend &&
      state_ == LifecycleState::SuspendRequested &&
      result != OperationResult::Success) {
    state_ = LifecycleState::Running;
    return;
  }
  if (intent != PolicyIntent::CompleteHostTransition) return;
  if (state_ == LifecycleState::PreparingSuspend) {
    state_ = LifecycleState::Suspended;
  } else if (state_ == LifecycleState::Resuming) {
    state_ = LifecycleState::Running;
  }
}

}  // namespace cec_injector::mate

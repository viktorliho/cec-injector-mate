#include "cec_injector/mate/policy.h"
#include "cec_injector/mate/semantic_event_queue.h"

#include <map>

#include "test_support.h"

using namespace cec_injector::mate;

namespace {

std::vector<PolicyIntent> Intents(const PolicyPlan& plan) {
  std::vector<PolicyIntent> result;
  for (const auto& step : plan.steps) result.push_back(step.intent);
  return result;
}

class FakeExecutor {
 public:
  void run(PolicyStateMachine& policy, const PolicyPlan& plan) {
    std::vector<OperationResult> results;
    for (const auto& step : plan.steps) {
      if (step.requires_success &&
          results[*step.requires_success] != OperationResult::Success) {
        results.push_back(OperationResult::InvalidState);
        continue;
      }
      executed.push_back(step.intent);
      const auto found = configured_results.find(step.intent);
      const auto result = found == configured_results.end()
                              ? OperationResult::Success
                              : found->second;
      results.push_back(result);
      policy.record_result(step.intent, result);
    }
  }

  std::map<PolicyIntent, OperationResult> configured_results;
  std::vector<PolicyIntent> executed;
};

class FakeService {
 public:
  explicit FakeService(PolicyConfiguration configuration)
      : policy(configuration), events(8) {}

  EventSubmitResult submit(LifecycleEvent event) {
    return events.submit(event);
  }

  void drain() {
    while (const auto event = events.begin_next()) {
      executor.run(policy, policy.handle(*event));
      events.complete_active();
    }
  }

  PolicyStateMachine policy;
  SemanticEventQueue events;
  FakeExecutor executor;
};

PolicyConfiguration Enabled() {
  return {.power_on_boot = true,
          .power_on_resume = true,
          .standby_on_suspend = true,
          .standby_on_shutdown = true,
          .suspend_on_tv_standby = true,
          .wake_host_on_tv_source_request = true};
}

}  // namespace

int main() {
  FakeService service(Enabled());
  CHECK(service.executor.executed.empty());
  CHECK(service.submit(LifecycleEvent::SessionConnected) ==
        EventSubmitResult::Accepted);
  CHECK(service.submit(LifecycleEvent::BootRequested) ==
        EventSubmitResult::Accepted);
  service.drain();
  CHECK(service.executor.executed ==
        std::vector<PolicyIntent>({PolicyIntent::PrepareWake,
                                   PolicyIntent::PowerOnTv,
                                   PolicyIntent::MakeActiveSource}));
  CHECK(service.submit(LifecycleEvent::SessionLost) ==
        EventSubmitResult::Accepted);
  CHECK(service.submit(LifecycleEvent::SessionConnected) ==
        EventSubmitResult::Accepted);
  service.drain();
  CHECK(service.executor.executed ==
        std::vector<PolicyIntent>({PolicyIntent::PrepareWake,
                                   PolicyIntent::PowerOnTv,
                                   PolicyIntent::MakeActiveSource,
                                   PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::PrepareWake}));

  PolicyStateMachine safe_defaults;
  CHECK(safe_defaults.state() == LifecycleState::Running);
  CHECK(safe_defaults.handle(LifecycleEvent::BootRequested).steps.empty());

  PolicyStateMachine policy(Enabled());
  CHECK(Intents(policy.handle(LifecycleEvent::BootRequested)) ==
        std::vector<PolicyIntent>({PolicyIntent::PowerOnTv,
                                   PolicyIntent::MakeActiveSource}));

  FakeExecutor boot_failure;
  boot_failure.configured_results[PolicyIntent::PowerOnTv] =
      OperationResult::Rejected;
  boot_failure.run(policy, policy.handle(LifecycleEvent::BootRequested));
  CHECK(boot_failure.executed ==
        std::vector<PolicyIntent>({PolicyIntent::PowerOnTv}));

  auto standby = policy.handle(LifecycleEvent::ExternalTvStandby);
  CHECK(Intents(standby) ==
        std::vector<PolicyIntent>({PolicyIntent::RequestHostSuspend}));
  CHECK(policy.state() == LifecycleState::SuspendRequested);
  CHECK(policy.handle(LifecycleEvent::ExternalTvStandby).steps.empty());

  FakeExecutor suspend_request_failure;
  suspend_request_failure.configured_results[PolicyIntent::RequestHostSuspend] =
      OperationResult::PermissionDenied;
  suspend_request_failure.run(policy, standby);
  CHECK(policy.state() == LifecycleState::Running);

  const auto preparing = policy.handle(LifecycleEvent::HostPreparingSuspend);
  CHECK(Intents(preparing) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::PrepareWake,
                                   PolicyIntent::StandbyTv,
                                   PolicyIntent::CompleteHostTransition}));
  CHECK(policy.state() == LifecycleState::PreparingSuspend);
  CHECK(policy.handle(LifecycleEvent::ExternalTvStandby).steps.empty());
  FakeExecutor suspend;
  suspend.run(policy, preparing);
  CHECK(policy.state() == LifecycleState::Suspended);

  const auto resume_plan = policy.handle(LifecycleEvent::HostResumed);
  CHECK(Intents(resume_plan) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::RefreshDeviceSession,
                                   PolicyIntent::PowerOnTv,
                                   PolicyIntent::MakeActiveSource,
                                   PolicyIntent::ReadWakeReason,
                                   PolicyIntent::CompleteHostTransition}));
  FakeExecutor resume;
  resume.run(policy, resume_plan);
  CHECK(policy.state() == LifecycleState::Running);

  FakeExecutor refresh_failure;
  refresh_failure.configured_results[PolicyIntent::RefreshDeviceSession] =
      OperationResult::Disconnected;
  refresh_failure.run(policy, policy.handle(LifecycleEvent::HostResumed));
  CHECK(refresh_failure.executed ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::RefreshDeviceSession,
                                   PolicyIntent::CompleteHostTransition}));

  CHECK(Intents(policy.handle(LifecycleEvent::HostPreparingShutdown)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::StandbyTv,
                                   PolicyIntent::CompleteHostTransition}));

  const auto lost = policy.handle(LifecycleEvent::SessionLost);
  CHECK(Intents(lost) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput}));
  CHECK(!policy.session_connected());
  const auto connected = policy.handle(LifecycleEvent::SessionConnected);
  CHECK(Intents(connected) ==
        std::vector<PolicyIntent>({PolicyIntent::PrepareWake}));
  CHECK(policy.session_connected());

  PolicyStateMachine disabled;
  CHECK(disabled.handle(LifecycleEvent::ExternalTvStandby).steps.empty());
  CHECK(Intents(disabled.handle(LifecycleEvent::SessionConnected)) ==
        std::vector<PolicyIntent>({PolicyIntent::DisarmWake}));
  CHECK(Intents(disabled.handle(LifecycleEvent::HostPreparingSuspend)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::CompleteHostTransition}));
  CHECK(Intents(disabled.handle(LifecycleEvent::HostResumed)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::RefreshDeviceSession,
                                   PolicyIntent::CompleteHostTransition}));
  CHECK(Intents(disabled.handle(LifecycleEvent::HostPreparingShutdown)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::CompleteHostTransition}));

  PolicyConfiguration only_boot;
  only_boot.power_on_boot = true;
  PolicyStateMachine boot_policy(only_boot);
  CHECK(Intents(boot_policy.handle(LifecycleEvent::BootRequested)) ==
        std::vector<PolicyIntent>({PolicyIntent::PowerOnTv,
                                   PolicyIntent::MakeActiveSource}));
  CHECK(Intents(boot_policy.handle(LifecycleEvent::HostResumed)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::RefreshDeviceSession,
                                   PolicyIntent::CompleteHostTransition}));

  PolicyConfiguration only_resume;
  only_resume.power_on_resume = true;
  PolicyStateMachine resume_policy(only_resume);
  CHECK(resume_policy.handle(LifecycleEvent::BootRequested).steps.empty());
  CHECK(Intents(resume_policy.handle(LifecycleEvent::HostResumed)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::RefreshDeviceSession,
                                   PolicyIntent::PowerOnTv,
                                   PolicyIntent::MakeActiveSource,
                                   PolicyIntent::CompleteHostTransition}));

  PolicyConfiguration only_suspend_standby;
  only_suspend_standby.standby_on_suspend = true;
  PolicyStateMachine suspend_policy(only_suspend_standby);
  CHECK(Intents(suspend_policy.handle(LifecycleEvent::HostPreparingSuspend)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::StandbyTv,
                                   PolicyIntent::CompleteHostTransition}));

  PolicyConfiguration only_shutdown_standby;
  only_shutdown_standby.standby_on_shutdown = true;
  PolicyStateMachine shutdown_policy(only_shutdown_standby);
  CHECK(Intents(shutdown_policy.handle(LifecycleEvent::HostPreparingShutdown)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::StandbyTv,
                                   PolicyIntent::CompleteHostTransition}));

  PolicyConfiguration only_tv_suspend;
  only_tv_suspend.suspend_on_tv_standby = true;
  PolicyStateMachine tv_policy(only_tv_suspend);
  CHECK(Intents(tv_policy.handle(LifecycleEvent::ExternalTvStandby)) ==
        std::vector<PolicyIntent>({PolicyIntent::RequestHostSuspend}));

  PolicyConfiguration only_wake;
  only_wake.wake_host_on_tv_source_request = true;
  PolicyStateMachine wake_policy(only_wake);
  CHECK(Intents(wake_policy.handle(LifecycleEvent::SessionConnected)) ==
        std::vector<PolicyIntent>({PolicyIntent::PrepareWake}));
  CHECK(Intents(wake_policy.handle(LifecycleEvent::HostPreparingSuspend)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::PrepareWake,
                                   PolicyIntent::CompleteHostTransition}));
  CHECK(Intents(wake_policy.handle(LifecycleEvent::HostResumed)) ==
        std::vector<PolicyIntent>({PolicyIntent::ResetRemoteInput,
                                   PolicyIntent::RefreshDeviceSession,
                                   PolicyIntent::ReadWakeReason,
                                   PolicyIntent::CompleteHostTransition}));
  return EXIT_SUCCESS;
}

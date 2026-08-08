#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "cec_injector/mate/types.h"

namespace cec_injector::mate {

enum class LifecycleEvent {
  BootRequested,
  ExternalTvStandby,
  HostPreparingSuspend,
  HostResumed,
  HostPreparingShutdown,
  SessionConnected,
  SessionLost,
};

enum class LifecycleState {
  Running,
  SuspendRequested,
  PreparingSuspend,
  Suspended,
  Resuming,
  PreparingShutdown,
};

enum class PolicyIntent {
  RequestHostSuspend,
  PrepareWake,
  DisarmWake,
  StandbyTv,
  PowerOnTv,
  MakeActiveSource,
  ResetRemoteInput,
  RefreshDeviceSession,
  ReadWakeReason,
  CompleteHostTransition,
};

struct PolicyConfiguration {
  bool power_on_boot{};
  bool power_on_resume{};
  bool standby_on_suspend{};
  bool standby_on_shutdown{};
  bool suspend_on_tv_standby{};
  bool wake_host_on_tv_source_request{};

  bool operator==(const PolicyConfiguration&) const = default;
};

struct PolicyStep {
  PolicyIntent intent;
  std::optional<std::size_t> requires_success;

  bool operator==(const PolicyStep&) const = default;
};

struct PolicyPlan {
  LifecycleEvent event;
  std::vector<PolicyStep> steps;

  bool operator==(const PolicyPlan&) const = default;
};

class PolicyStateMachine {
 public:
  explicit PolicyStateMachine(PolicyConfiguration configuration = {})
      : configuration_(configuration) {}

  [[nodiscard]] PolicyPlan handle(LifecycleEvent event);
  void record_result(PolicyIntent intent, OperationResult result);

  [[nodiscard]] LifecycleState state() const noexcept { return state_; }
  [[nodiscard]] bool session_connected() const noexcept {
    return session_connected_;
  }
  [[nodiscard]] const PolicyConfiguration& configuration() const noexcept {
    return configuration_;
  }

 private:
  PolicyConfiguration configuration_;
  LifecycleState state_{LifecycleState::Running};
  bool session_connected_{};
};

}  // namespace cec_injector::mate

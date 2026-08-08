#include "cec_injector/mate/linux/linux_power_session.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cec_injector::mate::linux_platform {

struct LinuxPowerSessionModel::HoldState {
  struct Resource {
    void release() noexcept {
      if (inhibitor) inhibitor->release();
      inhibitor.reset();
      if (on_release) on_release();
      on_release = {};
    }

    std::shared_ptr<LinuxTransitionInhibitor> inhibitor;
    std::function<void()> on_release;
  };

  explicit HoldState(std::shared_ptr<LinuxTransitionInhibitor> owned,
                     Clock::time_point expires, std::function<void()> done)
      : resource(std::make_shared<Resource>(
            Resource{std::move(owned), std::move(done)})),
        control([owned_resource = this->resource] { owned_resource->release(); }),
        deadline(expires) {}

  void release() noexcept { (void)control.complete(); }
  [[nodiscard]] bool active() const noexcept { return control.active(); }
  [[nodiscard]] HostTransitionHold take_hold() noexcept {
    return control.take_hold();
  }

  std::shared_ptr<Resource> resource;
  HostTransitionHoldControl control;
  Clock::time_point deadline;
};

struct LinuxPowerSessionModel::Impl {
  Acquire acquire;
  Suspend suspend;
  Authorization authorization;
  std::chrono::milliseconds transition_deadline;
  mutable std::mutex mutex;
  NotificationSink sink;
  bool connected{};
  bool stopped{};
  HostPowerState power_state{HostPowerState::Running};
  AuthorizationState authorization_state{AuthorizationState::Unavailable};
  std::unique_ptr<LinuxTransitionInhibitor> sleep_inhibitor;
  std::unique_ptr<LinuxTransitionInhibitor> shutdown_inhibitor;
  std::vector<std::shared_ptr<HoldState>> holds;
};

LinuxPowerSessionModel::LinuxPowerSessionModel(
    Acquire acquire, Suspend suspend, Authorization authorization,
    std::chrono::milliseconds transition_deadline)
    : impl_(std::make_shared<Impl>()) {
  if (!acquire || !suspend || !authorization ||
      transition_deadline <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("invalid Linux power-session configuration");
  }
  impl_->acquire = std::move(acquire);
  impl_->suspend = std::move(suspend);
  impl_->authorization = std::move(authorization);
  impl_->transition_deadline = transition_deadline;
}

LinuxPowerSessionModel::~LinuxPowerSessionModel() { shutdown(); }

void LinuxPowerSessionModel::set_notification_sink(NotificationSink sink) {
  std::lock_guard lock(impl_->mutex);
  impl_->sink = std::move(sink);
}

void LinuxPowerSessionModel::acquire_inhibitor(LinuxTransitionKind kind) {
  auto acquired = impl_->acquire(kind);
  std::lock_guard lock(impl_->mutex);
  if (!impl_->connected || impl_->stopped) {
    if (acquired) acquired->release();
    return;
  }
  auto& destination = kind == LinuxTransitionKind::Sleep
                          ? impl_->sleep_inhibitor
                          : impl_->shutdown_inhibitor;
  if (destination) destination->release();
  destination = std::move(acquired);
}

void LinuxPowerSessionModel::on_bus_connected() {
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopped) return;
    impl_->connected = true;
  }
  acquire_inhibitor(LinuxTransitionKind::Sleep);
  acquire_inhibitor(LinuxTransitionKind::Shutdown);
  const auto checks = impl_->authorization();
  std::lock_guard lock(impl_->mutex);
  if (!checks.suspend || !checks.suspend_multiple_sessions) {
    impl_->authorization_state = AuthorizationState::Unavailable;
  } else if (*checks.suspend && *checks.suspend_multiple_sessions) {
    impl_->authorization_state = AuthorizationState::Authorized;
  } else {
    impl_->authorization_state = AuthorizationState::Denied;
  }
}

void LinuxPowerSessionModel::release_all_holds() noexcept {
  std::vector<std::shared_ptr<HoldState>> holds;
  {
    std::lock_guard lock(impl_->mutex);
    holds.swap(impl_->holds);
  }
  for (const auto& hold : holds) hold->release();
}

void LinuxPowerSessionModel::on_bus_disconnected() {
  std::unique_ptr<LinuxTransitionInhibitor> sleep;
  std::unique_ptr<LinuxTransitionInhibitor> shutdown;
  {
    std::lock_guard lock(impl_->mutex);
    impl_->connected = false;
    impl_->authorization_state = AuthorizationState::Unavailable;
    sleep = std::move(impl_->sleep_inhibitor);
    shutdown = std::move(impl_->shutdown_inhibitor);
  }
  if (sleep) sleep->release();
  if (shutdown) shutdown->release();
  release_all_holds();
}

void LinuxPowerSessionModel::notify_prepare(LinuxTransitionKind kind,
                                            HostLifecycleEvent event,
                                            Clock::time_point now) {
  NotificationSink sink;
  std::shared_ptr<HoldState> hold;
  std::optional<HostTransitionHold> transition_hold;
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->connected || impl_->stopped) return;
    if ((kind == LinuxTransitionKind::Sleep &&
         (impl_->power_state == HostPowerState::PreparingSuspend ||
          impl_->power_state == HostPowerState::Suspended)) ||
        (kind == LinuxTransitionKind::Shutdown &&
         impl_->power_state == HostPowerState::PreparingShutdown)) {
      return;
    }
    auto& armed = kind == LinuxTransitionKind::Sleep
                      ? impl_->sleep_inhibitor
                      : impl_->shutdown_inhibitor;
    if (armed) {
      std::shared_ptr<LinuxTransitionInhibitor> inhibitor(std::move(armed));
      std::weak_ptr<Impl> weak = impl_;
      hold = std::make_shared<HoldState>(
          std::move(inhibitor), now + impl_->transition_deadline, [weak, kind] {
            const auto impl = weak.lock();
            if (!impl || kind != LinuxTransitionKind::Sleep) return;
            std::lock_guard lock(impl->mutex);
            if (impl->power_state == HostPowerState::PreparingSuspend) {
              impl->power_state = HostPowerState::Suspended;
            }
          });
      impl_->holds.push_back(hold);
      transition_hold.emplace(hold->take_hold());
    }
    impl_->power_state = kind == LinuxTransitionKind::Sleep
                             ? HostPowerState::PreparingSuspend
                             : HostPowerState::PreparingShutdown;
    sink = impl_->sink;
  }
  if (!sink) {
    if (hold) hold->release();
    return;
  }
  try {
    sink(HostLifecycleNotification(event, std::move(transition_hold)));
  } catch (...) {
    if (hold) hold->release();
  }
}

void LinuxPowerSessionModel::notify(HostLifecycleEvent event) {
  NotificationSink sink;
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->connected || impl_->stopped) return;
    sink = impl_->sink;
  }
  if (sink) {
    try {
      sink(HostLifecycleNotification(event));
    } catch (...) {
    }
  }
}

void LinuxPowerSessionModel::on_prepare_for_sleep(bool preparing,
                                                  Clock::time_point now) {
  if (preparing) {
    notify_prepare(LinuxTransitionKind::Sleep,
                   HostLifecycleEvent::PreparingSuspend, now);
    return;
  }
  acquire_inhibitor(LinuxTransitionKind::Sleep);
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->connected || impl_->stopped) return;
    impl_->power_state = HostPowerState::Resuming;
  }
  notify(HostLifecycleEvent::Resumed);
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->power_state == HostPowerState::Resuming) {
      impl_->power_state = HostPowerState::Running;
    }
  }
}

void LinuxPowerSessionModel::on_prepare_for_shutdown(bool preparing,
                                                     Clock::time_point now) {
  if (preparing) {
    notify_prepare(LinuxTransitionKind::Shutdown,
                   HostLifecycleEvent::PreparingShutdown, now);
    return;
  }
  acquire_inhibitor(LinuxTransitionKind::Shutdown);
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->connected || impl_->stopped) return;
    impl_->power_state = HostPowerState::Running;
  }
  notify(HostLifecycleEvent::ShutdownCancelled);
}

void LinuxPowerSessionModel::tick(Clock::time_point now) {
  std::vector<std::shared_ptr<HoldState>> expired;
  {
    std::lock_guard lock(impl_->mutex);
    auto& holds = impl_->holds;
    for (const auto& hold : holds) {
      if (hold->active() && now >= hold->deadline) expired.push_back(hold);
    }
    std::erase_if(holds, [](const auto& hold) { return !hold->active(); });
  }
  for (const auto& hold : expired) hold->release();
}

void LinuxPowerSessionModel::shutdown() {
  std::unique_ptr<LinuxTransitionInhibitor> sleep;
  std::unique_ptr<LinuxTransitionInhibitor> shutdown;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopped) return;
    impl_->stopped = true;
    impl_->connected = false;
    impl_->authorization_state = AuthorizationState::Unavailable;
    impl_->sink = {};
    sleep = std::move(impl_->sleep_inhibitor);
    shutdown = std::move(impl_->shutdown_inhibitor);
  }
  if (sleep) sleep->release();
  if (shutdown) shutdown->release();
  release_all_holds();
}

OperationResult LinuxPowerSessionModel::request_system_suspend() {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->connected || impl_->stopped) return OperationResult::Disconnected;
  }
  switch (impl_->suspend()) {
    case LinuxLogindCallResult::Accepted: return OperationResult::Success;
    case LinuxLogindCallResult::Denied: return OperationResult::PermissionDenied;
    case LinuxLogindCallResult::Rejected: return OperationResult::Rejected;
    case LinuxLogindCallResult::Timeout: return OperationResult::Timeout;
    case LinuxLogindCallResult::Unavailable: return OperationResult::Disconnected;
  }
  return OperationResult::InternalError;
}

AuthorizationState LinuxPowerSessionModel::suspend_authorization() const noexcept {
  std::lock_guard lock(impl_->mutex);
  return impl_->authorization_state;
}

HostPowerState LinuxPowerSessionModel::host_power_state() const noexcept {
  std::lock_guard lock(impl_->mutex);
  return impl_->power_state;
}

bool LinuxPowerSessionModel::connected() const noexcept {
  std::lock_guard lock(impl_->mutex);
  return impl_->connected;
}

std::chrono::seconds LinuxPowerSessionModel::next_reconnect_backoff(
    std::chrono::seconds current) noexcept {
  if (current < std::chrono::seconds(1)) return std::chrono::seconds(1);
  return std::min(current * 2, kMaximumReconnectBackoff);
}

}  // namespace cec_injector::mate::linux_platform

#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/device_session_factory.h"
#include "cec_injector/mate/host_platform.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "test_support.h"

using namespace cec_injector::mate;

namespace {

class FakeObserver final : public DeviceSessionObserver {
 public:
  void on_session_state_changed(const DeviceId&, SessionState state) override {
    last_state = state;
  }
  void on_external_cec_frame(const DeviceId&,
                             std::span<const std::uint8_t> frame) override {
    frame_size = frame.size();
  }
  void on_wake_state_changed(const DeviceId&, const WakeState& state) override {
    wake = state;
  }

  SessionState last_state{SessionState::Disconnected};
  std::size_t frame_size{};
  WakeState wake;
};

class FakeDeviceSession final : public DeviceSession {
 public:
  explicit FakeDeviceSession(DeviceId id = DeviceId("fake-device"))
      : id_(std::move(id)) {}

  const DeviceId& device_id() const noexcept override { return id_; }
  SessionState state() const noexcept override { return state_; }
  void set_observer(DeviceSessionObserver* observer) override { observer_ = observer; }

  OperationResult connect() override {
    state_ = SessionState::Connected;
    if (observer_) observer_->on_session_state_changed(id_, state_);
    return OperationResult::Success;
  }
  OperationResult disconnect() override {
    state_ = SessionState::Disconnected;
    if (observer_) observer_->on_session_state_changed(id_, state_);
    return OperationResult::Success;
  }
  OperationResult refresh_state() override { return OperationResult::Success; }
  DeviceStateSnapshot snapshot() const override { return snapshot_; }
  OperationResult power_on_tv() override { return OperationResult::Success; }
  OperationResult standby_tv() override { return OperationResult::Success; }
  OperationResult make_active_source() override { return OperationResult::Success; }
  WakeState query_wake_state() override { return snapshot_.wake; }
  OperationResult configure_wake(std::uint16_t physical_address) override {
    snapshot_.addresses.physical = physical_address;
    snapshot_.wake.configured = true;
    return OperationResult::Success;
  }
  OperationResult arm_wake() override {
    snapshot_.wake.armed = true;
    if (observer_) observer_->on_wake_state_changed(id_, snapshot_.wake);
    return OperationResult::Success;
  }
  OperationResult disarm_wake() override {
    snapshot_.wake.armed = false;
    return OperationResult::Success;
  }
  WakeReason read_last_wake_reason() override {
    return {OperationResult::Success, {1, 2}};
  }
  OperationResult clear_last_wake_reason() override {
    return OperationResult::Success;
  }

  void deliver_frame() {
    const std::array<std::uint8_t, 2> frame{1, 2};
    if (observer_) observer_->on_external_cec_frame(id_, frame);
  }

 private:
  DeviceId id_;
  SessionState state_{SessionState::Discovered};
  DeviceStateSnapshot snapshot_{{true, true}, {}, {true, false, false, false}};
  DeviceSessionObserver* observer_{};
};

class FakeSessionFactory final : public DeviceSessionFactory {
 public:
  explicit FakeSessionFactory(std::vector<DiscoveredDevice> inventory)
      : inventory_(std::move(inventory)) {}

  std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& selected) override {
    for (const auto& known : inventory_) {
      if (known.id == selected.id && known.locator == selected.locator) {
        return std::make_unique<FakeDeviceSession>(selected.id);
      }
    }
    return {};
  }

 private:
  std::vector<DiscoveredDevice> inventory_;
};

class FakeLifecycleObserver final : public HostLifecycleObserver {
 public:
  void on_host_lifecycle_notification(
      HostLifecycleNotification notification) override {
    events.push_back(notification.event);
    if (notification.transition_hold) {
      holds.push_back(std::move(*notification.transition_hold));
    }
  }

  std::vector<HostLifecycleEvent> events;
  std::vector<HostTransitionHold> holds;
};

class FakeHostPlatform final : public HostPlatform {
 public:
  void set_lifecycle_observer(HostLifecycleObserver* observer) override {
    lifecycle_observer_ = observer;
  }
  std::vector<DiscoveredDevice> discovered_devices() override {
    return inventory;
  }
  PhysicalAddressResolution resolve_physical_address() override {
    return {PhysicalAddressStatus::Available, 4096};
  }
  OperationResult request_system_suspend() override {
    power_state_ = HostPowerState::PreparingSuspend;
    return OperationResult::Success;
  }
  HostPowerState host_power_state() const noexcept override { return power_state_; }
  AuthorizationState suspend_authorization() const override {
    return AuthorizationState::Authorized;
  }
  AuthorizationState remote_wake_authorization(const DeviceId&) const override {
    return AuthorizationState::Authorized;
  }
  OperationResult emit_remote_action(SemanticRemoteAction action,
                                     RemoteActionState state) override {
    last_action = action;
    last_action_state = state;
    return OperationResult::Success;
  }
  OperationResult reset_remote_input() override {
    last_action.reset();
    return OperationResult::Success;
  }

  void deliver(HostLifecycleEvent event,
               std::optional<HostTransitionHold> hold = std::nullopt) {
    if (lifecycle_observer_) {
      lifecycle_observer_->on_host_lifecycle_notification(
          HostLifecycleNotification(event, std::move(hold)));
    }
  }

  std::vector<DiscoveredDevice> inventory{
      {DeviceId("device-a"), PlatformDeviceLocator("opaque-a"), "Fake A"},
      {DeviceId("device-b"), PlatformDeviceLocator("opaque-b"), "Fake B"}};
  HostPowerState power_state_{HostPowerState::Running};
  std::optional<SemanticRemoteAction> last_action;
  RemoteActionState last_action_state{RemoteActionState::Released};
  HostLifecycleObserver* lifecycle_observer_{};
};

}  // namespace

int main() {
  FakeObserver observer;
  FakeDeviceSession session;
  session.set_observer(&observer);
  CHECK(session.connect() == OperationResult::Success);
  CHECK(observer.last_state == SessionState::Connected);
  CHECK(session.configure_wake(4096) == OperationResult::Success);
  CHECK(session.arm_wake() == OperationResult::Success);
  CHECK(observer.wake.armed);
  session.deliver_frame();
  CHECK(observer.frame_size == 2);

  FakeHostPlatform platform;
  const auto discovered = platform.discovered_devices();
  CHECK(discovered.size() == 2);
  FakeSessionFactory empty_factory({});
  CHECK(!empty_factory.create_session(discovered[0]));
  FakeSessionFactory factory(discovered);
  auto selected_session = factory.create_session(discovered[1]);
  CHECK(selected_session);
  CHECK(selected_session->device_id() == discovered[1].id);
  const DiscoveredDevice unknown{DeviceId("unknown"),
                                 PlatformDeviceLocator("opaque-unknown"),
                                 "Unknown"};
  CHECK(!factory.create_session(unknown));

  CHECK(platform.resolve_physical_address().status ==
        PhysicalAddressStatus::Available);
  CHECK(platform.emit_remote_action(SemanticRemoteAction::Guide,
                                    RemoteActionState::Pressed) ==
        OperationResult::Success);
  CHECK(platform.last_action == SemanticRemoteAction::Guide);
  CHECK(platform.request_system_suspend() == OperationResult::Success);
  CHECK(platform.host_power_state() == HostPowerState::PreparingSuspend);
  CHECK(platform.reset_remote_input() == OperationResult::Success);
  CHECK(!platform.last_action);

  FakeLifecycleObserver lifecycle;
  platform.set_lifecycle_observer(&lifecycle);
  int suspend_releases = 0;
  int shutdown_releases = 0;
  platform.deliver(HostLifecycleEvent::PreparingSuspend,
                   HostTransitionHold([&] { ++suspend_releases; }));
  platform.deliver(HostLifecycleEvent::Resumed);
  platform.deliver(HostLifecycleEvent::PreparingShutdown,
                   HostTransitionHold([&] { ++shutdown_releases; }));
  platform.deliver(HostLifecycleEvent::ShutdownCancelled);
  CHECK(lifecycle.events ==
        std::vector<HostLifecycleEvent>({
            HostLifecycleEvent::PreparingSuspend,
            HostLifecycleEvent::Resumed,
            HostLifecycleEvent::PreparingShutdown,
            HostLifecycleEvent::ShutdownCancelled}));
  CHECK(lifecycle.holds.size() == 2);

  CHECK(lifecycle.holds[0].active());
  CHECK(lifecycle.holds[0].complete());
  CHECK(!lifecycle.holds[0].active());
  CHECK(!lifecycle.holds[0].complete());
  CHECK(suspend_releases == 1);

  {
    HostTransitionHold abandoned([&] { ++shutdown_releases; });
    CHECK(abandoned.active());
  }
  CHECK(shutdown_releases == 1);
  lifecycle.holds.clear();
  CHECK(shutdown_releases == 2);

  int forced_releases = 0;
  HostTransitionHoldControl control([&] { ++forced_releases; });
  auto controlled = control.take_hold();
  CHECK(controlled.active());
  CHECK(control.active());
  CHECK(!control.take_hold().active());
  CHECK(control.complete());
  CHECK(!control.active());
  CHECK(!controlled.active());
  CHECK(!controlled.complete());
  CHECK(!control.complete());
  CHECK(forced_releases == 1);

  int move_releases = 0;
  HostTransitionHold moved_from([&] { ++move_releases; });
  HostTransitionHold moved_to(std::move(moved_from));
  CHECK(!moved_from.active());
  CHECK(moved_to.active());
  HostTransitionHold assigned([&] { ++move_releases; });
  assigned = std::move(moved_to);
  CHECK(move_releases == 1);
  CHECK(!moved_to.active());
  CHECK(assigned.active());
  CHECK(assigned.complete());
  CHECK(move_releases == 2);
  return EXIT_SUCCESS;
}

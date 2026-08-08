#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/host_platform.h"

#include <array>

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
  DeviceId id_{"fake-device"};
  SessionState state_{SessionState::Discovered};
  DeviceStateSnapshot snapshot_{{true, true}, {}, {true, false, false, false}};
  DeviceSessionObserver* observer_{};
};

class FakeHostPlatform final : public HostPlatform {
 public:
  std::vector<DiscoveredDevice> discovered_devices() override {
    return {{DeviceId("fake-device"), PlatformDeviceLocator("opaque"), "Fake"}};
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

  HostPowerState power_state_{HostPowerState::Running};
  std::optional<SemanticRemoteAction> last_action;
  RemoteActionState last_action_state{RemoteActionState::Released};
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
  CHECK(platform.discovered_devices().size() == 1);
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
  return EXIT_SUCCESS;
}

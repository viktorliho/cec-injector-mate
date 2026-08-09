#include "cec_injector/mate/mate_service.h"

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cec_injector::mate;

#define REQUIRE(condition)                                                     \
  do {                                                                         \
    if (!(condition)) throw std::runtime_error(#condition);                    \
  } while (false)

namespace {

struct SessionRecord {
  DeviceId id;
  DeviceSessionObserver* observer{};
  SessionState state{SessionState::Discovered};
  DeviceStateSnapshot snapshot{{true, true}, {0x1000, 4},
                               {true, false, false, false}};
  int connects{};
  int disconnects{};
  int refreshes{};
  int powers{};
  int active_sources{};
  int standbys{};
  int wake_queries{};
  int wake_configures{};
  int wake_arms{};
  int wake_disarms{};
  int wake_reads{};
  int wake_clears{};
  bool lose_on_power{};
  bool lose_on_wake{};
  OperationResult wake_configure_result{OperationResult::Success};
  std::vector<std::string> operations;

  void frame(std::initializer_list<std::uint8_t> bytes) {
    const std::vector<std::uint8_t> frame_value(bytes);
    if (observer) observer->on_external_cec_frame(id, frame_value);
  }
};

class FakeSession final : public DeviceSession {
 public:
  explicit FakeSession(std::shared_ptr<SessionRecord> record)
      : record_(std::move(record)) {}
  const DeviceId& device_id() const noexcept override { return record_->id; }
  SessionState state() const noexcept override { return record_->state; }
  void set_observer(DeviceSessionObserver* observer) override {
    record_->observer = observer;
  }
  OperationResult connect() override {
    ++record_->connects;
    record_->operations.push_back("connect");
    record_->state = SessionState::Connected;
    if (record_->observer) {
      record_->observer->on_session_state_changed(record_->id, record_->state);
    }
    return OperationResult::Success;
  }
  OperationResult disconnect() override {
    ++record_->disconnects;
    record_->operations.push_back("disconnect");
    record_->state = SessionState::Disconnected;
    if (record_->observer) {
      record_->observer->on_session_state_changed(record_->id, record_->state);
    }
    return OperationResult::Success;
  }
  OperationResult refresh_state() override {
    ++record_->refreshes;
    record_->operations.push_back("refresh");
    return OperationResult::Success;
  }
  DeviceStateSnapshot snapshot() const override { return record_->snapshot; }
  OperationResult power_on_tv() override {
    ++record_->powers;
    record_->operations.push_back("power");
    if (record_->lose_on_power && record_->observer) {
      record_->state = SessionState::Disconnected;
      record_->observer->on_session_state_changed(record_->id, record_->state);
    }
    return OperationResult::Success;
  }
  OperationResult standby_tv() override {
    ++record_->standbys;
    record_->operations.push_back("standby");
    return OperationResult::Success;
  }
  OperationResult make_active_source() override {
    ++record_->active_sources;
    record_->operations.push_back("active");
    return OperationResult::Success;
  }
  WakeState query_wake_state() override {
    ++record_->wake_queries;
    record_->operations.push_back("wake-query");
    return record_->snapshot.wake;
  }
  OperationResult configure_wake(std::uint16_t address) override {
    ++record_->wake_configures;
    record_->operations.push_back("wake-configure");
    REQUIRE(address == record_->snapshot.addresses.physical);
    if (record_->lose_on_wake && record_->observer) {
      record_->state = SessionState::Disconnected;
      record_->observer->on_session_state_changed(record_->id, record_->state);
    }
    return record_->wake_configure_result;
  }
  OperationResult arm_wake() override {
    ++record_->wake_arms;
    record_->operations.push_back("wake-arm");
    return OperationResult::Success;
  }
  OperationResult disarm_wake() override {
    ++record_->wake_disarms;
    record_->operations.push_back("wake-disarm");
    return OperationResult::Success;
  }
  WakeReason read_last_wake_reason() override {
    ++record_->wake_reads;
    record_->operations.push_back("wake-read");
    return {OperationResult::Success, {0x0f, 0x86, 0x10, 0x00}};
  }
  OperationResult clear_last_wake_reason() override {
    ++record_->wake_clears;
    record_->operations.push_back("wake-clear");
    return OperationResult::Success;
  }

 private:
  std::shared_ptr<SessionRecord> record_;
};

class FakeFactory final : public DeviceSessionFactory {
 public:
  std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device,
      const DeviceSessionConfiguration& configuration) override {
    ++calls;
    configurations.push_back(configuration);
    devices.push_back(device);
    if (!configuration.valid() || fail) return {};
    auto record = std::make_shared<SessionRecord>();
    record->id = device.id;
    record->snapshot.addresses.physical = configuration.physical_address;
    records.push_back(record);
    return std::make_unique<FakeSession>(record);
  }

  int calls{};
  bool fail{};
  std::vector<DeviceSessionConfiguration> configurations;
  std::vector<DiscoveredDevice> devices;
  std::vector<std::shared_ptr<SessionRecord>> records;
};

class FakeHost final : public HostPlatform {
 public:
  void set_lifecycle_observer(HostLifecycleObserver* value) override {
    observer = value;
  }
  std::vector<DiscoveredDevice> discovered_devices() override {
    return inventory;
  }
  PhysicalAddressResolution resolve_physical_address() override { return pa; }
  OperationResult request_system_suspend() override {
    ++suspends;
    operations.push_back("suspend");
    return suspend_result;
  }
  HostPowerState host_power_state() const noexcept override {
    return HostPowerState::Running;
  }
  AuthorizationState suspend_authorization() const override {
    return AuthorizationState::Authorized;
  }
  AuthorizationState remote_wake_authorization(const DeviceId&) const override {
    return wake_authorization;
  }
  OperationResult emit_remote_action(SemanticRemoteAction action,
                                     RemoteActionState state) override {
    input.emplace_back(action, state);
    operations.push_back(state == RemoteActionState::Pressed ? "press" : "release");
    return OperationResult::Success;
  }
  OperationResult reset_remote_input() override {
    ++resets;
    operations.push_back("reset");
    return OperationResult::Success;
  }
  void lifecycle(HostLifecycleEvent event,
                 std::optional<HostTransitionHold> hold = std::nullopt) {
    REQUIRE(observer);
    observer->on_host_lifecycle_notification(
        HostLifecycleNotification(event, std::move(hold)));
  }

  std::vector<DiscoveredDevice> inventory;
  PhysicalAddressResolution pa{PhysicalAddressStatus::Available, 0x1000};
  AuthorizationState wake_authorization{AuthorizationState::Authorized};
  OperationResult suspend_result{OperationResult::Success};
  int suspends{};
  int resets{};
  std::vector<std::pair<SemanticRemoteAction, RemoteActionState>> input;
  std::vector<std::string> operations;
  HostLifecycleObserver* observer{};
};

DiscoveredDevice Device(std::string id, std::string locator) {
  return {DeviceId(std::move(id)), PlatformDeviceLocator(std::move(locator)),
          "test Device"};
}

void TestDiscoveryAndPa() {
  FakeHost host;
  FakeFactory factory;
  MateService service(host, factory);
  REQUIRE(service.start() == OperationResult::Success);
  service.wait_idle();
  REQUIRE(!service.has_active_session());
  REQUIRE(factory.calls == 0);

  host.inventory = {Device("a", "opaque-a"), Device("b", "opaque-b")};
  service.refresh_discovery();
  service.wait_idle();
  REQUIRE(factory.calls == 0);

  host.inventory = {Device("a", "opaque-a")};
  service.refresh_discovery();
  service.wait_idle();
  REQUIRE(factory.calls == 1);
  REQUIRE(factory.configurations.back().physical_address == 0x1000);
  REQUIRE(factory.records.back()->connects == 1);
  REQUIRE(factory.records.back()->wake_disarms == 1);
  REQUIRE(factory.records.back()->powers == 0);
  REQUIRE(factory.records.back()->active_sources == 0);
  REQUIRE(host.suspends == 0 && host.input.empty());
  const auto first_generation = service.session_generation();

  host.pa = {PhysicalAddressStatus::Available, 0x2100};
  service.refresh_discovery();
  service.wait_idle();
  REQUIRE(factory.calls == 2);
  REQUIRE(factory.records.front()->disconnects == 1);
  REQUIRE(factory.configurations.back().physical_address == 0x2100);
  REQUIRE(service.session_generation() > first_generation);

  host.inventory = {Device("b", "opaque-b")};
  service.refresh_discovery();
  service.wait_idle();
  REQUIRE(!service.has_active_session());
  REQUIRE(factory.calls == 2);

  service.shutdown();

  for (const auto status : {PhysicalAddressStatus::Unavailable,
                            PhysicalAddressStatus::Ambiguous}) {
    FakeHost unavailable_host;
    FakeFactory unavailable_factory;
    unavailable_host.inventory = {Device("a", "opaque-a")};
    unavailable_host.pa = {status, std::nullopt};
    MateService unavailable(unavailable_host, unavailable_factory);
    REQUIRE(unavailable.start() == OperationResult::Success);
    unavailable.wait_idle();
    REQUIRE(unavailable_factory.calls == 0);
    unavailable.shutdown();
  }
}

void TestInputStandbyAndLifecycle() {
  FakeHost host;
  FakeFactory factory;
  host.inventory = {Device("a", "opaque-a")};
  MateServiceConfiguration configuration;
  configuration.policy.suspend_on_tv_standby = true;
  configuration.policy.wake_host_on_tv_source_request = true;
  configuration.policy.standby_on_suspend = true;
  configuration.policy.standby_on_shutdown = true;
  configuration.policy.power_on_resume = true;
  MateService service(host, factory, configuration);
  REQUIRE(service.start() == OperationResult::Success);
  service.wait_idle();
  auto record = factory.records.back();
  REQUIRE(record->wake_queries == 1 && record->wake_configures == 1 &&
        record->wake_arms == 1);

  record->frame({0x04, 0x44, 0x01});
  service.wait_idle();
  REQUIRE(host.input.back() ==
        std::pair(SemanticRemoteAction::Up, RemoteActionState::Pressed));
  record->frame({0x04, 0x45});
  service.wait_idle();
  REQUIRE(host.input.back() ==
        std::pair(SemanticRemoteAction::Up, RemoteActionState::Released));
  for (const auto [operand, action] :
       {std::pair{std::uint8_t{0x71}, SemanticRemoteAction::Next},
        std::pair{std::uint8_t{0x72}, SemanticRemoteAction::Previous},
        std::pair{std::uint8_t{0x73}, SemanticRemoteAction::Pause},
        std::pair{std::uint8_t{0x74}, SemanticRemoteAction::Play}}) {
    record->frame({0x04, 0x44, operand});
    service.wait_idle();
    REQUIRE(host.input.back() ==
            std::pair(action, RemoteActionState::Pressed));
  }
  const auto input_count = host.input.size();
  record->frame({0x14, 0x44, 0x01});
  record->frame({0x05, 0x44, 0x01});
  record->frame({0x04, 0x44});
  record->frame({0x04, 0x44, 0x51});
  service.wait_idle();
  REQUIRE(host.input.size() == input_count);

  record->frame({0x0f, 0x36});
  service.wait_idle();
  REQUIRE(host.suspends == 1);
  record->frame({0x0f, 0x36});
  service.wait_idle();
  REQUIRE(host.suspends == 1);

  int suspend_hold_releases = 0;
  host.lifecycle(HostLifecycleEvent::PreparingSuspend,
                 HostTransitionHold([&] { ++suspend_hold_releases; }));
  service.wait_idle();
  REQUIRE(suspend_hold_releases == 1);
  REQUIRE(record->standbys == 1);
  REQUIRE(record->wake_configures == 2 && record->wake_arms == 2);

  host.lifecycle(HostLifecycleEvent::Resumed);
  service.wait_idle();
  REQUIRE(record->refreshes == 1);
  REQUIRE(record->powers == 1 && record->active_sources == 1);
  REQUIRE(record->wake_reads == 1 && record->wake_clears == 1);

  int shutdown_hold_releases = 0;
  host.lifecycle(HostLifecycleEvent::PreparingShutdown,
                 HostTransitionHold([&] { ++shutdown_hold_releases; }));
  service.wait_idle();
  REQUIRE(shutdown_hold_releases == 1);
  REQUIRE(record->standbys == 2);
  host.lifecycle(HostLifecycleEvent::ShutdownCancelled);
  service.wait_idle();

  service.shutdown();
  REQUIRE(record->disconnects == 1);
  REQUIRE(host.resets > 0);
}

void TestGenerationAndBoot() {
  FakeHost host;
  FakeFactory factory;
  host.inventory = {Device("a", "opaque-a")};
  MateServiceConfiguration configuration;
  configuration.policy.power_on_boot = true;
  MateService service(host, factory, configuration);
  REQUIRE(service.start() == OperationResult::Success);
  service.wait_idle();
  auto first = factory.records.back();
  REQUIRE(first->powers == 0 && first->active_sources == 0);
  first->lose_on_power = true;
  service.trigger_boot();
  service.wait_idle();
  REQUIRE(first->powers == 1);
  REQUIRE(first->active_sources == 0);
  REQUIRE(factory.records.size() == 2);
  REQUIRE(factory.records.back()->powers == 0);
  REQUIRE(factory.records.back()->active_sources == 0);
  service.shutdown();
}

void TestWakeFailureStillCompletesTransition() {
  FakeHost host;
  FakeFactory factory;
  host.inventory = {Device("a", "opaque-a")};
  MateServiceConfiguration configuration;
  configuration.policy.wake_host_on_tv_source_request = true;
  configuration.policy.standby_on_suspend = true;
  MateService service(host, factory, configuration);
  REQUIRE(service.start() == OperationResult::Success);
  service.wait_idle();
  auto record = factory.records.back();
  record->wake_configure_result = OperationResult::Rejected;
  int releases = 0;
  host.lifecycle(HostLifecycleEvent::PreparingSuspend,
                 HostTransitionHold([&] { ++releases; }));
  service.wait_idle();
  REQUIRE(record->standbys == 1);
  REQUIRE(releases == 1);
  service.shutdown();
}

void TestWakeAuthorizationAndHoldCancellation() {
  for (const auto authorization : {AuthorizationState::Denied,
                                   AuthorizationState::Unavailable}) {
    FakeHost host;
    FakeFactory factory;
    host.inventory = {Device("a", "opaque-a")};
    host.wake_authorization = authorization;
    MateServiceConfiguration configuration;
    configuration.policy.wake_host_on_tv_source_request = true;
    MateService service(host, factory, configuration);
    REQUIRE(service.start() == OperationResult::Success);
    service.wait_idle();
    REQUIRE(factory.records.back()->wake_configures == 0);
    REQUIRE(factory.records.back()->wake_arms == 0);
    service.shutdown();
  }

  FakeHost host;
  FakeFactory factory;
  host.inventory = {Device("a", "opaque-a")};
  MateServiceConfiguration configuration;
  configuration.policy.wake_host_on_tv_source_request = true;
  MateService service(host, factory, configuration);
  REQUIRE(service.start() == OperationResult::Success);
  service.wait_idle();
  auto record = factory.records.back();
  record->lose_on_wake = true;
  int releases = 0;
  host.lifecycle(HostLifecycleEvent::PreparingSuspend,
                 HostTransitionHold([&] { ++releases; }));
  service.wait_idle();
  REQUIRE(releases == 1);
  REQUIRE(factory.records.size() == 2);
  service.shutdown();
}

}  // namespace

int main() {
  TestDiscoveryAndPa();
  TestInputStandbyAndLifecycle();
  TestGenerationAndBoot();
  TestWakeFailureStillCompletesTransition();
  TestWakeAuthorizationAndHoldCancellation();
  return EXIT_SUCCESS;
}

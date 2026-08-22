#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/gui/macos/mac_in_process_mate_frontend_backend.h"

namespace {

using namespace cec_injector::mate;
using namespace cec_injector::mate::frontend;
using namespace cec_injector::mate::gui::macos;

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}

bool WaitFor(std::function<bool()> predicate,
             std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

struct BackendState {
  mutable std::mutex mutex;
  std::vector<DiscoveredDevice> devices;
  PhysicalAddressResolution physical_address{PhysicalAddressStatus::Unavailable,
                                             std::nullopt};
  DisplaySummary display;
};

class FakeHostPlatform final : public HostPlatform {
 public:
  explicit FakeHostPlatform(std::shared_ptr<BackendState> state)
      : state_(std::move(state)) {}

  void set_lifecycle_observer(HostLifecycleObserver*) override {}

  std::vector<DiscoveredDevice> discovered_devices() override {
    std::lock_guard lock(state_->mutex);
    return state_->devices;
  }

  PhysicalAddressResolution resolve_physical_address() override {
    std::lock_guard lock(state_->mutex);
    return state_->physical_address;
  }

  DisplaySummary display_summary() override {
    std::lock_guard lock(state_->mutex);
    return state_->display;
  }

  OperationResult request_system_suspend() override {
    return OperationResult::Unsupported;
  }

  HostPowerState host_power_state() const noexcept override {
    return HostPowerState::Running;
  }

  AuthorizationState suspend_authorization() const override {
    return AuthorizationState::Unavailable;
  }

  AuthorizationState remote_wake_authorization(const DeviceId&) const override {
    return AuthorizationState::Unavailable;
  }

  OperationResult emit_remote_action(SemanticRemoteAction,
                                     RemoteActionState) override {
    return OperationResult::Unsupported;
  }

  OperationResult reset_remote_input() override {
    return OperationResult::Success;
  }

 private:
  std::shared_ptr<BackendState> state_;
};

struct SessionRecord {
  DeviceSessionObserver* observer{};
  SessionState state{SessionState::Disconnected};
  DeviceStateSnapshot snapshot;
};

struct FactoryState {
  OperationResult connect_result{OperationResult::Success};
  OperationResult action_result{OperationResult::Success};
  std::vector<std::shared_ptr<SessionRecord>> records;
};

class FakeSession final : public DeviceSession {
 public:
  FakeSession(DeviceId id, DeviceSessionConfiguration configuration,
              std::shared_ptr<SessionRecord> record,
              std::shared_ptr<FactoryState> factory_state)
      : id_(std::move(id)), configuration_(configuration),
        record_(std::move(record)),
        factory_state_(std::move(factory_state)) {
    record_->snapshot.addresses.physical = configuration_.physical_address;
    record_->snapshot.addresses.logical = std::uint8_t{4};
    record_->snapshot.wake.supported = true;
  }

  const DeviceId& device_id() const noexcept override { return id_; }
  SessionState state() const noexcept override { return record_->state; }
  void set_observer(DeviceSessionObserver* observer) override {
    record_->observer = observer;
  }
  OperationResult connect() override {
    if (factory_state_->connect_result != OperationResult::Success) {
      record_->state = SessionState::Disconnected;
      return factory_state_->connect_result;
    }
    record_->state = SessionState::Connected;
    if (record_->observer) {
      record_->observer->on_session_state_changed(id_, record_->state);
    }
    return OperationResult::Success;
  }
  OperationResult disconnect() override {
    record_->state = SessionState::Disconnected;
    if (record_->observer) {
      record_->observer->on_session_state_changed(id_, record_->state);
    }
    return OperationResult::Success;
  }
  OperationResult refresh_state() override { return OperationResult::Success; }
  DeviceStateSnapshot snapshot() const override { return record_->snapshot; }
  OperationResult power_on_tv() override { return factory_state_->action_result; }
  OperationResult standby_tv() override { return factory_state_->action_result; }
  OperationResult make_active_source() override { return factory_state_->action_result; }
  WakeState query_wake_state() override { return record_->snapshot.wake; }
  OperationResult configure_wake(std::uint16_t) override {
    return OperationResult::Unsupported;
  }
  OperationResult arm_wake() override { return OperationResult::Unsupported; }
  OperationResult disarm_wake() override { return OperationResult::Unsupported; }
  WakeReason read_last_wake_reason() override {
    return {.result = OperationResult::Unsupported};
  }
  OperationResult clear_last_wake_reason() override {
    return OperationResult::Unsupported;
  }

  void emit_frame(std::vector<std::uint8_t> frame) {
    if (record_->observer) {
      record_->observer->on_external_cec_frame(id_, frame);
    }
  }

 private:
  DeviceId id_;
  DeviceSessionConfiguration configuration_;
  std::shared_ptr<SessionRecord> record_;
  std::shared_ptr<FactoryState> factory_state_;
};

class FakeFactory final : public DeviceSessionFactory {
 public:
  explicit FakeFactory(std::shared_ptr<FactoryState> state)
      : state_(std::move(state)) {}

  std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device,
      const DeviceSessionConfiguration& configuration) override {
    created_configurations.push_back(configuration);
    if (!configuration.valid()) return {};
    auto record = std::make_shared<SessionRecord>();
    state_->records.push_back(record);
    auto session =
        std::make_unique<FakeSession>(device.id, configuration, record, state_);
    last_session = session.get();
    return session;
  }

  std::vector<DeviceSessionConfiguration> created_configurations;
  FakeSession* last_session{};

 private:
  std::shared_ptr<FactoryState> state_;
};

class Observer final : public MateFrontendObserver {
 public:
  void on_frontend_status_changed(const FrontendStatus& status) override {
    std::lock_guard lock(mutex);
    last_status = status;
    ++status_count;
  }

  void on_frontend_cec_event(const FrontendCecEvent& event) override {
    std::lock_guard lock(mutex);
    last_event = event;
    ++event_count;
  }

  void on_frontend_action_completed(FrontendActionRequestId request,
                                    const FrontendActionResult& result) override {
    std::lock_guard lock(mutex);
    last_request = request;
    last_action = result;
    ++action_count;
  }

  std::mutex mutex;
  FrontendStatus last_status;
  FrontendCecEvent last_event;
  FrontendActionResult last_action;
  FrontendActionRequestId last_request{};
  int status_count{};
  int event_count{};
  int action_count{};
};

DiscoveredDevice TestDevice() {
  return {DeviceId("macos-usb-serial-v1:31323334"),
          PlatformDeviceLocator("opaque-registry-id"),
          "CEC Injector"};
}

MacInProcessMateFrontendBackendDependencies MakeDependencies(
    std::shared_ptr<BackendState> state, std::shared_ptr<FactoryState> factory_state,
    std::function<void()>* refresh_slot = nullptr) {
  MacInProcessMateFrontendBackendDependencies dependencies;
  dependencies.host = std::make_unique<FakeHostPlatform>(state);
  dependencies.session_factory = std::make_unique<FakeFactory>(factory_state);
  dependencies.capabilities = {.configuration = false,
                               .remote_input = false,
                               .remote_wake = false,
                               .tv_power_actions = true,
                               .cec_monitor = true};
  dependencies.configuration = {1, production_runtime_defaults()};
  dependencies.install_refresh_sources =
      [refresh_slot](std::function<void()> refresh) {
        if (refresh_slot) *refresh_slot = std::move(refresh);
        return std::vector<std::unique_ptr<MacFrontendRefreshSubscription>>{};
      };
  return dependencies;
}

}  // namespace

int main() {
  {
    auto state = std::make_shared<BackendState>();
    auto factory_state = std::make_shared<FactoryState>();
    MacInProcessMateFrontendBackend backend(
        MakeDependencies(state, factory_state));
    const auto capabilities = backend.capabilities();
    Check(!capabilities.configuration, "macOS backend keeps configuration honest");
    Check(!capabilities.remote_input, "macOS backend does not claim remote input");
    Check(!capabilities.remote_wake, "macOS backend does not claim remote wake");
    Check(capabilities.tv_power_actions, "manual TV commands exposed");
    Check(capabilities.cec_monitor, "CEC monitor exposed");
    Check(WaitFor([&] { return backend.status().service_available; }),
          "backend starts without hardware");
    Check(!backend.status().selected_device.has_value(),
          "no device stays disconnected");
    Check(backend.request_action(FrontendAction::MakeActiveSource) == 0,
          "action blocked without connected session");
  }

  {
    auto state = std::make_shared<BackendState>();
    {
      std::lock_guard lock(state->mutex);
      state->devices = {TestDevice()};
      state->physical_address = {PhysicalAddressStatus::Available, 0x1000};
      state->display = {"Philips UHDTV", true, 256};
    }
    auto factory_state = std::make_shared<FactoryState>();
    std::function<void()> refresh;
    MacInProcessMateFrontendBackend backend(
        MakeDependencies(state, factory_state, &refresh));
    Observer observer;
    backend.set_observer(&observer);
    Check(WaitFor([&] {
            return backend.status().session_state == SessionState::Connected;
          }),
          "device and PA produce a connected session");
    Check(factory_state->records.size() == 1,
          "initial connect produces one session record");
    const auto status = backend.status();
    Check(status.selected_device && *status.selected_device == TestDevice().id,
          "selected device projected");
    Check(status.physical_address == std::optional<std::uint16_t>(0x1000),
          "physical address projected");
    Check(status.logical_address == std::optional<std::uint8_t>(4),
          "logical address projected");
    Check(status.display_name == "Philips UHDTV", "display summary projected");

    const auto request =
        backend.request_action(FrontendAction::MakeActiveSource);
    Check(request != 0, "connected session accepts manual action");
    Check(WaitFor([&] {
            std::lock_guard lock(observer.mutex);
            return observer.action_count > 0;
          }),
          "manual action completion delivered");
    {
      std::lock_guard lock(observer.mutex);
      Check(observer.last_action.action == FrontendAction::MakeActiveSource,
            "action identity preserved");
      Check(observer.last_action.result == OperationResult::Success,
            "action result projected");
    }

    factory_state->records.back()->observer->on_external_cec_frame(
        TestDevice().id, std::vector<std::uint8_t>{0x0f, 0x36});
    Check(WaitFor([&] {
            std::lock_guard lock(observer.mutex);
            return observer.event_count > 0;
          }),
          "CEC monitor event delivered");

    {
      std::lock_guard lock(state->mutex);
      state->devices.clear();
    }
    refresh();
    Check(WaitFor([&] {
            const auto current = backend.status();
            return !current.selected_device.has_value() &&
                   current.session_state == SessionState::Disconnected;
          }),
          "device loss clears active session");

    {
      std::lock_guard lock(state->mutex);
      state->devices = {TestDevice()};
    }
    refresh();
    const auto reconnected = WaitFor([&] {
      return backend.status().session_state == SessionState::Connected &&
             factory_state->records.size() == 2;
    });
    if (!reconnected) {
      const auto current = backend.status();
      std::cerr << "reconnect diagnostic: service_available="
                << current.service_available
                << " selected=" << (current.selected_device ? "yes" : "no")
                << " injector_connected=" << current.injector_connected
                << " session_state=" << static_cast<int>(current.session_state)
                << " records=" << factory_state->records.size() << '\n';
    }
    Check(reconnected, "refresh reconnects after injector return");
    backend.set_observer(nullptr);
  }

  {
    auto state = std::make_shared<BackendState>();
    {
      std::lock_guard lock(state->mutex);
      state->devices = {TestDevice()};
      state->physical_address = {PhysicalAddressStatus::Unavailable,
                                 std::nullopt};
      state->display = {"Philips UHDTV", true, 256};
    }
    auto factory_state = std::make_shared<FactoryState>();
    MacInProcessMateFrontendBackend backend(
        MakeDependencies(state, factory_state));
    Check(WaitFor([&] {
            return backend.status().physical_address_status ==
                   PhysicalAddressStatus::Unavailable;
          }),
          "PA status projected without connect");
    Check(backend.status().session_state != SessionState::Connected,
          "session not created when PA unavailable");
  }

  return 0;
}

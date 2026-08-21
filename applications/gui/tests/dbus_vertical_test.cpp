#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QElapsedTimer>
#include <QThread>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>

#include "cec_injector/mate/frontend_linux/linux_dbus_frontend_service.h"
#include "cec_injector/mate/gui/daemon_mate_frontend_backend.h"
#include "cec_injector/mate/gui/main_window.h"

namespace {
using namespace cec_injector::mate;
using namespace cec_injector::mate::frontend;
using namespace cec_injector::mate::frontend_linux;
using namespace cec_injector::mate::gui;

void Check(bool value, const char* message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}
template <class Predicate> bool Wait(Predicate predicate, int timeout = 3000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  return predicate();
}

class Session final : public DeviceSession {
 public:
  explicit Session(DeviceId id) : id_(std::move(id)) {}
  const DeviceId& device_id() const noexcept override { return id_; }
  SessionState state() const noexcept override { return state_; }
  void set_observer(DeviceSessionObserver* value) override { observer_ = value; }
  OperationResult connect() override {
    state_ = SessionState::Connected;
    if (observer_) observer_->on_session_state_changed(id_, state_);
    return OperationResult::Success;
  }
  OperationResult disconnect() override { state_ = SessionState::Disconnected; return OperationResult::Success; }
  OperationResult refresh_state() override { return OperationResult::Success; }
  DeviceStateSnapshot snapshot() const override {
    return {{true, true}, {{0x1000}, {4}}, {true, true, true, false}};
  }
  OperationResult power_on_tv() override { return OperationResult::Success; }
  OperationResult standby_tv() override { return OperationResult::Success; }
  OperationResult make_active_source() override { return OperationResult::Success; }
  WakeState query_wake_state() override { return {true, true, true, false}; }
  OperationResult configure_wake(std::uint16_t) override { return OperationResult::Success; }
  OperationResult arm_wake() override { return OperationResult::Success; }
  OperationResult disarm_wake() override { return OperationResult::Success; }
  WakeReason read_last_wake_reason() override { return {OperationResult::Success, {}}; }
  OperationResult clear_last_wake_reason() override { return OperationResult::Success; }
 private:
  DeviceId id_;
  SessionState state_{SessionState::Discovered};
  DeviceSessionObserver* observer_{};
};
class Factory final : public DeviceSessionFactory {
 public:
  std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device,
      const DeviceSessionConfiguration&) override {
    return std::make_unique<Session>(device.id);
  }
};
class Host final : public HostPlatform {
 public:
  void set_lifecycle_observer(HostLifecycleObserver*) override {}
  std::vector<DiscoveredDevice> discovered_devices() override {
    return {{DeviceId("synthetic-ipc-device"), PlatformDeviceLocator("test"),
             "Synthetic IPC Device"}};
  }
  PhysicalAddressResolution resolve_physical_address() override {
    return {PhysicalAddressStatus::Available, 0x1000};
  }
  OperationResult request_system_suspend() override { return OperationResult::Success; }
  HostPowerState host_power_state() const noexcept override { return HostPowerState::Running; }
  AuthorizationState suspend_authorization() const override { return AuthorizationState::Authorized; }
  AuthorizationState remote_wake_authorization(const DeviceId&) const override { return AuthorizationState::Authorized; }
  OperationResult emit_remote_action(SemanticRemoteAction, RemoteActionState) override { return OperationResult::Success; }
  OperationResult reset_remote_input() override { return OperationResult::Success; }
  OperationResult apply_remote_input_configuration(const RemoteInputConfiguration&) override { return OperationResult::Success; }
};
class Observer final : public MateFrontendObserver {
 public:
  void on_frontend_status_changed(const FrontendStatus& value) override { status = value; ++statuses; }
  void on_frontend_cec_event(const FrontendCecEvent& value) override { event = value; ++events; }
  void on_frontend_action_completed(FrontendActionRequestId,
      const FrontendActionResult& value) override { action = value; ++actions; }
  void on_frontend_configuration_changed(const FrontendConfigurationSnapshot& value) override { configuration = value; ++configurations; }
  void on_frontend_configuration_completed(FrontendConfigurationRequestId,
      const FrontendConfigurationResult& value) override { config_result = value; ++config_completions; }
  FrontendStatus status;
  FrontendConfigurationSnapshot configuration;
  FrontendActionResult action;
  FrontendCecEvent event;
  FrontendConfigurationResult config_result;
  int statuses{}, events{}, actions{}, configurations{}, config_completions{};
};
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const auto* address = std::getenv("MATE_TEST_BUS_ADDRESS");
  Check(address, "private bus address");
  const auto directory = std::filesystem::temp_directory_path() /
      ("mate-dbus-vertical-" + std::to_string(::getpid()));
  std::filesystem::create_directory(directory);
  const auto config_path = directory / "config.conf";
  auto initial = production_runtime_defaults();
  std::string error;
  Check(persist_runtime_configuration_atomic(config_path, initial, error), "seed config");

  Host host; Factory factory;
  MateService service(host, factory, {initial.policy, initial.remote});
  Check(service.start() == OperationResult::Success, "service start");
  service.wait_idle();
  RuntimeConfigurationController controller(service,
      std::make_unique<FileRuntimeConfigurationStore>(config_path), initial);
  LinuxDbusFrontendService server(service, controller, true, address);
  Check(server.start() == OperationResult::Success, "D-Bus server start");

  auto connection = QDBusConnection::connectToBus(address, "mate-vertical-client");
  Check(connection.isConnected(), "client bus connection");
  auto second_connection =
      QDBusConnection::connectToBus(address, "mate-vertical-second-client");
  Check(second_connection.isConnected(), "second client bus connection");
  DaemonMateFrontendBackend backend(connection);
  Observer observer; backend.set_observer(&observer);
  DaemonMateFrontendBackend second_backend(second_connection);
  Observer second_observer; second_backend.set_observer(&second_observer);
  Check(Wait([&] { return observer.status.service_available && observer.status.injector_connected; }), "initial real status");
  Check(observer.status.physical_address == 0x1000 && observer.status.logical_address == 4, "PA and logical address");

  auto changed = backend.configuration();
  const auto stale_revision = changed.revision;
  changed.configuration.policy.power_on_resume = false;
  Check(backend.request_configuration(changed.revision, changed.configuration) != 0, "configuration request");
  Check(Wait([&] { return observer.config_completions > 0; }), "configuration completion");
  Check(observer.config_result.result == OperationResult::Success, "configuration success");
  Check(Wait([&] {
          return second_observer.configuration.configuration.policy.power_on_resume == false;
        }), "second client receives configuration update");
  auto stale = second_backend.configuration().configuration;
  stale.policy.power_on_boot = false;
  Check(second_backend.request_configuration(stale_revision, stale) != 0,
        "stale client request sent");
  Check(Wait([&] { return second_observer.config_completions > 0; }) &&
            second_observer.config_result.result == OperationResult::Rejected,
        "stale concurrent write rejected by revision");
  const auto loaded = load_runtime_configuration(config_path);
  Check(loaded.ok() && !loaded.configuration.policy.power_on_resume, "configuration persisted");

  Check(backend.request_action(FrontendAction::PowerOnTv) != 0, "manual action accepted");
  Check(Wait([&] { return observer.actions == 1; }), "manual action completion");
  Check(observer.action.result == OperationResult::Success, "manual action result");
  Check(Wait([&] { return observer.events > 0; }) &&
            observer.event.direction == CecDirection::Transmitted,
        "structured live CEC event");

  QDBusInterface raw(kMateBusName, kMateObjectPath, kMateInterface, connection);
  Check(raw.call("RequestAction", "raw_opcode").type() == QDBusMessage::ErrorMessage,
        "malformed action rejected");
  Check(raw.call("SetConfiguration", QVariant::fromValue<qulonglong>(
                     backend.configuration().revision), QVariantMap{})
                .type() == QDBusMessage::ErrorMessage,
        "incomplete configuration rejected");

  server.stop();
  service.shutdown();
  Check(Wait([&] { return !observer.status.service_available; }), "service loss");
  const auto reloaded = load_runtime_configuration(config_path);
  Check(reloaded.ok(), "reload persisted configuration");
  Host host2; Factory factory2;
  MateService service2(host2, factory2,
                       {reloaded.configuration.policy,
                        reloaded.configuration.remote});
  Check(service2.start() == OperationResult::Success, "daemon restart");
  service2.wait_idle();
  RuntimeConfigurationController controller2(
      service2, std::make_unique<FileRuntimeConfigurationStore>(config_path),
      reloaded.configuration);
  LinuxDbusFrontendService restarted(service2, controller2, true, address);
  Check(restarted.start() == OperationResult::Success, "D-Bus service restart");
  Check(Wait([&] { return observer.status.service_available; }), "service recovery");
  Check(backend.configuration().configuration.policy.power_on_resume == false,
        "fresh configuration after recovery");

  auto gui_connection =
      QDBusConnection::connectToBus(address, "mate-vertical-gui-client");
  Check(gui_connection.isConnected(), "GUI client bus connection");
  MainWindow window(
      std::make_unique<DaemonMateFrontendBackend>(gui_connection));
  window.show();
  QCoreApplication::processEvents();
  Check(window.isVisible(), "real GUI constructs against daemon backend");
  window.close();
  backend.set_observer(nullptr);
  second_backend.set_observer(nullptr);
  restarted.stop();
  service2.shutdown();
  QDBusConnection::disconnectFromBus("mate-vertical-client");
  QDBusConnection::disconnectFromBus("mate-vertical-second-client");
  QDBusConnection::disconnectFromBus("mate-vertical-gui-client");
  std::filesystem::remove_all(directory);
}

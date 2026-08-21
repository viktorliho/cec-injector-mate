#include "cec_injector/mate/gui/daemon_mate_frontend_backend.h"

#include <QDBusInterface>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusReply>

#include "cec_injector/mate/frontend_linux/linux_dbus_frontend_service.h"

namespace cec_injector::mate::gui {
namespace {
using namespace frontend;
constexpr auto kName = frontend_linux::kMateBusName;
constexpr auto kPath = frontend_linux::kMateObjectPath;
constexpr auto kInterface = frontend_linux::kMateInterface;

SessionState Session(const QString& value) {
  if (value == "connecting") return SessionState::Connecting;
  if (value == "connected") return SessionState::Connected;
  if (value == "degraded") return SessionState::Degraded;
  if (value == "disconnecting") return SessionState::Disconnecting;
  if (value == "discovered") return SessionState::Discovered;
  return SessionState::Disconnected;
}
OperationResult Result(const QString& value) {
  if (value == "success") return OperationResult::Success;
  if (value == "unsupported") return OperationResult::Unsupported;
  if (value == "invalid_state") return OperationResult::InvalidState;
  if (value == "invalid_configuration") return OperationResult::InvalidConfiguration;
  if (value == "rejected") return OperationResult::Rejected;
  if (value == "timeout") return OperationResult::Timeout;
  if (value == "disconnected") return OperationResult::Disconnected;
  if (value == "malformed_response") return OperationResult::MalformedResponse;
  if (value == "permission_denied") return OperationResult::PermissionDenied;
  return OperationResult::InternalError;
}
FrontendAction Action(const QString& value) {
  if (value == "make_active_source") return FrontendAction::MakeActiveSource;
  if (value == "standby_tv") return FrontendAction::StandbyTv;
  return FrontendAction::PowerOnTv;
}
QString ActionName(FrontendAction value) {
  switch (value) {
    case FrontendAction::PowerOnTv: return "power_on_tv";
    case FrontendAction::MakeActiveSource: return "make_active_source";
    case FrontendAction::StandbyTv: return "standby_tv";
  }
  return {};
}
FrontendStatus DecodeStatus(const QVariantMap& map) {
  FrontendStatus value;
  value.service_available = map.value("service_available").toBool();
  value.injector_connected = map.value("injector_connected").toBool();
  const auto id = map.value("device_id").toString();
  if (!id.isEmpty()) value.selected_device = DeviceId(id.toStdString());
  value.device_display_name = map.value("device_name").toString().toStdString();
  value.display_name = map.value("display_name").toString().toStdString();
  value.session_state = Session(map.value("session_state").toString());
  if (map.value("physical_address_available").toBool()) {
    value.physical_address_status = PhysicalAddressStatus::Available;
    value.physical_address = static_cast<std::uint16_t>(map.value("physical_address").toUInt());
  }
  if (map.value("logical_address_available").toBool())
    value.logical_address = static_cast<std::uint8_t>(map.value("logical_address").toUInt());
  value.wake = {.supported = map.value("wake_supported").toBool(),
                .configured = map.value("wake_configured").toBool(),
                .armed = map.value("wake_armed").toBool(),
                .reason_available = map.value("wake_reason_available").toBool()};
  value.edid_available = map.value("edid_available").toBool();
  value.edid_byte_count = map.value("edid_byte_count").toULongLong();
  return value;
}
FrontendConfigurationSnapshot DecodeConfiguration(const QVariantMap& map) {
  FrontendConfigurationSnapshot value;
  value.revision = map.value("revision").toULongLong();
  auto& p = value.configuration.policy;
  p.power_on_boot = map.value("power_on_boot").toBool();
  p.power_on_resume = map.value("power_on_resume").toBool();
  p.standby_on_suspend = map.value("standby_on_suspend").toBool();
  p.standby_on_shutdown = map.value("standby_on_shutdown").toBool();
  p.suspend_on_tv_standby = map.value("suspend_on_tv_standby").toBool();
  p.wake_host_on_tv_source_request = map.value("wake_host_on_tv_source_request").toBool();
  value.configuration.remote.enabled = map.value("remote_input_enabled").toBool();
  RemoteInputProfile profile;
  if (parse_remote_input_profile(map.value("remote_profile").toString().toStdString(), profile))
    value.configuration.remote.profile = profile;
  return value;
}
QVariantMap EncodeConfiguration(const FrontendConfiguration& value) {
  const auto& p = value.policy;
  return {{"power_on_boot", p.power_on_boot},
          {"power_on_resume", p.power_on_resume},
          {"standby_on_suspend", p.standby_on_suspend},
          {"standby_on_shutdown", p.standby_on_shutdown},
          {"suspend_on_tv_standby", p.suspend_on_tv_standby},
          {"wake_host_on_tv_source_request", p.wake_host_on_tv_source_request},
          {"remote_input_enabled", value.remote.enabled},
          {"remote_profile", QString::fromUtf8(remote_input_profile_name(value.remote.profile).data())}};
}
QVariantMap ReplyMap(const QDBusMessage& reply) {
  return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().empty()
             ? qdbus_cast<QVariantMap>(reply.arguments().front()) : QVariantMap{};
}
}  // namespace

DaemonMateFrontendBackend::DaemonMateFrontendBackend(QDBusConnection connection,
                                                       QObject* parent)
    : QObject(parent), connection_(std::move(connection)) {
  watcher_ = new QDBusServiceWatcher(kName, connection_,
      QDBusServiceWatcher::WatchForRegistration |
      QDBusServiceWatcher::WatchForUnregistration, this);
  connect(watcher_, &QDBusServiceWatcher::serviceRegistered, this,
          &DaemonMateFrontendBackend::serviceAppeared);
  connect(watcher_, &QDBusServiceWatcher::serviceUnregistered, this,
          &DaemonMateFrontendBackend::serviceVanished);
  connection_.connect(kName, kPath, kInterface, "StatusChanged", this,
                      SLOT(statusSignal(QVariantMap)));
  connection_.connect(kName, kPath, kInterface, "ConfigurationChanged", this,
                      SLOT(configurationSignal(QVariantMap)));
  if (!connection_.connect(kName, kPath, kInterface, "ActionCompleted", this,
                           SLOT(actionSignal(QVariantMap))))
    qWarning("Could not subscribe to Mate ActionCompleted");
  if (!connection_.connect(kName, kPath, kInterface, "CecEvent", this,
                           SLOT(cecSignal(QVariantMap))))
    qWarning("Could not subscribe to Mate CecEvent");
  if (connection_.interface() && connection_.interface()->isServiceRegistered(kName))
    serviceAppeared();
}
DaemonMateFrontendBackend::~DaemonMateFrontendBackend() { set_observer(nullptr); }

void DaemonMateFrontendBackend::set_observer(MateFrontendObserver* observer) {
  std::lock_guard lock(mutex_);
  observer_ = observer;
  if (observer_) {
    observer_->on_frontend_status_changed(status_);
    observer_->on_frontend_configuration_changed(configuration_);
  }
}
FrontendStatus DaemonMateFrontendBackend::status() const { std::lock_guard lock(mutex_); return status_; }
FrontendCapabilities DaemonMateFrontendBackend::capabilities() const { std::lock_guard lock(mutex_); return capabilities_; }
FrontendConfigurationSnapshot DaemonMateFrontendBackend::configuration() const { std::lock_guard lock(mutex_); return configuration_; }

void DaemonMateFrontendBackend::serviceAppeared() { refresh(); }
void DaemonMateFrontendBackend::serviceVanished() {
  std::lock_guard lock(mutex_);
  status_ = {};
  status_.service_available = false;
  capabilities_ = {};
  pending_actions_.clear();
  early_actions_.clear();
  if (observer_) observer_->on_frontend_status_changed(status_);
}
void DaemonMateFrontendBackend::refresh() {
  QDBusInterface remote(kName, kPath, kInterface, connection_);
  const auto status = DecodeStatus(ReplyMap(remote.call("GetStatus")));
  const auto configuration = DecodeConfiguration(ReplyMap(remote.call("GetConfiguration")));
  const auto capability_map = ReplyMap(remote.call("GetCapabilities"));
  FrontendCapabilities capabilities{capability_map.value("configuration").toBool(),
      capability_map.value("remote_input").toBool(), capability_map.value("remote_wake").toBool(),
      capability_map.value("tv_power_actions").toBool(), capability_map.value("cec_monitor").toBool()};
  std::lock_guard lock(mutex_);
  status_ = status; configuration_ = configuration; capabilities_ = capabilities;
  pending_actions_.clear();
  early_actions_.clear();
  if (observer_) { observer_->on_frontend_status_changed(status_); observer_->on_frontend_configuration_changed(configuration_); }
}
void DaemonMateFrontendBackend::statusSignal(const QVariantMap& map) {
  std::lock_guard lock(mutex_); status_ = DecodeStatus(map);
  if (observer_) observer_->on_frontend_status_changed(status_);
}
void DaemonMateFrontendBackend::configurationSignal(const QVariantMap& map) {
  std::lock_guard lock(mutex_); configuration_ = DecodeConfiguration(map);
  if (observer_) observer_->on_frontend_configuration_changed(configuration_);
}
FrontendConfigurationRequestId DaemonMateFrontendBackend::request_configuration(
    std::uint64_t base_revision, const FrontendConfiguration& value) {
  std::lock_guard lock(mutex_);
  if (!status_.service_available || !capabilities_.configuration) return 0;
  const auto request = next_configuration_request_++;
  QDBusInterface remote(kName, kPath, kInterface, connection_);
  auto* watcher = new QDBusPendingCallWatcher(remote.asyncCall(
      "SetConfiguration", QVariant::fromValue<qulonglong>(base_revision),
      EncodeConfiguration(value)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, watcher, request] {
    QDBusPendingReply<QString, qulonglong, QString> reply = *watcher;
    FrontendConfigurationResult result;
    if (reply.isError()) {
      result = {OperationResult::PermissionDenied, configuration(),
                reply.error().message().toStdString()};
    } else {
      result = {Result(reply.argumentAt<0>()), configuration(),
                reply.argumentAt<2>().toStdString()};
      result.authoritative.revision = reply.argumentAt<1>();
    }
    MateFrontendObserver* observer = nullptr;
    { std::lock_guard lock(mutex_); observer = observer_; }
    if (observer) observer->on_frontend_configuration_completed(request, result);
    watcher->deleteLater();
  });
  return request;
}
FrontendActionRequestId DaemonMateFrontendBackend::request_action(FrontendAction action) {
  {
    std::lock_guard lock(mutex_);
    if (!status_.service_available || !capabilities_.tv_power_actions) return 0;
  }
  QDBusInterface remote(kName, kPath, kInterface, connection_);
  remote.setTimeout(2000);
  QDBusReply<qlonglong> reply = remote.call("RequestAction", ActionName(action));
  if (!reply.isValid() || reply.value() == 0) return 0;
  std::lock_guard lock(mutex_);
  pending_actions_.insert(reply.value());
  if (const auto early = early_actions_.find(reply.value());
      early != early_actions_.end()) {
    pending_actions_.erase(reply.value());
    if (observer_) observer_->on_frontend_action_completed(reply.value(), early->second);
    early_actions_.erase(early);
  }
  return reply.value();
}
void DaemonMateFrontendBackend::actionSignal(const QVariantMap& value) {
  const auto request = value.value("request_id").toLongLong();
  std::lock_guard lock(mutex_);
  frontend::FrontendActionResult completed{
      Action(value.value("action").toString()),
      Result(value.value("result").toString()),
      std::chrono::milliseconds(value.value("duration_ms").toLongLong()),
      value.value("message").toString().toStdString()};
  if (!pending_actions_.erase(request)) {
    early_actions_[request] = std::move(completed);
    if (early_actions_.size() > 64) early_actions_.erase(early_actions_.begin());
    return;
  }
  if (observer_) observer_->on_frontend_action_completed(request, completed);
}
void DaemonMateFrontendBackend::cecSignal(const QVariantMap& value) {
  FrontendCecEvent event;
  event.timestamp = std::chrono::system_clock::time_point(
      std::chrono::microseconds(value.value("timestamp_us").toLongLong()));
  event.direction = value.value("direction").toString() == "tx"
                        ? CecDirection::Transmitted : CecDirection::Received;
  event.source = static_cast<std::uint8_t>(value.value("source").toUInt());
  event.destination = static_cast<std::uint8_t>(value.value("destination").toUInt());
  event.opcode = static_cast<std::uint8_t>(value.value("opcode").toUInt());
  const auto payload = value.value("payload").toByteArray();
  event.parameters.assign(payload.begin(), payload.end());
  const auto semantic = value.value("semantic").toString();
  if (!semantic.isEmpty()) event.semantic = semantic.toStdString();
  std::lock_guard lock(mutex_);
  if (observer_) observer_->on_frontend_cec_event(event);
}

}  // namespace cec_injector::mate::gui

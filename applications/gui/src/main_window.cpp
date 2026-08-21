#include "cec_injector/mate/gui/main_window.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>

#include "cec_injector/mate/gui/backend_qt_bridge.h"
#include "cec_injector/mate/gui/cec_monitor_model.h"

namespace cec_injector::mate::gui {
namespace {

QString PhysicalAddress(const std::optional<std::uint16_t>& address) {
  if (!address) return QStringLiteral("Unavailable");
  return QStringLiteral("%1.%2.%3.%4")
      .arg((*address >> 12) & 0xf)
      .arg((*address >> 8) & 0xf)
      .arg((*address >> 4) & 0xf)
      .arg(*address & 0xf);
}

QString SessionName(SessionState state) {
  switch (state) {
    case SessionState::Discovered: return QStringLiteral("Discovered");
    case SessionState::Connecting: return QStringLiteral("Connecting");
    case SessionState::Connected: return QStringLiteral("Connected");
    case SessionState::Degraded: return QStringLiteral("Degraded");
    case SessionState::Disconnecting: return QStringLiteral("Disconnecting");
    case SessionState::Disconnected: return QStringLiteral("Disconnected");
  }
  return QStringLiteral("Unknown");
}

QString ResultName(OperationResult result) {
  switch (result) {
    case OperationResult::Success: return QStringLiteral("Success");
    case OperationResult::Unsupported: return QStringLiteral("Unsupported");
    case OperationResult::InvalidState: return QStringLiteral("Invalid state");
    case OperationResult::InvalidConfiguration:
      return QStringLiteral("Invalid configuration");
    case OperationResult::Rejected: return QStringLiteral("Rejected");
    case OperationResult::Timeout: return QStringLiteral("Timeout");
    case OperationResult::Disconnected: return QStringLiteral("Disconnected");
    case OperationResult::MalformedResponse:
      return QStringLiteral("Malformed response");
    case OperationResult::PermissionDenied:
      return QStringLiteral("Permission denied");
    case OperationResult::InternalError: return QStringLiteral("Internal error");
  }
  return QStringLiteral("Unknown");
}

QLabel* ValueLabel(const char* name) {
  auto* label = new QLabel;
  label->setObjectName(QString::fromLatin1(name));
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}

}  // namespace

MainWindow::MainWindow(std::unique_ptr<frontend::MateFrontendBackend> backend,
                       QWidget* parent)
    : QMainWindow(parent), backend_(std::move(backend)) {
  setWindowTitle(QStringLiteral("CEC Injector Mate"));
  resize(940, 620);

  auto* central = new QWidget;
  auto* layout = new QHBoxLayout(central);
  navigation_ = new QListWidget;
  navigation_->setObjectName(QStringLiteral("navigation"));
  navigation_->setMaximumWidth(210);
  pages_ = new QStackedWidget;
  layout->addWidget(navigation_);
  layout->addWidget(pages_, 1);
  setCentralWidget(central);

  addPage(QStringLiteral("Overview"), createOverviewPage(), false);
  addPage(QStringLiteral("Behavior"), createBehaviorPage(), false);
  addPage(QStringLiteral("Remote"), createRemotePage(), false);
  addPage(QStringLiteral("Advanced / Diagnostics"), createSettingsPage(), false);
  addPage(QStringLiteral("Device"), createDevicePage(), true);
  addPage(QStringLiteral("Display / EDID"), createDisplayPage(), true);
  addPage(QStringLiteral("CEC Monitor"), createMonitorPage(), true);
  addPage(QStringLiteral("Commands"), createCommandsPage(), true);
  addPage(QStringLiteral("Tests"), createTestsPage(), true);

  connect(navigation_, &QListWidget::currentRowChanged, pages_,
          &QStackedWidget::setCurrentIndex);
  navigation_->setCurrentRow(0);
  setLabModeEnabled(false);

  bridge_ = std::make_unique<BackendQtBridge>(*backend_);
  connect(bridge_.get(), &BackendQtBridge::statusChanged, this,
          &MainWindow::renderStatus);
  connect(bridge_.get(), &BackendQtBridge::cecEventReceived, monitor_model_,
          &CecMonitorModel::appendEvent);
  connect(bridge_.get(), &BackendQtBridge::actionCompleted, this,
          [this](frontend::FrontendActionRequestId,
                 const frontend::FrontendActionResult& value) {
            if (!action_result_) return;
            QPushButton* button = value.action == frontend::FrontendAction::PowerOnTv
                                      ? power_on_action_
                                  : value.action == frontend::FrontendAction::MakeActiveSource
                                      ? active_source_action_ : standby_action_;
            if (button) button->setEnabled(backend_->status().service_available);
            action_result_->setText(QStringLiteral("%1 — %2 ms — %3")
                                        .arg(ResultName(value.result))
                                        .arg(value.duration.count())
                                        .arg(QString::fromStdString(value.message)));
          });
  connect(bridge_.get(), &BackendQtBridge::configurationChanged, this,
          &MainWindow::renderConfiguration);
  connect(bridge_.get(), &BackendQtBridge::configurationCompleted, this,
          [this](frontend::FrontendConfigurationRequestId,
                 const frontend::FrontendConfigurationResult& result) {
            configuration_pending_ = false;
            if (result.result == OperationResult::Success) {
              configuration_result_->setText(QStringLiteral("Configuration applied"));
            } else {
              configuration_result_->setText(
                  QStringLiteral("Apply failed: %1").arg(
                      QString::fromStdString(result.message)));
            }
            markConfigurationDirty();
          });
  renderConfiguration(backend_->configuration());
  renderStatus(backend_->status());
}

MainWindow::~MainWindow() = default;

bool MainWindow::labModeEnabled() const noexcept { return lab_mode_; }

int MainWindow::visibleLabPageCount() const {
  int count = 0;
  for (const auto* item : lab_items_) {
    if (!item->isHidden()) ++count;
  }
  return count;
}

CecMonitorModel* MainWindow::cecMonitorModel() const noexcept {
  return monitor_model_;
}

void MainWindow::setLabModeEnabled(bool enabled) {
  lab_mode_ = enabled;
  for (auto* item : lab_items_) item->setHidden(!enabled);
  if (!enabled && navigation_->currentRow() >= 4) navigation_->setCurrentRow(0);
}

QWidget* MainWindow::createOverviewPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  auto* title = new QLabel(QStringLiteral("Overview"));
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 4);
  title_font.setBold(true);
  title->setFont(title_font);
  layout->addWidget(title);
  auto* form = new QFormLayout;
  connection_value_ = ValueLabel("overviewConnection");
  display_value_ = ValueLabel("overviewDisplay");
  pa_value_ = ValueLabel("overviewPhysicalAddress");
  session_value_ = ValueLabel("overviewSession");
  form->addRow(QStringLiteral("CEC Injector"), connection_value_);
  form->addRow(QStringLiteral("TV / display"), display_value_);
  form->addRow(QStringLiteral("Physical Address"), pa_value_);
  form->addRow(QStringLiteral("CEC session"), session_value_);
  layout->addLayout(form);
  layout->addStretch();
  return page;
}

QWidget* MainWindow::createBehaviorPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->addWidget(new QLabel(QStringLiteral("Behavior")));
  const auto add = [&](QCheckBox*& box, const char* text) {
    box = new QCheckBox(QString::fromUtf8(text));
    connect(box, &QCheckBox::toggled, this, &MainWindow::markConfigurationDirty);
    layout->addWidget(box);
  };
  add(power_on_boot_, "Turn TV on when computer boots");
  power_on_boot_->setObjectName(QStringLiteral("powerOnBoot"));
  add(power_on_resume_, "Turn TV on when computer wakes");
  add(standby_on_suspend_, "Put TV in standby when computer sleeps");
  add(standby_on_shutdown_, "Put TV in standby when computer shuts down");
  add(suspend_on_tv_standby_, "Suspend computer when TV enters standby");
  add(wake_on_source_, "Allow TV input selection to wake computer");
  auto* buttons = new QHBoxLayout;
  apply_configuration_ = new QPushButton(QStringLiteral("Apply"));
  reset_configuration_ = new QPushButton(QStringLiteral("Reset"));
  apply_configuration_->setObjectName(QStringLiteral("applyConfiguration"));
  reset_configuration_->setObjectName(QStringLiteral("resetConfiguration"));
  configuration_result_ = new QLabel;
  connect(apply_configuration_, &QPushButton::clicked, this,
          &MainWindow::applyConfiguration);
  connect(reset_configuration_, &QPushButton::clicked, this,
          &MainWindow::resetConfiguration);
  buttons->addWidget(apply_configuration_);
  buttons->addWidget(reset_configuration_);
  buttons->addWidget(configuration_result_, 1);
  layout->addLayout(buttons);
  layout->addStretch();
  return page;
}

QWidget* MainWindow::createRemotePage() {
  auto* page = new QWidget;
  auto* layout = new QFormLayout(page);
  remote_enabled_ = new QCheckBox;
  connect(remote_enabled_, &QCheckBox::toggled, this,
          &MainWindow::markConfigurationDirty);
  layout->addRow(QStringLiteral("Use TV remote for navigation"), remote_enabled_);
  remote_profile_ = new QLabel;
  layout->addRow(QStringLiteral("Current remote profile"),
                 remote_profile_);
  return page;
}

QWidget* MainWindow::createSettingsPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->addWidget(new QLabel(QStringLiteral("Advanced / Diagnostics")));
  auto* lab = new QCheckBox(QStringLiteral("Enable Lab Mode"));
  lab->setObjectName(QStringLiteral("labModeToggle"));
  connect(lab, &QCheckBox::toggled, this, &MainWindow::setLabModeEnabled);
  layout->addWidget(lab);
  layout->addWidget(new QLabel(QStringLiteral(
      "Lab Mode reveals engineering views without changing the backend or "
      "hardware ownership model.")));
  layout->addStretch();
  return page;
}

QWidget* MainWindow::createDevicePage() {
  auto* page = new QWidget;
  auto* form = new QFormLayout(page);
  device_id_value_ = ValueLabel("labDeviceId");
  logical_value_ = ValueLabel("labLogicalAddress");
  wake_value_ = ValueLabel("labWakeState");
  backend_status_value_ = ValueLabel("labBackendStatus");
  form->addRow(QStringLiteral("DeviceId"), device_id_value_);
  form->addRow(QStringLiteral("Session state"), new QLabel(QStringLiteral("See Overview")));
  form->addRow(QStringLiteral("Logical address"), logical_value_);
  form->addRow(QStringLiteral("Backend status"), backend_status_value_);
  form->addRow(QStringLiteral("Wake support / state"), wake_value_);
  return page;
}

QWidget* MainWindow::createDisplayPage() {
  auto* page = new QWidget;
  auto* form = new QFormLayout(page);
  lab_display_value_ = ValueLabel("labDisplayIdentity");
  lab_pa_status_value_ = ValueLabel("labPaStatus");
  lab_pa_value_ = ValueLabel("labPhysicalAddress");
  form->addRow(QStringLiteral("Display identity"), lab_display_value_);
  form->addRow(QStringLiteral("PA status"), lab_pa_status_value_);
  form->addRow(QStringLiteral("Physical Address"), lab_pa_value_);
  edid_value_ = ValueLabel("labEdidState");
  form->addRow(QStringLiteral("EDID"), edid_value_);
  return page;
}

QWidget* MainWindow::createMonitorPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  auto* view = new QTableView;
  view->setObjectName(QStringLiteral("cecMonitor"));
  monitor_model_ = new CecMonitorModel(view);
  view->setModel(monitor_model_);
  view->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(view);
  auto* clear = new QPushButton(QStringLiteral("Clear"));
  connect(clear, &QPushButton::clicked, monitor_model_, &CecMonitorModel::clear);
  layout->addWidget(clear, 0, Qt::AlignRight);
  return page;
}

QWidget* MainWindow::createCommandsPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  action_result_ = new QLabel(QStringLiteral("No action invoked"));
  action_result_->setObjectName(QStringLiteral("actionResult"));
  for (const auto [title, action] : {
           std::pair{QStringLiteral("Power On TV"), frontend::FrontendAction::PowerOnTv},
           std::pair{QStringLiteral("Make Active Source"), frontend::FrontendAction::MakeActiveSource},
           std::pair{QStringLiteral("Standby TV"), frontend::FrontendAction::StandbyTv},
       }) {
    auto* button = new QPushButton(title);
    if (action == frontend::FrontendAction::PowerOnTv) {
      button->setObjectName(QStringLiteral("powerOnAction"));
      power_on_action_ = button;
    } else if (action == frontend::FrontendAction::MakeActiveSource) {
      active_source_action_ = button;
    } else {
      standby_action_ = button;
    }
    connect(button, &QPushButton::clicked, this, [this, action, button] {
      button->setEnabled(false);
      action_result_->setText(QStringLiteral("Pending"));
      const auto request = backend_->request_action(action);
      if (request == 0) {
        button->setEnabled(true);
        action_result_->setText(QStringLiteral("Request rejected"));
      }
    });
    layout->addWidget(button);
  }
  layout->addWidget(action_result_);
  layout->addStretch();
  return page;
}

QWidget* MainWindow::createTestsPage() {
  auto* table = new QTableWidget;
  const std::vector<std::string> tests{
      "Injector discovery", "Physical Address", "CEC session", "CEC receive",
      "Active Source", "Power On", "Standby", "Wake Extension"};
  table->setColumnCount(3);
  table->setHorizontalHeaderLabels({"Test", "Status", "Detail"});
  table->setRowCount(static_cast<int>(tests.size()));
  for (int row = 0; row < static_cast<int>(tests.size()); ++row) {
    table->setItem(row, 0,
                   new QTableWidgetItem(QString::fromStdString(tests[row])));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("Not Run")));
    table->setItem(row, 2, new QTableWidgetItem);
  }
  table->horizontalHeader()->setStretchLastSection(true);
  return table;
}

void MainWindow::addPage(const QString& title, QWidget* page, bool lab_only) {
  auto* item = new QListWidgetItem(title, navigation_);
  pages_->addWidget(page);
  if (lab_only) lab_items_.push_back(item);
}

void MainWindow::renderStatus(const frontend::FrontendStatus& status) {
  const bool mutable_available = status.service_available;
  for (auto* control : {power_on_boot_, power_on_resume_, standby_on_suspend_,
                        standby_on_shutdown_, suspend_on_tv_standby_,
                        wake_on_source_, remote_enabled_}) {
    if (control) control->setEnabled(mutable_available);
  }
  for (auto* button : {power_on_action_, active_source_action_, standby_action_}) {
    if (button) button->setEnabled(mutable_available);
  }
  connection_value_->setText(
      !status.service_available ? QStringLiteral("Mate service unavailable")
      : !status.selected_device ? QStringLiteral("Daemon available — no Injector")
      : status.injector_connected ? QStringLiteral("Connected")
                                  : QStringLiteral("Injector present — connecting"));
  display_value_->setText(QString::fromStdString(status.display_name));
  pa_value_->setText(PhysicalAddress(status.physical_address));
  session_value_->setText(SessionName(status.session_state));
  device_id_value_->setText(
      status.selected_device
          ? QString::fromUtf8(status.selected_device->diagnostic_value())
          : QStringLiteral("None"));
  logical_value_->setText(status.logical_address
                              ? QStringLiteral("Playback 1 (%1)")
                                    .arg(*status.logical_address)
                              : QStringLiteral("Unavailable"));
  wake_value_->setText(status.wake.supported
                           ? (status.wake.armed ? QStringLiteral("Supported, armed")
                                               : QStringLiteral("Supported, not armed"))
                           : QStringLiteral("Unsupported"));
  backend_status_value_->setText(status.service_available
                                      ? QStringLiteral("Daemon available")
                                      : QStringLiteral("Service unavailable"));
  lab_display_value_->setText(QString::fromStdString(status.display_name));
  lab_pa_status_value_->setText(
      status.physical_address_status == PhysicalAddressStatus::Available
          ? QStringLiteral("Available")
      : status.physical_address_status == PhysicalAddressStatus::Ambiguous
          ? QStringLiteral("Ambiguous") : QStringLiteral("Unavailable"));
  lab_pa_value_->setText(PhysicalAddress(status.physical_address));
  edid_value_->setText(status.edid_available
                           ? QStringLiteral("Available, %1 bytes")
                                 .arg(status.edid_byte_count)
                           : QStringLiteral("Unavailable"));
  markConfigurationDirty();
}

void MainWindow::renderConfiguration(
    const frontend::FrontendConfigurationSnapshot& value) {
  if (configuration_dirty_ && !configuration_pending_) {
    if (value.revision != authoritative_configuration_.revision) {
      external_conflict_ = true;
      configuration_result_->setText(
          QStringLiteral("Configuration changed externally — Reset required"));
      markConfigurationDirty();
    }
    authoritative_configuration_ = value;
    return;
  }
  authoritative_configuration_ = value;
  const QSignalBlocker blocks[] = {QSignalBlocker(power_on_boot_),
      QSignalBlocker(power_on_resume_), QSignalBlocker(standby_on_suspend_),
      QSignalBlocker(standby_on_shutdown_), QSignalBlocker(suspend_on_tv_standby_),
      QSignalBlocker(wake_on_source_), QSignalBlocker(remote_enabled_)};
  (void)blocks;
  const auto& configuration = value.configuration;
  power_on_boot_->setChecked(configuration.policy.power_on_boot);
  power_on_resume_->setChecked(configuration.policy.power_on_resume);
  standby_on_suspend_->setChecked(configuration.policy.standby_on_suspend);
  standby_on_shutdown_->setChecked(configuration.policy.standby_on_shutdown);
  suspend_on_tv_standby_->setChecked(configuration.policy.suspend_on_tv_standby);
  wake_on_source_->setChecked(configuration.policy.wake_host_on_tv_source_request);
  remote_enabled_->setChecked(configuration.remote.enabled);
  remote_profile_->setText(configuration.remote.profile == RemoteInputProfile::SteamOsParity
                               ? QStringLiteral("SteamOS parity")
                               : QStringLiteral("Unknown"));
  configuration_dirty_ = false;
  external_conflict_ = false;
  markConfigurationDirty();
}

void MainWindow::markConfigurationDirty() {
  if (!power_on_boot_) return;
  const auto& value = authoritative_configuration_.configuration;
  configuration_dirty_ =
      power_on_boot_->isChecked() != value.policy.power_on_boot ||
      power_on_resume_->isChecked() != value.policy.power_on_resume ||
      standby_on_suspend_->isChecked() != value.policy.standby_on_suspend ||
      standby_on_shutdown_->isChecked() != value.policy.standby_on_shutdown ||
      suspend_on_tv_standby_->isChecked() != value.policy.suspend_on_tv_standby ||
      wake_on_source_->isChecked() != value.policy.wake_host_on_tv_source_request ||
      remote_enabled_->isChecked() != value.remote.enabled;
  const bool available = backend_->status().service_available;
  apply_configuration_->setEnabled(available && configuration_dirty_ &&
                                    !configuration_pending_ && !external_conflict_);
  reset_configuration_->setEnabled(configuration_dirty_ || external_conflict_);
}

void MainWindow::applyConfiguration() {
  auto value = authoritative_configuration_.configuration;
  value.policy.power_on_boot = power_on_boot_->isChecked();
  value.policy.power_on_resume = power_on_resume_->isChecked();
  value.policy.standby_on_suspend = standby_on_suspend_->isChecked();
  value.policy.standby_on_shutdown = standby_on_shutdown_->isChecked();
  value.policy.suspend_on_tv_standby = suspend_on_tv_standby_->isChecked();
  value.policy.wake_host_on_tv_source_request = wake_on_source_->isChecked();
  value.remote.enabled = remote_enabled_->isChecked();
  configuration_pending_ = true;
  configuration_result_->setText(QStringLiteral("Applying…"));
  markConfigurationDirty();
  const auto request = backend_->request_configuration(
      authoritative_configuration_.revision, value);
  if (request == 0) {
    configuration_pending_ = false;
    configuration_result_->setText(QStringLiteral("Mate service unavailable"));
    markConfigurationDirty();
    return;
  }
}

void MainWindow::resetConfiguration() {
  configuration_dirty_ = false;
  external_conflict_ = false;
  renderConfiguration(authoritative_configuration_);
  configuration_result_->clear();
}

}  // namespace cec_injector::mate::gui

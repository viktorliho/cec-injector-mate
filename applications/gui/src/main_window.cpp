#include "cec_injector/mate/gui/main_window.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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
            action_result_->setText(QStringLiteral("%1 — %2 ms — %3")
                                        .arg(ResultName(value.result))
                                        .arg(value.duration.count())
                                        .arg(QString::fromStdString(value.message)));
          });
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
  const auto configuration = backend_->configuration();
  struct Entry { const char* text; bool value; };
  for (const auto& entry : {
           Entry{"Turn TV on when computer boots", configuration.policy.power_on_boot},
           Entry{"Turn TV on when computer resumes", configuration.policy.power_on_resume},
           Entry{"Put TV in standby when computer suspends", configuration.policy.standby_on_suspend},
           Entry{"Put TV in standby when computer shuts down", configuration.policy.standby_on_shutdown},
           Entry{"Suspend computer when TV enters standby", configuration.policy.suspend_on_tv_standby},
           Entry{"Switch to this HDMI source when appropriate", true},
           Entry{"Remote Wake", configuration.policy.wake_host_on_tv_source_request},
       }) {
    auto* box = new QCheckBox(QString::fromUtf8(entry.text));
    box->setChecked(entry.value);
    box->setEnabled(backend_->capabilities().configuration);
    layout->addWidget(box);
  }
  layout->addWidget(new QLabel(
      QStringLiteral("Controls use synthetic configuration in this milestone.")));
  layout->addStretch();
  return page;
}

QWidget* MainWindow::createRemotePage() {
  auto* page = new QWidget;
  auto* layout = new QFormLayout(page);
  const auto configuration = backend_->configuration();
  auto* enabled = new QCheckBox;
  enabled->setChecked(configuration.remote_input_enabled);
  layout->addRow(QStringLiteral("TV remote input enabled"), enabled);
  layout->addRow(QStringLiteral("Current remote profile"),
                 new QLabel(QString::fromStdString(configuration.remote_profile)));
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
  form->addRow(QStringLiteral("DeviceId"), device_id_value_);
  form->addRow(QStringLiteral("Session state"), new QLabel(QStringLiteral("See Overview")));
  form->addRow(QStringLiteral("Logical address"), logical_value_);
  form->addRow(QStringLiteral("Backend status"), new QLabel(QStringLiteral("Synthetic in-process backend")));
  form->addRow(QStringLiteral("Wake support / state"), wake_value_);
  return page;
}

QWidget* MainWindow::createDisplayPage() {
  auto* page = new QWidget;
  auto* form = new QFormLayout(page);
  form->addRow(QStringLiteral("Display identity"), new QLabel(QStringLiteral("See Overview")));
  form->addRow(QStringLiteral("PA status"), new QLabel(QStringLiteral("Available")));
  form->addRow(QStringLiteral("Physical Address"), new QLabel(QStringLiteral("1.0.0.0")));
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
    }
    connect(button, &QPushButton::clicked, this, [this, action] {
      const auto request = backend_->request_action(action);
      if (request == 0) action_result_->setText(QStringLiteral("Request rejected"));
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
  connection_value_->setText(status.injector_connected ? QStringLiteral("Connected")
                                                       : QStringLiteral("Disconnected"));
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
  edid_value_->setText(status.edid_available
                           ? QStringLiteral("Available, %1 bytes")
                                 .arg(status.edid_byte_count)
                           : QStringLiteral("Unavailable"));
}

}  // namespace cec_injector::mate::gui

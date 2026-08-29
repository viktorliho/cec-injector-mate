#include "cec_injector/mate/gui/main_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
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

QString CaptureStateName(frontend::CompatibilityCaptureState state) {
  return QString::fromStdString(
      frontend::compatibility_capture_state_name(state));
}

frontend::CompatibilityCaptureApplicationInfo CaptureApplicationInfo() {
  frontend::CompatibilityCaptureApplicationInfo info;
  info.application_name = "CEC Injector Mate";
#if defined(CEC_INJECTOR_MATE_GUI_MACOS_IN_PROCESS)
  info.platform = "macOS";
#elif defined(CEC_INJECTOR_MATE_GUI_LINUX_DBUS)
  info.platform = "Linux";
#else
  info.platform = "Unknown";
#endif
#ifdef CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION
  info.version = CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION;
#else
  info.version = "unknown";
#endif
#ifdef CEC_INJECTOR_MATE_GUI_GIT_HASH
  info.git_revision = CEC_INJECTOR_MATE_GUI_GIT_HASH;
#else
  info.git_revision = "unknown";
#endif
  return info;
}

QString DefaultExportPath(const std::string& basename) {
  return QDir::current().filePath(QString::fromStdString(basename) +
                                  QStringLiteral(".md"));
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

  capture_timer_ = new QTimer(this);
  capture_timer_->setInterval(250);
  connect(capture_timer_, &QTimer::timeout, this,
          &MainWindow::renderCompatibilityCapture);
  capture_timer_->start();

  bridge_ = std::make_unique<BackendQtBridge>(*backend_);
  connect(bridge_.get(), &BackendQtBridge::statusChanged, this,
          [this](const frontend::FrontendStatus& status) {
            compatibility_capture_.record_status_transition(
                status, std::chrono::system_clock::now());
            renderStatus(status);
            renderCompatibilityCapture();
          });
  connect(bridge_.get(), &BackendQtBridge::cecEventReceived, this,
          [this](const frontend::FrontendCecEvent& event) {
            monitor_model_->appendEvent(event);
            compatibility_capture_.record_cec_event(event);
            renderCompatibilityCapture();
          });
  connect(bridge_.get(), &BackendQtBridge::actionCompleted, this,
          [this](frontend::FrontendActionRequestId,
                 const frontend::FrontendActionResult& value) {
            if (!action_result_) return;
            QPushButton* button = value.action == frontend::FrontendAction::PowerOnTv
                                      ? power_on_action_
                                  : value.action == frontend::FrontendAction::MakeActiveSource
                                      ? active_source_action_ : standby_action_;
            if (button) renderStatus(backend_->status());
            action_result_->setText(QStringLiteral("%1 — %2 ms — %3")
                                        .arg(ResultName(value.result))
                                        .arg(value.duration.count())
                                        .arg(QString::fromStdString(value.message)));
          });
  connect(bridge_.get(), &BackendQtBridge::actionCompleted, this,
          [this](frontend::FrontendActionRequestId request,
                 const frontend::FrontendActionResult& value) {
            compatibility_capture_.record_action_completed(
                request, value, std::chrono::system_clock::now());
            const auto pending =
                compatibility_capture_.latest_action_needing_physical_result();
            capture_pending_physical_request_ =
                pending ? std::optional{pending->request_id} : std::nullopt;
            renderCompatibilityCapture();
          });
  connect(bridge_.get(), &BackendQtBridge::configurationChanged, this,
          &MainWindow::renderConfiguration);
  connect(bridge_.get(), &BackendQtBridge::configurationCompleted, this,
          &MainWindow::completeConfiguration);
  renderConfiguration(backend_->configuration());
  renderStatus(backend_->status());
  renderCompatibilityCapture();
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
  power_on_resume_->setObjectName(QStringLiteral("powerOnResume"));
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
  configuration_result_->setObjectName(QStringLiteral("configurationResult"));
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
        renderCompatibilityCapture();
        return;
      }
      compatibility_capture_.record_action_requested(
          request, action, std::chrono::system_clock::now());
      renderCompatibilityCapture();
    });
    layout->addWidget(button);
  }
  layout->addWidget(action_result_);
  layout->addStretch();
  return page;
}

QWidget* MainWindow::createTestsPage() {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->addWidget(new QLabel(QStringLiteral("Compatibility Capture")));

  auto* summary = new QFormLayout;
  capture_state_value_ = ValueLabel("captureState");
  capture_elapsed_value_ = ValueLabel("captureElapsed");
  capture_event_count_value_ = ValueLabel("captureEventCount");
  capture_action_count_value_ = ValueLabel("captureActionCount");
  capture_transition_count_value_ = ValueLabel("captureStatusChangeCount");
  capture_marker_count_value_ = ValueLabel("captureMarkerCount");
  capture_note_count_value_ = ValueLabel("captureNoteCount");
  capture_dropped_count_value_ = ValueLabel("captureDroppedCount");
  summary->addRow(QStringLiteral("State"), capture_state_value_);
  summary->addRow(QStringLiteral("Elapsed"), capture_elapsed_value_);
  summary->addRow(QStringLiteral("CEC events"), capture_event_count_value_);
  summary->addRow(QStringLiteral("Action results"), capture_action_count_value_);
  summary->addRow(QStringLiteral("Status changes"),
                  capture_transition_count_value_);
  summary->addRow(QStringLiteral("Markers"), capture_marker_count_value_);
  summary->addRow(QStringLiteral("Notes"), capture_note_count_value_);
  summary->addRow(QStringLiteral("Dropped events"), capture_dropped_count_value_);
  layout->addLayout(summary);

  auto* controls = new QHBoxLayout;
  start_capture_action_ = new QPushButton(QStringLiteral("Start Capture"));
  start_capture_action_->setObjectName(QStringLiteral("startCapture"));
  stop_capture_action_ = new QPushButton(QStringLiteral("Stop Capture"));
  stop_capture_action_->setObjectName(QStringLiteral("stopCapture"));
  clear_capture_action_ = new QPushButton(QStringLiteral("Clear"));
  clear_capture_action_->setObjectName(QStringLiteral("clearCapture"));
  export_capture_action_ = new QPushButton(QStringLiteral("Export Report"));
  export_capture_action_->setObjectName(QStringLiteral("exportCapture"));
  connect(start_capture_action_, &QPushButton::clicked, this,
          &MainWindow::startCompatibilityCapture);
  connect(stop_capture_action_, &QPushButton::clicked, this,
          &MainWindow::stopCompatibilityCapture);
  connect(clear_capture_action_, &QPushButton::clicked, this,
          &MainWindow::clearCompatibilityCapture);
  connect(export_capture_action_, &QPushButton::clicked, this,
          &MainWindow::exportCompatibilityCapture);
  controls->addWidget(start_capture_action_);
  controls->addWidget(stop_capture_action_);
  controls->addWidget(clear_capture_action_);
  controls->addWidget(export_capture_action_);
  layout->addLayout(controls);

  auto* marker_controls = new QHBoxLayout;
  capture_marker_selector_ = new QComboBox;
  capture_marker_selector_->setObjectName(QStringLiteral("captureMarkerSelector"));
  for (const auto* label : {"Baseline", "Remote input test",
                            "Active Source test", "Power On test",
                            "Standby test", "USB reconnect",
                            "HDMI reconnect"}) {
    capture_marker_selector_->addItem(QString::fromUtf8(label));
  }
  add_marker_action_ = new QPushButton(QStringLiteral("Add Marker"));
  add_marker_action_->setObjectName(QStringLiteral("addCaptureMarker"));
  add_note_action_ = new QPushButton(QStringLiteral("Add Note"));
  add_note_action_->setObjectName(QStringLiteral("addCaptureNote"));
  connect(add_marker_action_, &QPushButton::clicked, this,
          &MainWindow::addCompatibilityMarker);
  connect(add_note_action_, &QPushButton::clicked, this,
          &MainWindow::promptCompatibilityNote);
  marker_controls->addWidget(capture_marker_selector_, 1);
  marker_controls->addWidget(add_marker_action_);
  marker_controls->addWidget(add_note_action_);
  layout->addLayout(marker_controls);

  capture_physical_prompt_value_ = ValueLabel("capturePhysicalPrompt");
  capture_physical_note_ = new QLineEdit;
  capture_physical_note_->setObjectName(QStringLiteral("capturePhysicalNote"));
  capture_physical_note_->setPlaceholderText(
      QStringLiteral("Optional physical-result note"));
  auto* physical_controls = new QHBoxLayout;
  physical_worked_action_ = new QPushButton(QStringLiteral("Worked"));
  physical_worked_action_->setObjectName(QStringLiteral("capturePhysicalWorked"));
  physical_failed_action_ = new QPushButton(QStringLiteral("Did not work"));
  physical_failed_action_->setObjectName(QStringLiteral("capturePhysicalFailed"));
  physical_not_checked_action_ = new QPushButton(QStringLiteral("Not checked"));
  physical_not_checked_action_->setObjectName(
      QStringLiteral("capturePhysicalNotChecked"));
  connect(physical_worked_action_, &QPushButton::clicked, this, [this] {
    recordPhysicalResult(frontend::CompatibilityPhysicalResult::Worked);
  });
  connect(physical_failed_action_, &QPushButton::clicked, this, [this] {
    recordPhysicalResult(frontend::CompatibilityPhysicalResult::Failed);
  });
  connect(physical_not_checked_action_, &QPushButton::clicked, this, [this] {
    recordPhysicalResult(frontend::CompatibilityPhysicalResult::NotChecked);
  });
  physical_controls->addWidget(physical_worked_action_);
  physical_controls->addWidget(physical_failed_action_);
  physical_controls->addWidget(physical_not_checked_action_);
  layout->addWidget(capture_physical_prompt_value_);
  layout->addWidget(capture_physical_note_);
  layout->addLayout(physical_controls);

  capture_timeline_ = new QListWidget;
  capture_timeline_->setObjectName(QStringLiteral("captureTimeline"));
  layout->addWidget(new QLabel(QStringLiteral("Recent timeline")));
  layout->addWidget(capture_timeline_, 1);
  return page;
}

void MainWindow::addPage(const QString& title, QWidget* page, bool lab_only) {
  auto* item = new QListWidgetItem(title, navigation_);
  pages_->addWidget(page);
  if (lab_only) lab_items_.push_back(item);
}

void MainWindow::renderStatus(const frontend::FrontendStatus& status) {
  const auto capabilities = backend_->capabilities();
  if (!status.service_available && configuration_pending_) {
    configuration_pending_ = false;
    pending_configuration_request_ = 0;
    configuration_result_->setText(QStringLiteral("Backend unavailable"));
  }
  const bool configuration_available =
      status.service_available && capabilities.configuration;
  const bool remote_input_available =
      configuration_available && capabilities.remote_input;
  const bool remote_wake_available =
      configuration_available && capabilities.remote_wake;
  const bool action_available =
      status.service_available && capabilities.tv_power_actions &&
      status.session_state == SessionState::Connected;
  for (auto* control :
       {power_on_boot_, power_on_resume_, standby_on_suspend_,
        standby_on_shutdown_, suspend_on_tv_standby_}) {
    if (control) control->setEnabled(configuration_available);
  }
  if (wake_on_source_) wake_on_source_->setEnabled(remote_wake_available);
  if (remote_enabled_) remote_enabled_->setEnabled(remote_input_available);
  for (auto* button : {power_on_action_, active_source_action_, standby_action_}) {
    if (button) button->setEnabled(action_available);
  }
  connection_value_->setText(
      !status.service_available ? QStringLiteral("Backend unavailable")
      : !status.selected_device ? QStringLiteral("Injector not discovered")
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
                                     ? QStringLiteral("In-process backend running")
                                     : QStringLiteral("Backend unavailable"));
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
  if (configuration_result_ && !capabilities.configuration &&
      !configuration_pending_) {
    configuration_result_->setText(
        QStringLiteral("Configuration unavailable on this platform"));
  }
  if (remote_profile_ && !capabilities.remote_input) {
    remote_profile_->setText(QStringLiteral("Unavailable on this platform"));
  }
  markConfigurationDirty();
}

void MainWindow::renderCompatibilityCapture() {
  if (!capture_state_value_) return;
  const auto counts = compatibility_capture_.counts();
  capture_state_value_->setText(CaptureStateName(compatibility_capture_.state()));
  const auto elapsed =
      compatibility_capture_.elapsed(std::chrono::system_clock::now());
  capture_elapsed_value_->setText(
      elapsed ? QStringLiteral("%1 ms").arg(elapsed->count())
              : QStringLiteral("Not started"));
  capture_event_count_value_->setText(
      QStringLiteral("%1 retained / %2 total")
          .arg(counts.retained_cec_events)
          .arg(counts.total_cec_events));
  capture_action_count_value_->setText(QString::number(counts.action_count));
  capture_transition_count_value_->setText(
      QString::number(counts.status_transition_count));
  capture_marker_count_value_->setText(QString::number(counts.marker_count));
  capture_note_count_value_->setText(QString::number(counts.note_count));
  capture_dropped_count_value_->setText(
      QString::number(counts.dropped_cec_events));

  const auto active =
      compatibility_capture_.state() == frontend::CompatibilityCaptureState::Capturing;
  const auto completed =
      compatibility_capture_.state() == frontend::CompatibilityCaptureState::Completed;
  start_capture_action_->setEnabled(!active);
  stop_capture_action_->setEnabled(active);
  clear_capture_action_->setEnabled(active || completed);
  export_capture_action_->setEnabled(completed);
  add_note_action_->setEnabled(active);
  add_marker_action_->setEnabled(active);
  capture_marker_selector_->setEnabled(active);

  const auto pending =
      compatibility_capture_.latest_action_needing_physical_result();
  capture_pending_physical_request_ =
      pending ? std::optional{pending->request_id} : std::nullopt;
  const bool physical_enabled = active && capture_pending_physical_request_.has_value();
  physical_worked_action_->setEnabled(physical_enabled);
  physical_failed_action_->setEnabled(physical_enabled);
  physical_not_checked_action_->setEnabled(physical_enabled);
  capture_physical_note_->setEnabled(physical_enabled);
  if (pending) {
    capture_physical_prompt_value_->setText(
        QStringLiteral("Physical result for request #%1: %2 (%3)")
            .arg(pending->request_id)
            .arg(QString::fromStdString(
                frontend::compatibility_physical_result_name(
                    pending->physical_result)))
            .arg(QString::fromStdString(
                frontend::compatibility_capture_state_name(
                    compatibility_capture_.state()))));
  } else {
    capture_physical_prompt_value_->setText(
        active ? QStringLiteral("No completed action waiting for physical confirmation")
               : QStringLiteral("Physical confirmation available only while capturing"));
    if (!physical_enabled) capture_physical_note_->clear();
  }

  capture_timeline_->clear();
  for (const auto& line : compatibility_capture_.recent_timeline_lines()) {
    capture_timeline_->addItem(QString::fromStdString(line));
  }
  if (capture_timeline_->count() > 0) {
    capture_timeline_->scrollToBottom();
  }
}

void MainWindow::renderConfiguration(
    const frontend::FrontendConfigurationSnapshot& value) {
  if (configuration_pending_) {
    const bool own_change =
        value.revision == pending_configuration_base_revision_ + 1 &&
        value.configuration == pending_configuration_value_;
    if (!own_change) {
      if (value.revision == authoritative_configuration_.revision &&
          value.configuration == authoritative_configuration_.configuration) {
        return;
      }
      authoritative_configuration_ = value;
      external_conflict_ = true;
      configuration_result_->setText(
          QStringLiteral("Configuration changed externally — Reset required"));
      markConfigurationDirty();
      return;
    }
  }
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

void MainWindow::completeConfiguration(
    frontend::FrontendConfigurationRequestId request,
    const frontend::FrontendConfigurationResult& result) {
  if (!configuration_pending_ || request != pending_configuration_request_) return;
  const auto expected_revision = pending_configuration_base_revision_ + 1;
  if (result.result == OperationResult::Success &&
      result.authoritative.revision == expected_revision) {
    renderConfiguration({expected_revision, pending_configuration_value_});
    configuration_pending_ = false;
    pending_configuration_request_ = 0;
    external_conflict_ = false;
    configuration_result_->setText(QStringLiteral("Configuration applied"));
  } else {
    configuration_pending_ = false;
    pending_configuration_request_ = 0;
    if (result.result == OperationResult::Rejected) {
      authoritative_configuration_ = result.authoritative;
      external_conflict_ = true;
    }
    configuration_result_->setText(
        QStringLiteral("Apply failed: %1").arg(
            QString::fromStdString(result.message)));
  }
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
  const auto capabilities = backend_->capabilities();
  apply_configuration_->setEnabled(available && capabilities.configuration &&
                                   configuration_dirty_ &&
                                   !configuration_pending_ &&
                                   !external_conflict_);
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
  pending_configuration_base_revision_ = authoritative_configuration_.revision;
  pending_configuration_value_ = value;
  pending_configuration_request_ = 0;
  configuration_result_->setText(QStringLiteral("Applying…"));
  markConfigurationDirty();
  const auto request = backend_->request_configuration(
      authoritative_configuration_.revision, value);
  pending_configuration_request_ = request;
  if (request == 0) {
    configuration_pending_ = false;
    configuration_result_->setText(backend_->capabilities().configuration
                                       ? QStringLiteral("Backend unavailable")
                                       : QStringLiteral("Configuration unavailable on this platform"));
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

void MainWindow::startCompatibilityCapture() {
  compatibility_capture_.start(CaptureApplicationInfo(), backend_->status(),
                               backend_->capabilities(),
                               backend_->configuration(),
                               std::chrono::system_clock::now());
  capture_pending_physical_request_.reset();
  renderCompatibilityCapture();
}

void MainWindow::stopCompatibilityCapture() {
  compatibility_capture_.stop(std::chrono::system_clock::now());
  capture_pending_physical_request_.reset();
  renderCompatibilityCapture();
}

void MainWindow::clearCompatibilityCapture() {
  compatibility_capture_.clear();
  capture_pending_physical_request_.reset();
  capture_physical_note_->clear();
  renderCompatibilityCapture();
}

void MainWindow::addCompatibilityMarker() {
  compatibility_capture_.record_marker(
      capture_marker_selector_->currentText().toStdString(),
      std::chrono::system_clock::now());
  renderCompatibilityCapture();
}

void MainWindow::promptCompatibilityNote() {
  bool accepted = false;
  const auto text = QInputDialog::getText(
      this, QStringLiteral("Add Note"), QStringLiteral("Note"),
      QLineEdit::Normal, {}, &accepted);
  if (!accepted || text.trimmed().isEmpty()) return;
  compatibility_capture_.record_note(text.toStdString(),
                                     std::chrono::system_clock::now());
  renderCompatibilityCapture();
}

void MainWindow::exportCompatibilityCapture() {
  const auto export_data = compatibility_capture_.build_export();
  if (export_data.markdown.empty() || export_data.json.empty()) {
    QMessageBox::warning(this, QStringLiteral("Export Compatibility Report"),
                         QStringLiteral("No completed capture is available."));
    return;
  }

  const auto markdown_path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Export Compatibility Report"),
      DefaultExportPath(export_data.suggested_basename),
      QStringLiteral("Markdown (*.md)"));
  if (markdown_path.isEmpty()) return;

  QString json_path = markdown_path;
  if (json_path.endsWith(QStringLiteral(".md"))) {
    json_path.chop(3);
  }
  json_path += QStringLiteral(".json");

  auto write_file = [&](const QString& path, const std::string& contents) -> bool {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    if (file.write(contents.data(), static_cast<qint64>(contents.size())) < 0) {
      return false;
    }
    return file.commit();
  };

  if (!write_file(markdown_path, export_data.markdown) ||
      !write_file(json_path, export_data.json)) {
    QMessageBox::critical(this, QStringLiteral("Export Compatibility Report"),
                          QStringLiteral("Failed to write report files."));
    return;
  }

  QMessageBox::information(
      this, QStringLiteral("Export Compatibility Report"),
      QStringLiteral("Saved:\n%1\n%2").arg(markdown_path, json_path));
}

void MainWindow::recordPhysicalResult(
    frontend::CompatibilityPhysicalResult result) {
  if (!capture_pending_physical_request_) return;
  compatibility_capture_.record_action_physical_result(
      *capture_pending_physical_request_, result,
      capture_physical_note_->text().toStdString(),
      std::chrono::system_clock::now());
  capture_physical_note_->clear();
  capture_pending_physical_request_.reset();
  renderCompatibilityCapture();
}

}  // namespace cec_injector::mate::gui

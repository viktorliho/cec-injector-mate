#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

#include "cec_injector/mate/frontend/fake_frontend_backend.h"
#include "cec_injector/mate/gui/cec_monitor_model.h"
#include "cec_injector/mate/gui/main_window.h"

namespace {
using namespace cec_injector::mate;
using namespace cec_injector::mate::frontend;

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}

class ControlledBackend final : public MateFrontendBackend {
 public:
  ControlledBackend() {
    status_.service_available = true;
    status_.injector_connected = true;
    status_.selected_device = DeviceId("capture-device");
    status_.display_name = "Capture TV";
    status_.physical_address_status = PhysicalAddressStatus::Available;
    status_.physical_address = 0x1000;
    status_.logical_address = 4;
    status_.session_state = SessionState::Connected;
    capabilities_.configuration = true;
    capabilities_.tv_power_actions = true;
    capabilities_.cec_monitor = true;
    configuration_.revision = 1;
    configuration_.configuration = production_runtime_defaults();
  }

  void set_observer(MateFrontendObserver* observer) override { observer_ = observer; }
  FrontendStatus status() const override { return status_; }
  FrontendCapabilities capabilities() const override { return capabilities_; }
  FrontendConfigurationSnapshot configuration() const override {
    return configuration_;
  }
  FrontendConfigurationRequestId request_configuration(
      std::uint64_t base, const FrontendConfiguration& value) override {
    requested_base = base;
    requested = value;
    return next_request++;
  }
  FrontendActionRequestId request_action(FrontendAction action) override {
    last_action = action;
    ++action_requests;
    return next_action_request++;
  }

  void change(const FrontendConfigurationSnapshot& value) {
    configuration_ = value;
    observer_->on_frontend_configuration_changed(value);
  }
  void complete(FrontendConfigurationRequestId request, OperationResult result,
                std::uint64_t revision, std::string message = {}) {
    observer_->on_frontend_configuration_completed(
        request, {result, {revision, configuration_.configuration},
                  std::move(message)});
  }
  void disappear() {
    status_.service_available = false;
    observer_->on_frontend_status_changed(status_);
  }
  void appear(const FrontendConfigurationSnapshot& value) {
    status_.service_available = true;
    observer_->on_frontend_status_changed(status_);
    change(value);
  }

  void pushStatus(const FrontendStatus& status) {
    status_ = status;
    observer_->on_frontend_status_changed(status_);
  }

  void completeAction(FrontendActionRequestId request,
                      const FrontendActionResult& result) {
    observer_->on_frontend_action_completed(request, result);
  }

  std::uint64_t requested_base{};
  FrontendConfiguration requested;
  FrontendConfigurationRequestId next_request{41};
  FrontendActionRequestId next_action_request{71};
  FrontendAction last_action{FrontendAction::PowerOnTv};
  int action_requests{};

 private:
  MateFrontendObserver* observer_{};
  FrontendStatus status_;
  FrontendCapabilities capabilities_;
  FrontendConfigurationSnapshot configuration_;
};

void FlushEvents(QApplication& application) {
  application.processEvents();
  application.processEvents();
}

struct ConfigurationControls {
  QCheckBox* resume;
  QPushButton* apply;
  QPushButton* reset;
  QLabel* result;
};

ConfigurationControls Controls(cec_injector::mate::gui::MainWindow& window) {
  return {window.findChild<QCheckBox*>("powerOnResume"),
          window.findChild<QPushButton*>("applyConfiguration"),
          window.findChild<QPushButton*>("resetConfiguration"),
          window.findChild<QLabel*>("configurationResult")};
}

void BeginApply(ConfigurationControls controls) {
  controls.resume->setChecked(false);
  Check(controls.apply->isEnabled(), "edited configuration can apply");
  controls.apply->click();
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  cec_injector::mate::gui::MainWindow window(
      std::make_unique<
          cec_injector::mate::frontend::FakeFrontendBackend>());
  application.processEvents();
  Check(!window.labModeEnabled(), "Lab Mode defaults off");
  Check(window.visibleLabPageCount() == 0, "Lab navigation hidden");
  const auto* connection = window.findChild<QLabel*>("overviewConnection");
  Check(connection && connection->text() == "Connected", "fake state rendered");
  auto* toggle = window.findChild<QCheckBox*>("labModeToggle");
  Check(toggle != nullptr, "Lab Mode control exists");
  toggle->setChecked(true);
  Check(window.labModeEnabled(), "Lab Mode enabled");
  Check(window.visibleLabPageCount() == 5, "Lab navigation exposed");
  Check(window.cecMonitorModel()->rowCount() == 2,
        "synthetic CEC monitor rows");

  const auto before = window.cecMonitorModel()->rowCount();
  window.cecMonitorModel()->appendEvent(
      {.timestamp = std::chrono::system_clock::now(),
       .direction = cec_injector::mate::frontend::CecDirection::Received,
       .source = 0,
       .destination = 15,
       .opcode = 0x36,
       .parameters = {},
       .semantic = "test"});
  Check(window.cecMonitorModel()->rowCount() == before + 1,
        "CEC monitor accepts rows");
  for (std::size_t index = 0;
       index < cec_injector::mate::gui::CecMonitorModel::kMaximumEvents + 10;
       ++index) {
    window.cecMonitorModel()->appendEvent({});
  }
  Check(window.cecMonitorModel()->rowCount() == 2000,
        "CEC monitor history is bounded");
  auto* power = window.findChild<QPushButton*>("powerOnAction");
  auto* result = window.findChild<QLabel*>("actionResult");
  Check(power && result, "typed action controls exist");
  power->click();
  application.processEvents();
  Check(result->text().contains("Success") && result->text().contains("12 ms"),
        "action result rendered");
  auto* boot = window.findChild<QCheckBox*>("powerOnBoot");
  auto* apply = window.findChild<QPushButton*>("applyConfiguration");
  auto* reset = window.findChild<QPushButton*>("resetConfiguration");
  Check(boot && apply && reset && !apply->isEnabled(),
        "configuration starts authoritative and clean");
  boot->setChecked(!boot->isChecked());
  Check(apply->isEnabled() && reset->isEnabled(), "configuration dirty state");
  apply->click();
  application.processEvents();
  Check(!apply->isEnabled(), "confirmed configuration clears dirty state");

  {
    auto backend = std::make_unique<ControlledBackend>();
    auto* control = backend.get();
    cec_injector::mate::gui::MainWindow ordered(std::move(backend));
    FlushEvents(application);
    auto controls = Controls(ordered);
    BeginApply(controls);
    auto own = FrontendConfigurationSnapshot{2, control->requested};
    control->change(own);
    FlushEvents(application);
    control->complete(41, OperationResult::Success, 2);
    FlushEvents(application);
    Check(controls.result->text() == "Configuration applied" &&
              !controls.apply->isEnabled() && !controls.reset->isEnabled(),
          "own change before completion finishes cleanly");
  }

  {
    auto backend = std::make_unique<ControlledBackend>();
    auto* control = backend.get();
    cec_injector::mate::gui::MainWindow ordered(std::move(backend));
    FlushEvents(application);
    auto controls = Controls(ordered);
    BeginApply(controls);
    control->complete(41, OperationResult::Success, 2);
    FlushEvents(application);
    control->change({2, control->requested});
    FlushEvents(application);
    Check(controls.result->text() == "Configuration applied" &&
              !controls.apply->isEnabled() && !controls.reset->isEnabled(),
          "completion before own change finishes cleanly");
    control->complete(41, OperationResult::Success, 2);
    FlushEvents(application);
    Check(controls.result->text() == "Configuration applied",
          "duplicate completion ignored");
  }

  {
    auto backend = std::make_unique<ControlledBackend>();
    auto* control = backend.get();
    cec_injector::mate::gui::MainWindow conflicting(std::move(backend));
    FlushEvents(application);
    auto controls = Controls(conflicting);
    BeginApply(controls);
    auto external = production_runtime_defaults();
    external.policy.power_on_boot = false;
    control->change({2, external});
    FlushEvents(application);
    control->complete(41, OperationResult::Rejected, 2, "revision conflict");
    FlushEvents(application);
    Check(controls.result->text().contains("revision conflict") &&
              controls.reset->isEnabled() && !controls.apply->isEnabled(),
          "external change and stale rejection require reset");
  }

  {
    auto backend = std::make_unique<ControlledBackend>();
    auto* control = backend.get();
    cec_injector::mate::gui::MainWindow failed(std::move(backend));
    FlushEvents(application);
    auto controls = Controls(failed);
    BeginApply(controls);
    control->complete(41, OperationResult::InternalError, 1, "persist failed");
    FlushEvents(application);
    Check(controls.result->text().contains("persist failed") &&
              controls.apply->isEnabled() && controls.reset->isEnabled(),
          "failed apply preserves editable state");
  }

  {
    auto backend = std::make_unique<ControlledBackend>();
    auto* control = backend.get();
    cec_injector::mate::gui::MainWindow lost(std::move(backend));
    FlushEvents(application);
    auto controls = Controls(lost);
    BeginApply(controls);
    control->disappear();
    FlushEvents(application);
    control->complete(41, OperationResult::Success, 2);
    FlushEvents(application);
    Check(controls.result->text().contains("unavailable", Qt::CaseInsensitive) &&
              !controls.apply->isEnabled(),
          "service loss invalidates pending apply and late completion");
    auto fresh = production_runtime_defaults();
    fresh.policy.power_on_boot = false;
    control->appear({3, fresh});
    FlushEvents(application);
    Check(controls.result->text().contains("Reset required") &&
              controls.reset->isEnabled() && !controls.apply->isEnabled(),
          "fresh state after service recovery wins without stale correlation");
  }

  {
    auto backend = std::make_unique<ControlledBackend>();
    auto* control = backend.get();
    cec_injector::mate::gui::MainWindow capture_window(std::move(backend));
    FlushEvents(application);
    auto* toggle = capture_window.findChild<QCheckBox*>("labModeToggle");
    toggle->setChecked(true);
    FlushEvents(application);
    auto* start = capture_window.findChild<QPushButton*>("startCapture");
    auto* stop = capture_window.findChild<QPushButton*>("stopCapture");
    auto* clear = capture_window.findChild<QPushButton*>("clearCapture");
    auto* export_button =
        capture_window.findChild<QPushButton*>("exportCapture");
    auto* action_count =
        capture_window.findChild<QLabel*>("captureActionCount");
    auto* status_count =
        capture_window.findChild<QLabel*>("captureStatusChangeCount");
    auto* state = capture_window.findChild<QLabel*>("captureState");
    auto* worked =
        capture_window.findChild<QPushButton*>("capturePhysicalWorked");
    auto* physical_note =
        capture_window.findChild<QLineEdit*>("capturePhysicalNote");
    auto* timeline = capture_window.findChild<QListWidget*>("captureTimeline");
    Check(start && stop && clear && export_button && action_count &&
              status_count && state && worked && physical_note && timeline,
          "capture controls exist");
    Check(control->action_requests == 0, "capture start has no side effects");
    start->click();
    FlushEvents(application);
    Check(state->text() == "Capturing", "capture entered capturing state");
    Check(control->action_requests == 0, "start capture does not send action");

    auto* power = capture_window.findChild<QPushButton*>("powerOnAction");
    power->click();
    FlushEvents(application);
    Check(control->action_requests == 1 &&
              control->last_action == FrontendAction::PowerOnTv,
          "manual action still goes through normal request path");
    control->completeAction(
        71, {.action = FrontendAction::PowerOnTv,
             .result = OperationResult::Success,
             .duration = std::chrono::milliseconds(55),
             .message = "ok"});
    FlushEvents(application);
    Check(action_count->text() == "1", "completed action counted");
    Check(worked->isEnabled(), "physical result can be recorded");
    physical_note->setText("TV woke");
    worked->click();
    FlushEvents(application);
    Check(!worked->isEnabled(), "physical result recording clears pending state");

    auto disconnected = control->status();
    disconnected.session_state = SessionState::Disconnected;
    disconnected.injector_connected = false;
    control->pushStatus(disconnected);
    FlushEvents(application);
    Check(status_count->text() == "1", "status transition recorded");
    Check(timeline->count() > 0, "capture timeline shows recent activity");

    stop->click();
    FlushEvents(application);
    Check(state->text() == "Completed" && export_button->isEnabled(),
          "completed capture is exportable");
    clear->click();
    FlushEvents(application);
    Check(state->text() == "Idle" && action_count->text() == "0" &&
              !export_button->isEnabled(),
          "clear resets local capture state");
  }
}

#pragma once

#include <QMainWindow>

#include <memory>
#include <optional>
#include <vector>

#include "cec_injector/mate/frontend/compatibility_capture.h"
#include "cec_injector/mate/frontend/frontend_backend.h"

class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTimer;

namespace cec_injector::mate::gui {

class BackendQtBridge;
class CecMonitorModel;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(
      std::unique_ptr<frontend::MateFrontendBackend> backend,
      QWidget* parent = nullptr);
  ~MainWindow() override;

  [[nodiscard]] bool labModeEnabled() const noexcept;
  [[nodiscard]] int visibleLabPageCount() const;
  [[nodiscard]] CecMonitorModel* cecMonitorModel() const noexcept;

 public slots:
  void setLabModeEnabled(bool enabled);

 private:
  QWidget* createOverviewPage();
  QWidget* createBehaviorPage();
  QWidget* createRemotePage();
  QWidget* createSettingsPage();
  QWidget* createDevicePage();
  QWidget* createDisplayPage();
  QWidget* createMonitorPage();
  QWidget* createCommandsPage();
  QWidget* createTestsPage();
  void addPage(const QString& title, QWidget* page, bool lab_only);
  void renderStatus(const frontend::FrontendStatus& status);
  void renderConfiguration(
      const frontend::FrontendConfigurationSnapshot& configuration);
  void completeConfiguration(
      frontend::FrontendConfigurationRequestId request,
      const frontend::FrontendConfigurationResult& result);
  void renderCompatibilityCapture();
  void markConfigurationDirty();
  void applyConfiguration();
  void resetConfiguration();
  void startCompatibilityCapture();
  void stopCompatibilityCapture();
  void clearCompatibilityCapture();
  void addCompatibilityMarker();
  void promptCompatibilityNote();
  void exportCompatibilityCapture();
  void recordPhysicalResult(frontend::CompatibilityPhysicalResult result);

  std::unique_ptr<frontend::MateFrontendBackend> backend_;
  std::unique_ptr<BackendQtBridge> bridge_;
  QListWidget* navigation_{};
  QStackedWidget* pages_{};
  QLabel* connection_value_{};
  QLabel* display_value_{};
  QLabel* pa_value_{};
  QLabel* session_value_{};
  QLabel* device_id_value_{};
  QLabel* logical_value_{};
  QLabel* wake_value_{};
  QLabel* edid_value_{};
  QLabel* backend_status_value_{};
  QLabel* lab_display_value_{};
  QLabel* lab_pa_status_value_{};
  QLabel* lab_pa_value_{};
  QLabel* action_result_{};
  QLabel* capture_state_value_{};
  QLabel* capture_elapsed_value_{};
  QLabel* capture_event_count_value_{};
  QLabel* capture_action_count_value_{};
  QLabel* capture_transition_count_value_{};
  QLabel* capture_note_count_value_{};
  QLabel* capture_marker_count_value_{};
  QLabel* capture_dropped_count_value_{};
  QLabel* capture_physical_prompt_value_{};
  QPushButton* power_on_action_{};
  QPushButton* active_source_action_{};
  QPushButton* standby_action_{};
  QPushButton* start_capture_action_{};
  QPushButton* stop_capture_action_{};
  QPushButton* clear_capture_action_{};
  QPushButton* export_capture_action_{};
  QPushButton* add_note_action_{};
  QPushButton* add_marker_action_{};
  QPushButton* physical_worked_action_{};
  QPushButton* physical_failed_action_{};
  QPushButton* physical_not_checked_action_{};
  QLabel* configuration_result_{};
  QCheckBox* power_on_boot_{};
  QCheckBox* power_on_resume_{};
  QCheckBox* standby_on_suspend_{};
  QCheckBox* standby_on_shutdown_{};
  QCheckBox* suspend_on_tv_standby_{};
  QCheckBox* wake_on_source_{};
  QCheckBox* remote_enabled_{};
  QLabel* remote_profile_{};
  QPushButton* apply_configuration_{};
  QPushButton* reset_configuration_{};
  QComboBox* capture_marker_selector_{};
  QLineEdit* capture_physical_note_{};
  QListWidget* capture_timeline_{};
  QTimer* capture_timer_{};
  frontend::FrontendConfigurationSnapshot authoritative_configuration_;
  frontend::CompatibilityCapture compatibility_capture_;
  std::optional<frontend::FrontendActionRequestId>
      capture_pending_physical_request_;
  bool configuration_dirty_{};
  bool configuration_pending_{};
  bool external_conflict_{};
  frontend::FrontendConfigurationRequestId pending_configuration_request_{};
  std::uint64_t pending_configuration_base_revision_{};
  frontend::FrontendConfiguration pending_configuration_value_{};
  CecMonitorModel* monitor_model_{};
  bool lab_mode_{};
  std::vector<QListWidgetItem*> lab_items_;
};

}  // namespace cec_injector::mate::gui

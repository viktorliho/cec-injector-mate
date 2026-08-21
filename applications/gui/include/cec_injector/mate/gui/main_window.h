#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

#include "cec_injector/mate/frontend/frontend_backend.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;

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
  QLabel* action_result_{};
  CecMonitorModel* monitor_model_{};
  bool lab_mode_{};
  std::vector<QListWidgetItem*> lab_items_;
};

}  // namespace cec_injector::mate::gui

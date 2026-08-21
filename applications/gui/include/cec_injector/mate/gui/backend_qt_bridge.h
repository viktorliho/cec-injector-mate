#pragma once

#include <QObject>

#include "cec_injector/mate/frontend/frontend_backend.h"

namespace cec_injector::mate::gui {

class BackendQtBridge final : public QObject,
                              public frontend::MateFrontendObserver {
  Q_OBJECT

 public:
  explicit BackendQtBridge(frontend::MateFrontendBackend& backend,
                           QObject* parent = nullptr);
  ~BackendQtBridge() override;

  void on_frontend_status_changed(
      const frontend::FrontendStatus& status) override;
  void on_frontend_cec_event(
      const frontend::FrontendCecEvent& event) override;
  void on_frontend_action_completed(
      frontend::FrontendActionRequestId request,
      const frontend::FrontendActionResult& result) override;
  void on_frontend_configuration_changed(
      const frontend::FrontendConfigurationSnapshot& value) override;
  void on_frontend_configuration_completed(
      frontend::FrontendConfigurationRequestId request,
      const frontend::FrontendConfigurationResult& result) override;

 signals:
  void statusChanged(frontend::FrontendStatus status);
  void cecEventReceived(frontend::FrontendCecEvent event);
  void actionCompleted(frontend::FrontendActionRequestId request,
                       frontend::FrontendActionResult result);
  void configurationChanged(frontend::FrontendConfigurationSnapshot value);
  void configurationCompleted(frontend::FrontendConfigurationRequestId request,
                              frontend::FrontendConfigurationResult result);

 private:
  frontend::MateFrontendBackend& backend_;
};

}  // namespace cec_injector::mate::gui

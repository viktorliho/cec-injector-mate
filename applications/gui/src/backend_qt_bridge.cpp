#include "cec_injector/mate/gui/backend_qt_bridge.h"

#include <QMetaObject>

namespace cec_injector::mate::gui {

BackendQtBridge::BackendQtBridge(frontend::MateFrontendBackend& backend,
                                 QObject* parent)
    : QObject(parent), backend_(backend) {
  backend_.set_observer(this);
}

BackendQtBridge::~BackendQtBridge() { backend_.set_observer(nullptr); }

void BackendQtBridge::on_frontend_status_changed(
    const frontend::FrontendStatus& status) {
  QMetaObject::invokeMethod(
      this, [this, status] { emit statusChanged(status); },
      Qt::QueuedConnection);
}

void BackendQtBridge::on_frontend_cec_event(
    const frontend::FrontendCecEvent& event) {
  QMetaObject::invokeMethod(
      this, [this, event] { emit cecEventReceived(event); },
      Qt::QueuedConnection);
}

void BackendQtBridge::on_frontend_action_completed(
    frontend::FrontendActionRequestId request,
    const frontend::FrontendActionResult& result) {
  QMetaObject::invokeMethod(
      this, [this, request, result] { emit actionCompleted(request, result); },
      Qt::QueuedConnection);
}

void BackendQtBridge::on_frontend_configuration_changed(
    const frontend::FrontendConfigurationSnapshot& value) {
  QMetaObject::invokeMethod(
      this, [this, value] { emit configurationChanged(value); },
      Qt::QueuedConnection);
}

void BackendQtBridge::on_frontend_configuration_completed(
    frontend::FrontendConfigurationRequestId request,
    const frontend::FrontendConfigurationResult& result) {
  QMetaObject::invokeMethod(
      this, [this, request, result] { emit configurationCompleted(request, result); },
      Qt::QueuedConnection);
}

}  // namespace cec_injector::mate::gui

#include <QApplication>

#include <memory>

#include "cec_injector/mate/frontend/fake_frontend_backend.h"
#include "cec_injector/mate/gui/main_window.h"
#ifdef CEC_INJECTOR_MATE_GUI_LINUX_DBUS
#include "cec_injector/mate/gui/daemon_mate_frontend_backend.h"
#endif

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  const bool fake = application.arguments().contains(QStringLiteral("--fake-backend"));
  std::unique_ptr<cec_injector::mate::frontend::MateFrontendBackend> backend;
#ifdef CEC_INJECTOR_MATE_GUI_LINUX_DBUS
  if (!fake) {
    backend = std::make_unique<cec_injector::mate::gui::DaemonMateFrontendBackend>();
  } else
#endif
  {
    backend = std::make_unique<cec_injector::mate::frontend::FakeFrontendBackend>();
  }
  cec_injector::mate::gui::MainWindow window(
      std::move(backend));
  window.show();
  return application.exec();
}

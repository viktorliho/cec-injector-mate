#include <QApplication>

#include <memory>

#include "cec_injector/mate/frontend/fake_frontend_backend.h"
#include "cec_injector/mate/gui/main_window.h"

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  cec_injector::mate::gui::MainWindow window(
      std::make_unique<cec_injector::mate::frontend::FakeFrontendBackend>());
  window.show();
  return application.exec();
}

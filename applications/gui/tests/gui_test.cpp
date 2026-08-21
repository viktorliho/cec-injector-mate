#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

#include "cec_injector/mate/frontend/fake_frontend_backend.h"
#include "cec_injector/mate/gui/cec_monitor_model.h"
#include "cec_injector/mate/gui/main_window.h"

namespace {

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
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
  auto* power = window.findChild<QPushButton*>("powerOnAction");
  auto* result = window.findChild<QLabel*>("actionResult");
  Check(power && result, "typed action controls exist");
  power->click();
  application.processEvents();
  Check(result->text().contains("Success") && result->text().contains("12 ms"),
        "action result rendered");
}

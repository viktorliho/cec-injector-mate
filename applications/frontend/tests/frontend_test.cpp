#include <cstdlib>
#include <iostream>
#include <string_view>

#include "cec_injector/mate/frontend/fake_frontend_backend.h"

namespace {

void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}

struct Observer final : cec_injector::mate::frontend::MateFrontendObserver {
  int statuses{};
  int events{};
  int actions{};
  cec_injector::mate::frontend::FrontendActionRequestId last_request{};

  void on_frontend_status_changed(
      const cec_injector::mate::frontend::FrontendStatus&) override {
    ++statuses;
  }
  void on_frontend_cec_event(
      const cec_injector::mate::frontend::FrontendCecEvent&) override {
    ++events;
  }
  void on_frontend_action_completed(
      cec_injector::mate::frontend::FrontendActionRequestId request,
      const cec_injector::mate::frontend::FrontendActionResult&) override {
    ++actions;
    last_request = request;
  }
};

}  // namespace

int main() {
  using namespace cec_injector::mate;
  using namespace cec_injector::mate::frontend;
  FakeFrontendBackend backend;
  Observer observer;
  backend.set_observer(&observer);
  Check(observer.statuses == 1 && observer.events == 2,
        "observer receives current synthetic feed");
  const auto status = backend.status();
  Check(status.injector_connected, "representative connection state");
  Check(status.physical_address == 0x1000, "representative PA");
  Check(status.logical_address == 4, "Playback 1 logical address");
  Check(status.session_state == SessionState::Connected, "session state");
  Check(status.wake.supported && status.wake.armed, "Wake state");

  const auto capabilities = backend.capabilities();
  Check(capabilities.configuration && capabilities.cec_monitor,
        "capability state");

  auto configuration = backend.configuration();
  Check(configuration.policy.power_on_boot, "typed policy configuration");
  configuration.policy.power_on_boot = false;
  Check(backend.apply_configuration(configuration) == OperationResult::Success,
        "fake configuration edit");
  Check(!backend.configuration().policy.power_on_boot,
        "configuration retained");

  const auto request = backend.request_action(FrontendAction::PowerOnTv);
  Check(request != 0 && observer.actions == 1 &&
            observer.last_request == request,
        "typed asynchronous fake action result");
  backend.set_observer(nullptr);
  const auto detached_statuses = observer.statuses;
  const auto detached_actions = observer.actions;
  (void)backend.apply_configuration(configuration);
  (void)backend.request_action(FrontendAction::StandbyTv);
  Check(observer.statuses == detached_statuses &&
            observer.actions == detached_actions,
        "no callbacks after observer detach");
}

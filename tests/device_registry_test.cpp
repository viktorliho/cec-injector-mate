#include "cec_injector/mate/device_registry.h"

#include <iostream>
#include <stdexcept>

#include "test_support.h"

using namespace cec_injector::mate;

namespace {
const DeviceId a("a");
const DeviceId b("b");
const DeviceId c("c");
}

int main() {
  DeviceRegistry registry;

  CHECK(registry.update_discovered({}).empty());
  CHECK(registry.snapshot().state == SelectionState::Waiting);

  auto intents = registry.update_discovered({a});
  CHECK(intents == std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, a}});
  CHECK(registry.snapshot().selected_active == a);

  intents = registry.update_discovered({a, b});
  CHECK(intents.empty());
  CHECK(registry.snapshot().selected_active == a);

  intents = registry.update_discovered({b});
  CHECK(intents == std::vector<CoreIntent>{{CoreIntentKind::DisconnectDevice, a}});
  CHECK(!registry.snapshot().selected_active);
  CHECK(registry.snapshot().state == SelectionState::SelectionRequired);

  intents = registry.set_explicit_selection(b);
  CHECK(intents == std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, b}});
  CHECK(registry.snapshot().selected_active == b);

  intents = registry.set_explicit_selection(c);
  CHECK(intents == std::vector<CoreIntent>{{CoreIntentKind::DisconnectDevice, b}});
  CHECK(!registry.snapshot().selected_active);
  CHECK(registry.snapshot().state == SelectionState::Waiting);

  intents = registry.update_discovered({b, c});
  if (!(intents == std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, c}})) {
    std::cerr << "unexpected explicit recovery intents size=" << intents.size()
              << " selected="
              << (registry.snapshot().selected_active
                      ? registry.snapshot().selected_active->diagnostic_value()
                      : "none")
              << '\n';
  }
  CHECK(intents == std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, c}});
  CHECK(registry.snapshot().selected_active == c);

  DeviceRegistry ambiguous;
  CHECK(ambiguous.update_discovered({a, b}).empty());
  CHECK(ambiguous.snapshot().state == SelectionState::Ambiguous);
  CHECK(!ambiguous.snapshot().selected_active);

  CHECK(ambiguous.set_explicit_selection(b) ==
        std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, b}});
  CHECK(ambiguous.snapshot().selected_active == b);

  DeviceRegistry reconnecting;
  CHECK(reconnecting.update_discovered({a}) ==
        std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, a}});
  CHECK(reconnecting.update_discovered({}) ==
        std::vector<CoreIntent>{{CoreIntentKind::DisconnectDevice, a}});
  CHECK(!reconnecting.snapshot().selected_active);
  CHECK(reconnecting.snapshot().state == SelectionState::Waiting);
  CHECK(reconnecting.update_discovered({a}) ==
        std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, a}});
  CHECK(reconnecting.snapshot().selected_active == a);

  DeviceRegistry invalid;
  bool rejected = false;
  try {
    (void)invalid.update_discovered({DeviceId{}});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);

  return EXIT_SUCCESS;
}

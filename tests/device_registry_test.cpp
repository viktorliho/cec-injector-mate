#include "cec_injector/mate/device_registry.h"

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
  CHECK(intents == std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, c}});
  CHECK(registry.snapshot().selected_active == c);

  DeviceRegistry ambiguous;
  CHECK(ambiguous.update_discovered({a, b}).empty());
  CHECK(ambiguous.snapshot().state == SelectionState::Ambiguous);
  CHECK(!ambiguous.snapshot().selected_active);

  CHECK(ambiguous.set_explicit_selection(b) ==
        std::vector<CoreIntent>{{CoreIntentKind::ConnectDevice, b}});
  CHECK(ambiguous.snapshot().selected_active == b);

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

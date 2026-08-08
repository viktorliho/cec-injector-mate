#include "cec_injector/mate/linux/linux_device_session_factory.h"

#include <cstdlib>
#include <memory>
#include <vector>

#include "test_support.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::linux_platform;

int main() {
  auto inventory = std::make_shared<LinuxDeviceInventory>();
  const LinuxDeviceCandidate candidate{
      "/dev/ttyACM7", "/dev/serial/by-id/usb-reference", 0xcafe, 0xcec1,
      "SERIAL-A", "cdc_acm", "0202"};
  auto snapshot = make_discovery_snapshot({candidate});
  CHECK(snapshot.status == DiscoverySnapshotStatus::Complete);
  CHECK(snapshot.devices.size() == 1);
  inventory->replace(snapshot, {candidate});

  LinuxDeviceSessionFactory factory(inventory);
  auto session = factory.create_session(snapshot.devices.front(), {0x2100});
  CHECK(session);
  CHECK(session->device_id() == snapshot.devices.front().id);
  CHECK(session->state() == SessionState::Discovered);

  CHECK(!factory.create_session(snapshot.devices.front(), {0xffff}));
  const DiscoveredDevice wrong_locator{
      snapshot.devices.front().id, PlatformDeviceLocator("opaque-stale"),
      "stale"};
  CHECK(!factory.create_session(wrong_locator, {0x2100}));

  inventory->replace({}, {});
  CHECK(!factory.create_session(snapshot.devices.front(), {0x2100}));
  CHECK(session->state() == SessionState::Discovered);
  return EXIT_SUCCESS;
}

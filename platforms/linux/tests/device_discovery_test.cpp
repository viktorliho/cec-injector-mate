#include "cec_injector/mate/linux/linux_device_discovery.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include "test_support.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::linux_platform;

namespace {

LinuxDeviceCandidate Candidate(std::string serial = "SERIAL-A") {
  return {.device_node = "/dev/ttyACM0",
          .stable_path = "/dev/serial/by-id/reference-a",
          .vendor_id = kReferenceVendorId,
          .product_id = kReferenceProductId,
          .serial = std::move(serial),
          .driver = "cdc_acm",
          .interface_class = "0202"};
}

}  // namespace

int main() {
  const auto valid = Candidate();
  CHECK(is_reference_device_candidate(valid));

  auto rejected = valid;
  rejected.vendor_id = 0x1234;
  CHECK(!is_reference_device_candidate(rejected));
  rejected = valid;
  rejected.product_id = 0x5678;
  CHECK(!is_reference_device_candidate(rejected));
  rejected = valid;
  rejected.driver = "usbserial";
  CHECK(!is_reference_device_candidate(rejected));
  rejected = valid;
  rejected.interface_class = "0300";
  CHECK(!is_reference_device_candidate(rejected));
  rejected = valid;
  rejected.serial.clear();
  CHECK(!is_reference_device_candidate(rejected));
  rejected = valid;
  rejected.stable_path.clear();
  CHECK(!is_reference_device_candidate(rejected));
  rejected = valid;
  rejected.stable_path = "/dev/ttyACM0";
  CHECK(!is_reference_device_candidate(rejected));

  bool empty_rejected = false;
  try {
    (void)make_linux_device_id("");
  } catch (const std::invalid_argument&) {
    empty_rejected = true;
  }
  CHECK(empty_rejected);

  auto reconnected = valid;
  reconnected.device_node = "/dev/ttyACM7";
  CHECK(make_linux_device_id(valid.serial) ==
        make_linux_device_id(reconnected.serial));
  CHECK(make_linux_device_id("SERIAL-A") != make_linux_device_id("SERIAL-B"));

  const auto empty = make_discovery_snapshot({});
  CHECK(empty.status == DiscoverySnapshotStatus::Complete);
  CHECK(empty.devices.empty());

  const auto one = make_discovery_snapshot({valid});
  CHECK(one.status == DiscoverySnapshotStatus::Complete);
  CHECK(one.devices.size() == 1);
  CHECK(one.devices[0].id == make_linux_device_id("SERIAL-A"));
  CHECK(one.devices[0].locator ==
        PlatformDeviceLocator("/dev/serial/by-id/reference-a"));

  auto second = Candidate("SERIAL-B");
  second.device_node = "/dev/ttyACM2";
  second.stable_path = "/dev/serial/by-id/reference-b";
  const auto many = make_discovery_snapshot({second, valid});
  CHECK(many.status == DiscoverySnapshotStatus::Complete);
  CHECK(many.devices.size() == 2);
  CHECK(many.devices[0].id == make_linux_device_id("SERIAL-A"));
  CHECK(many.devices[1].id == make_linux_device_id("SERIAL-B"));

  const auto reversed = make_discovery_snapshot({valid, second});
  CHECK(reversed.devices == many.devices);

  auto duplicate = valid;
  duplicate.device_node = "/dev/ttyACM9";
  duplicate.stable_path = "/dev/serial/by-id/reference-duplicate";
  const auto collision = make_discovery_snapshot({valid, duplicate, second});
  CHECK(collision.status == DiscoverySnapshotStatus::DuplicateIdentity);
  CHECK(collision.devices.empty());
  CHECK(collision.duplicate_ids ==
        std::vector<DeviceId>{make_linux_device_id("SERIAL-A")});

  CHECK(should_reenumerate({UdevAction::Add, "tty"}));
  CHECK(should_reenumerate({UdevAction::Remove, "tty"}));
  CHECK(should_reenumerate({UdevAction::Change, "tty"}));
  CHECK(!should_reenumerate({UdevAction::Other, "tty"}));
  CHECK(!should_reenumerate({UdevAction::Add, "usb"}));

  return EXIT_SUCCESS;
}

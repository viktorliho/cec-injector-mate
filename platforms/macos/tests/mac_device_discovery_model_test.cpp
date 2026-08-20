#include <iostream>
#include <vector>

#include "cec_injector/mate/macos/mac_device_inventory.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::macos_platform;

namespace {

void check(bool value, const char* message) {
  if (!value) throw message;
}

MacDeviceCandidate candidate(std::string serial, std::string callout,
                             std::uint64_t registry_id) {
  MacDeviceCandidate value;
  value.callout_device = std::move(callout);
  value.dialin_device = "/dev/tty.usbmodem1101";
  value.vendor_id = kReferenceVendorId;
  value.product_id = kReferenceProductId;
  value.serial = std::move(serial);
  value.tty_device = "usbmodem1101";
  value.serial_client_type = "IOSerialStream";
  value.has_usb_device_ancestry = true;
  value.registry_entry_id = registry_id;
  return value;
}

}  // namespace

int main() {
  try {
    check(make_macos_device_id("TEST-SERIAL-A").valid(), "device id");
    check(is_reference_device_candidate(
              candidate("TEST-SERIAL-A", "/dev/cu.usbmodem1101", 0x101)),
          "positive matcher");

    auto wrong_vid = candidate("TEST-SERIAL-A", "/dev/cu.usbmodem1101", 0x101);
    wrong_vid.vendor_id = 0;
    check(!is_reference_device_candidate(wrong_vid), "wrong vid rejected");

    auto wrong_pid = candidate("TEST-SERIAL-A", "/dev/cu.usbmodem1101", 0x101);
    wrong_pid.product_id = 0;
    check(!is_reference_device_candidate(wrong_pid), "wrong pid rejected");

    auto wrong_service =
        candidate("TEST-SERIAL-A", "/dev/cu.usbmodem1101", 0x101);
    wrong_service.serial_client_type = "Other";
    check(!is_reference_device_candidate(wrong_service),
          "wrong service rejected");

    auto empty_serial = candidate("", "/dev/cu.usbmodem1101", 0x101);
    check(!is_reference_device_candidate(empty_serial), "empty serial rejected");

    auto missing_endpoint = candidate("TEST-SERIAL-A", "", 0x101);
    check(!is_reference_device_candidate(missing_endpoint),
          "missing endpoint rejected");

    auto unrelated_serial_service =
        candidate("TEST-SERIAL-A", "/dev/cu.Bluetooth-Incoming-Port", 0x101);
    unrelated_serial_service.tty_device = "Bluetooth-Incoming-Port";
    check(!is_reference_device_candidate(unrelated_serial_service),
          "unrelated serial service rejected");

    auto dialin_only = candidate("TEST-SERIAL-A", "", 0x101);
    dialin_only.dialin_device = "/dev/tty.usbmodem1101";
    check(!is_reference_device_candidate(dialin_only),
          "dialin-only candidate rejected");

    auto first = candidate("TEST-SERIAL-A", "/dev/cu.usbmodem1101", 0x101);
    auto renamed = candidate("TEST-SERIAL-A", "/dev/cu.usbmodem2201", 0x202);
    check(make_macos_device_id(first.serial) == make_macos_device_id(renamed.serial),
          "endpoint rename preserves device id");

    auto second = candidate("TEST-SERIAL-B", "/dev/cu.usbmodem3301", 0x303);
    check(make_macos_device_id(first.serial) != make_macos_device_id(second.serial),
          "different serial changes id");

    auto empty_snapshot = make_discovery_snapshot({});
    check(empty_snapshot.status == DiscoverySnapshotStatus::Complete &&
              empty_snapshot.devices.empty(),
          "empty snapshot");

    auto ordered_snapshot = make_discovery_snapshot({second, first});
    check(ordered_snapshot.devices.size() == 2, "two devices");
    check(ordered_snapshot.devices.front().id == make_macos_device_id(first.serial),
          "deterministic ordering");

    auto mixed_snapshot = make_discovery_snapshot(
        {first, unrelated_serial_service, dialin_only});
    check(mixed_snapshot.status == DiscoverySnapshotStatus::Complete,
          "mixed snapshot remains complete");
    check(mixed_snapshot.devices.size() == 1,
          "unrelated serial services ignored");

    auto duplicate_snapshot = make_discovery_snapshot({first, renamed});
    check(duplicate_snapshot.status == DiscoverySnapshotStatus::DuplicateIdentity,
          "duplicate fail closed");
    check(duplicate_snapshot.devices.empty(), "duplicates publish no devices");

    MacDeviceInventory inventory;
    inventory.replace(ordered_snapshot, {second, first});
    check(inventory.snapshot().devices.size() == 2, "inventory snapshot");

    const auto resolved_first = inventory.resolve(ordered_snapshot.devices.front());
    check(resolved_first && resolved_first->explicit_port == first.callout_device,
          "inventory resolves first");

    const auto resolved_second = inventory.resolve(ordered_snapshot.devices.back());
    check(resolved_second && resolved_second->explicit_port == second.callout_device,
          "inventory resolves second");

    inventory.replace(make_discovery_snapshot({renamed}), {renamed});
    const auto refreshed = inventory.snapshot();
    check(refreshed.devices.size() == 1, "reconnect refresh count");
    check(refreshed.devices.front().id == make_macos_device_id(first.serial),
          "reconnect preserves id");
    check(refreshed.devices.front().locator != ordered_snapshot.devices.front().locator,
          "reconnect refreshes opaque locator");
    check(!inventory.resolve(ordered_snapshot.devices.front()),
          "stale locator rejected after refresh");
    const auto refreshed_entry = inventory.resolve(refreshed.devices.front());
    check(refreshed_entry &&
              refreshed_entry->explicit_port == renamed.callout_device,
          "refreshed endpoint resolved");

    std::cout << "macOS discovery model tests passed\n";
    return 0;
  } catch (const char* message) {
    std::cerr << message << '\n';
    return 1;
  }
}

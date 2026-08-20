#include <iomanip>
#include <iostream>

#include "cec_injector/mate/macos/mac_device_inventory.h"

using namespace cec_injector::mate::macos_platform;

int main() {
  try {
    MacDeviceDiscovery discovery;
    const auto candidates = discovery.enumerate_candidates();
    const auto snapshot = make_discovery_snapshot(candidates);

    MacDeviceInventory inventory;
    inventory.replace(snapshot, candidates);

    std::cout << "raw_candidates=" << candidates.size() << '\n';
    for (const auto& candidate : candidates) {
      std::cout << "candidate tty=" << candidate.tty_device
                << " callout_present="
                << (!candidate.callout_device.empty() ? "yes" : "no")
                << " vid=0x" << std::hex << std::setw(4) << std::setfill('0')
                << candidate.vendor_id << " pid=0x" << std::setw(4)
                << candidate.product_id << std::dec
                << " client_type=" << candidate.serial_client_type
                << " usb_ancestry="
                << (candidate.has_usb_device_ancestry ? "yes" : "no")
                << " matched="
                << (is_reference_device_candidate(candidate) ? "yes" : "no")
                << '\n';
    }
    std::cout << "devices=" << snapshot.devices.size() << '\n';
    for (const auto& device : snapshot.devices) {
      const auto resolved = inventory.resolve(device);
      std::cout << "DeviceId=" << device.id.diagnostic_value() << '\n';
      std::cout << "locator=opaque\n";
      std::cout << "endpoint_kind="
                << (resolved && resolved->endpoint_kind == MacEndpointKind::Callout
                        ? "callout"
                        : "unknown")
                << '\n';
      std::cout << "endpoint_present="
                << (resolved && !resolved->explicit_port.empty() ? "yes" : "no")
                << '\n';
    }
    return snapshot.status == DiscoverySnapshotStatus::Complete ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include "cec_injector/mate/macos/mac_device_discovery.h"

#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace cec_injector::mate::macos_platform {
namespace {

constexpr std::string_view kDeviceIdPrefix = "macos-usb-serial-v1:";
constexpr std::string_view kLocatorPrefix = "macos-ioserialbsd-v1:";

std::string encode_bytes(std::string_view value) {
  constexpr std::array digits{'0', '1', '2', '3', '4', '5', '6', '7',
                              '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const unsigned char byte : value) {
    encoded.push_back(digits[byte >> 4]);
    encoded.push_back(digits[byte & 0x0f]);
  }
  return encoded;
}

std::string encode_registry_id(std::uint64_t value) {
  constexpr std::array digits{'0', '1', '2', '3', '4', '5', '6', '7',
                              '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string encoded(16, '0');
  for (std::size_t index = encoded.size(); index-- > 0;) {
    encoded[index] = digits[value & 0x0f];
    value >>= 4;
  }
  return encoded;
}

PlatformDeviceLocator make_locator(std::uint64_t registry_entry_id) {
  if (registry_entry_id == 0) return {};
  return PlatformDeviceLocator(std::string(kLocatorPrefix) +
                               encode_registry_id(registry_entry_id));
}

}  // namespace

bool is_reference_device_candidate(const MacDeviceCandidate& candidate) {
  return candidate.vendor_id == kReferenceVendorId &&
         candidate.product_id == kReferenceProductId &&
         !candidate.serial.empty() && !candidate.callout_device.empty() &&
         candidate.callout_device.starts_with("/dev/cu.") &&
         candidate.has_usb_device_ancestry &&
         candidate.serial_client_type == "IOSerialStream" &&
         candidate.tty_device.starts_with("usbmodem") &&
         candidate.registry_entry_id != 0;
}

DeviceId make_macos_device_id(std::string_view serial) {
  if (serial.empty()) throw std::invalid_argument("macOS Device serial is empty");
  return DeviceId(std::string(kDeviceIdPrefix) + encode_bytes(serial));
}

MacDiscoverySnapshot make_discovery_snapshot(
    const std::vector<MacDeviceCandidate>& candidates) {
  std::map<DeviceId, std::vector<const MacDeviceCandidate*>> by_id;
  for (const auto& candidate : candidates) {
    if (!is_reference_device_candidate(candidate)) continue;
    by_id[make_macos_device_id(candidate.serial)].push_back(&candidate);
  }

  MacDiscoverySnapshot snapshot;
  for (const auto& [id, matches] : by_id) {
    if (matches.size() > 1) snapshot.duplicate_ids.push_back(id);
  }
  if (!snapshot.duplicate_ids.empty()) {
    snapshot.status = DiscoverySnapshotStatus::DuplicateIdentity;
    return snapshot;
  }

  for (const auto& [id, matches] : by_id) {
    const auto& candidate = *matches.front();
    snapshot.devices.push_back(
        {id, make_locator(candidate.registry_entry_id),
         "CEC Injector development adapter " + candidate.serial});
  }
  std::sort(snapshot.devices.begin(), snapshot.devices.end(),
            [](const DiscoveredDevice& left, const DiscoveredDevice& right) {
              if (left.id == right.id) {
                return left.display_name < right.display_name;
              }
              return left.id < right.id;
            });
  return snapshot;
}

MacDeviceDiscovery::MacDeviceDiscovery(Enumerator enumerator)
    : enumerator_(std::move(enumerator)) {}

std::vector<MacDeviceCandidate> MacDeviceDiscovery::enumerate_candidates() const {
  return enumerator_ ? enumerator_() : std::vector<MacDeviceCandidate>{};
}

MacDiscoverySnapshot MacDeviceDiscovery::enumerate() const {
  return make_discovery_snapshot(enumerate_candidates());
}

}  // namespace cec_injector::mate::macos_platform

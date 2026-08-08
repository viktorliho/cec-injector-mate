#include "cec_injector/mate/linux/linux_device_discovery.h"

#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace cec_injector::mate::linux_platform {
namespace {

constexpr std::string_view kByIdPrefix = "/dev/serial/by-id/";
constexpr std::string_view kDeviceIdPrefix = "linux-usb-serial-v1:";

std::string EncodeSerial(std::string_view serial) {
  constexpr std::array digits{'0', '1', '2', '3', '4', '5', '6', '7',
                              '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string encoded;
  encoded.reserve(serial.size() * 2);
  for (const unsigned char byte : serial) {
    encoded.push_back(digits[byte >> 4]);
    encoded.push_back(digits[byte & 0x0f]);
  }
  return encoded;
}

}  // namespace

bool is_reference_device_candidate(const LinuxDeviceCandidate& candidate) {
  return candidate.vendor_id == kReferenceVendorId &&
         candidate.product_id == kReferenceProductId &&
         candidate.driver == "cdc_acm" &&
         candidate.interface_class == "0202" && !candidate.serial.empty() &&
         candidate.stable_path.starts_with(kByIdPrefix) &&
         candidate.stable_path.size() > kByIdPrefix.size();
}

DeviceId make_linux_device_id(std::string_view serial) {
  if (serial.empty()) throw std::invalid_argument("Linux Device serial is empty");
  return DeviceId(std::string(kDeviceIdPrefix) + EncodeSerial(serial));
}

LinuxDiscoverySnapshot make_discovery_snapshot(
    const std::vector<LinuxDeviceCandidate>& candidates) {
  std::map<DeviceId, std::vector<const LinuxDeviceCandidate*>> by_id;
  for (const auto& candidate : candidates) {
    if (!is_reference_device_candidate(candidate)) continue;
    by_id[make_linux_device_id(candidate.serial)].push_back(&candidate);
  }

  LinuxDiscoverySnapshot snapshot;
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
        {id, PlatformDeviceLocator(candidate.stable_path),
         "CEC Injector development adapter " + candidate.serial});
  }
  return snapshot;
}

bool should_reenumerate(const UdevEvent& event) {
  if (event.subsystem != "tty") return false;
  return event.action == UdevAction::Add || event.action == UdevAction::Remove ||
         event.action == UdevAction::Change;
}

}  // namespace cec_injector::mate::linux_platform

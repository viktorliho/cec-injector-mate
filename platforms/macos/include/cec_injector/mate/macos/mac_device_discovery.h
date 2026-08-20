#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::macos_platform {

inline constexpr std::uint16_t kReferenceVendorId = 0xcafe;
inline constexpr std::uint16_t kReferenceProductId = 0xcec1;

enum class MacEndpointKind { Callout, DialIn };

struct MacDeviceCandidate {
  std::string callout_device;
  std::string dialin_device;
  std::uint16_t vendor_id{};
  std::uint16_t product_id{};
  std::string serial;
  std::string tty_device;
  std::string serial_client_type;
  bool has_usb_device_ancestry{};
  std::uint64_t registry_entry_id{};
};

enum class DiscoverySnapshotStatus { Complete, DuplicateIdentity };

struct MacDiscoverySnapshot {
  DiscoverySnapshotStatus status{DiscoverySnapshotStatus::Complete};
  std::vector<DiscoveredDevice> devices;
  std::vector<DeviceId> duplicate_ids;
};

[[nodiscard]] bool is_reference_device_candidate(
    const MacDeviceCandidate& candidate);
[[nodiscard]] DeviceId make_macos_device_id(std::string_view serial);
[[nodiscard]] MacDiscoverySnapshot make_discovery_snapshot(
    const std::vector<MacDeviceCandidate>& candidates);

// Production seam for native macOS device enumeration. Future
// IOServiceAddMatchingNotification callbacks can publish a fresh complete
// snapshot by reusing enumerate_candidates()/enumerate() without changing
// Core, Service, or locator ownership.
class MacDeviceDiscovery {
 public:
  using Enumerator = std::function<std::vector<MacDeviceCandidate>()>;

  MacDeviceDiscovery();
  explicit MacDeviceDiscovery(Enumerator enumerator);

  [[nodiscard]] std::vector<MacDeviceCandidate> enumerate_candidates() const;
  [[nodiscard]] MacDiscoverySnapshot enumerate() const;

 private:
  Enumerator enumerator_;
};

}  // namespace cec_injector::mate::macos_platform

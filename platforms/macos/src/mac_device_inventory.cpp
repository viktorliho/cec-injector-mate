#include "cec_injector/mate/macos/mac_device_inventory.h"

#include <mutex>
#include <utility>

namespace cec_injector::mate::macos_platform {

struct MacDeviceInventory::Impl {
  mutable std::mutex mutex;
  MacDiscoverySnapshot snapshot;
  std::vector<MacInventoryEntry> entries;
};

MacDeviceInventory::MacDeviceInventory() : impl_(std::make_shared<Impl>()) {}

void MacDeviceInventory::replace(MacDiscoverySnapshot snapshot,
                                 const std::vector<MacDeviceCandidate>& candidates) {
  std::vector<MacInventoryEntry> entries;
  if (snapshot.status == DiscoverySnapshotStatus::Complete) {
    for (const auto& discovered : snapshot.devices) {
      for (const auto& candidate : candidates) {
        if (!is_reference_device_candidate(candidate) ||
            make_macos_device_id(candidate.serial) != discovered.id ||
            make_discovery_snapshot({candidate}).devices.empty() ||
            make_discovery_snapshot({candidate}).devices.front().locator !=
                discovered.locator) {
          continue;
        }
        entries.push_back({discovered, candidate.callout_device,
                           MacEndpointKind::Callout, candidate.serial,
                           candidate.registry_entry_id});
        break;
      }
    }
  }
  std::lock_guard lock(impl_->mutex);
  impl_->snapshot = std::move(snapshot);
  impl_->entries = std::move(entries);
}

MacDiscoverySnapshot MacDeviceInventory::snapshot() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->snapshot;
}

std::optional<MacInventoryEntry> MacDeviceInventory::resolve(
    const DiscoveredDevice& device) const {
  std::lock_guard lock(impl_->mutex);
  const MacInventoryEntry* match = nullptr;
  for (const auto& entry : impl_->entries) {
    if (entry.discovered.id != device.id ||
        entry.discovered.locator != device.locator) {
      continue;
    }
    if (match) return std::nullopt;
    match = &entry;
  }
  return match ? std::optional<MacInventoryEntry>(*match) : std::nullopt;
}

}  // namespace cec_injector::mate::macos_platform

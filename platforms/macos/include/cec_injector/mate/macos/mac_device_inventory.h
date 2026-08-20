#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cec_injector/mate/macos/mac_device_discovery.h"

namespace cec_injector::mate::macos_platform {

struct MacInventoryEntry {
  DiscoveredDevice discovered;
  std::string explicit_port;
  MacEndpointKind endpoint_kind{MacEndpointKind::Callout};
  std::string serial;
  std::uint64_t registry_entry_id{};
};

class MacDeviceInventory {
 public:
  MacDeviceInventory();

  void replace(MacDiscoverySnapshot snapshot,
               const std::vector<MacDeviceCandidate>& candidates);
  [[nodiscard]] MacDiscoverySnapshot snapshot() const;
  [[nodiscard]] std::optional<MacInventoryEntry> resolve(
      const DiscoveredDevice& device) const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace cec_injector::mate::macos_platform

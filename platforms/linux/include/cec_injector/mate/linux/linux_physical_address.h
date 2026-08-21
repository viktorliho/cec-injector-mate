#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cec_injector/mate/edid/physical_address.h"
#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::linux_platform {

using edid::EdidPhysicalAddressResult;
using edid::EdidPhysicalAddressStatus;
using edid::parse_edid_physical_address;
using edid::physical_address_valid_for_source;

struct DrmConnectorObservation {
  std::string name;
  bool connected{};
  std::optional<std::vector<std::uint8_t>> edid;
};

struct LinuxPhysicalAddressObservation {
  PhysicalAddressResolution resolution;
  std::optional<std::string> topology_identity;
  // True only when DRM enumerated at least one connector and every connector
  // explicitly reported disconnected. This is distinct from an empty,
  // unreadable, or partially unresolved connector snapshot.
  bool all_connectors_disconnected{};
};

struct PhysicalAddressCacheEntry {
  std::uint16_t address{};
  std::string topology_identity;

  bool operator==(const PhysicalAddressCacheEntry&) const = default;
};

using DrmEdidReader = std::function<std::optional<std::vector<std::uint8_t>>(
    const std::filesystem::path& connector)>;

[[nodiscard]] LinuxPhysicalAddressObservation observe_physical_address(
    const std::vector<DrmConnectorObservation>& connectors);
[[nodiscard]] PhysicalAddressResolution apply_physical_address_cache(
    const LinuxPhysicalAddressObservation& live,
    const std::optional<PhysicalAddressCacheEntry>& cache);
[[nodiscard]] std::optional<PhysicalAddressCacheEntry>
load_physical_address_cache(const std::filesystem::path& path);
[[nodiscard]] bool store_physical_address_cache(
    const std::filesystem::path& path,
    const PhysicalAddressCacheEntry& entry);

class LinuxPhysicalAddressResolver {
 public:
  LinuxPhysicalAddressResolver(std::filesystem::path drm_root,
                               std::filesystem::path cache_path,
                               DrmEdidReader drm_edid_reader = {});

  [[nodiscard]] PhysicalAddressResolution resolve() const;
  // Read-only live query for platform diagnostics. It neither reads nor
  // writes the configured cache.
  [[nodiscard]] PhysicalAddressResolution resolve_live() const;
  [[nodiscard]] DisplaySummary display_summary() const;

 private:
  std::filesystem::path drm_root_;
  std::filesystem::path cache_path_;
  DrmEdidReader drm_edid_reader_;
};

// Linux production source. Correlates the sysfs connector with its DRM card
// and connector id before reading the KMS EDID property.
[[nodiscard]] DrmEdidReader create_libdrm_edid_reader();

}  // namespace cec_injector::mate::linux_platform

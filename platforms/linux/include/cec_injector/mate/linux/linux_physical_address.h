#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::linux_platform {

enum class EdidPhysicalAddressStatus {
  Invalid,
  NoPhysicalAddress,
  PhysicalAddress,
};

struct EdidPhysicalAddressResult {
  EdidPhysicalAddressStatus status{EdidPhysicalAddressStatus::Invalid};
  std::optional<std::uint16_t> address;
};

struct DrmConnectorObservation {
  std::string name;
  bool connected{};
  std::optional<std::vector<std::uint8_t>> edid;
};

struct LinuxPhysicalAddressObservation {
  PhysicalAddressResolution resolution;
  std::optional<std::string> topology_identity;
};

struct PhysicalAddressCacheEntry {
  std::uint16_t address{};
  std::string topology_identity;

  bool operator==(const PhysicalAddressCacheEntry&) const = default;
};

using DrmEdidReader = std::function<std::optional<std::vector<std::uint8_t>>(
    const std::filesystem::path& connector)>;

[[nodiscard]] bool physical_address_valid_for_source(std::uint16_t address);
[[nodiscard]] EdidPhysicalAddressResult parse_edid_physical_address(
    std::span<const std::uint8_t> edid);
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

 private:
  std::filesystem::path drm_root_;
  std::filesystem::path cache_path_;
  DrmEdidReader drm_edid_reader_;
};

// Linux production source. Correlates the sysfs connector with its DRM card
// and connector id before reading the KMS EDID property.
[[nodiscard]] DrmEdidReader create_libdrm_edid_reader();

}  // namespace cec_injector::mate::linux_platform

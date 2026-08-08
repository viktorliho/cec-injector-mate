#pragma once

#include <cstdint>
#include <filesystem>
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
                               std::filesystem::path cache_path);

  [[nodiscard]] PhysicalAddressResolution resolve() const;

 private:
  std::filesystem::path drm_root_;
  std::filesystem::path cache_path_;
};

}  // namespace cec_injector::mate::linux_platform

#include "cec_injector/mate/linux/linux_physical_address.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace cec_injector::mate::linux_platform {
namespace {

std::optional<std::vector<std::uint8_t>> ReadBinary(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
}

std::optional<bool> Connected(const std::filesystem::path& connector) {
  std::ifstream input(connector / "status");
  std::string status;
  input >> status;
  if (!input) return std::nullopt;
  return status == "connected";
}

std::optional<std::vector<DrmConnectorObservation>> ReadConnectors(
    const std::filesystem::path& drm_root) {
  std::vector<DrmConnectorObservation> connectors;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(drm_root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    const bool directory = iterator->is_directory(error);
    if (error) return std::nullopt;
    const bool symlink = iterator->is_symlink(error);
    if (error) return std::nullopt;
    if (!directory && !symlink) continue;
    const auto path = iterator->path();
    const auto connected = Connected(path);
    if (!connected) return std::nullopt;
    connectors.push_back(
        {path.filename().string(), *connected,
         *connected ? ReadBinary(path / "edid") : std::nullopt});
  }
  if (error) return std::nullopt;
  return connectors;
}

}  // namespace

LinuxPhysicalAddressResolver::LinuxPhysicalAddressResolver(
    std::filesystem::path drm_root, std::filesystem::path cache_path)
    : drm_root_(std::move(drm_root)), cache_path_(std::move(cache_path)) {
  if (drm_root_.empty() || cache_path_.empty()) {
    throw std::invalid_argument("DRM root and PA cache path are required");
  }
}

PhysicalAddressResolution LinuxPhysicalAddressResolver::resolve() const {
  const auto connectors = ReadConnectors(drm_root_);
  if (!connectors) {
    return {PhysicalAddressStatus::Unavailable, std::nullopt};
  }
  const auto live = observe_physical_address(*connectors);
  if (live.resolution.status == PhysicalAddressStatus::Available &&
      live.topology_identity) {
    (void)store_physical_address_cache(
        cache_path_, {*live.resolution.address, *live.topology_identity});
    return live.resolution;
  }
  if (live.resolution.status == PhysicalAddressStatus::Ambiguous) {
    return live.resolution;
  }
  return apply_physical_address_cache(live,
                                      load_physical_address_cache(cache_path_));
}

}  // namespace cec_injector::mate::linux_platform

#include "cec_injector/mate/linux/linux_physical_address.h"

#include <algorithm>
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
  if (status == "connected") return true;
  if (status == "disconnected" || status == "unknown") return false;
  return std::nullopt;
}

bool ValidEdid(const std::optional<std::vector<std::uint8_t>>& edid) {
  return edid && parse_edid_physical_address(*edid).status !=
                     EdidPhysicalAddressStatus::Invalid;
}

std::optional<std::vector<std::uint8_t>> SelectEdid(
    std::optional<std::vector<std::uint8_t>> sysfs,
    std::optional<std::vector<std::uint8_t>> drm) {
  const bool valid_sysfs = ValidEdid(sysfs);
  const bool valid_drm = ValidEdid(drm);
  if (valid_sysfs && valid_drm) {
    return *sysfs == *drm ? std::move(sysfs) : std::nullopt;
  }
  if (valid_sysfs) return sysfs;
  if (valid_drm) return drm;
  // Preserve the existing observation/cache behavior for malformed live
  // evidence: a structurally usable base block can still provide topology
  // identity while the parser marks the connector unresolved.
  if (sysfs) return sysfs;
  return drm;
}

std::optional<std::vector<DrmConnectorObservation>> ReadConnectors(
    const std::filesystem::path& drm_root,
    const DrmEdidReader& drm_edid_reader) {
  std::vector<std::filesystem::path> paths;
  std::vector<DrmConnectorObservation> connectors;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(drm_root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    const bool directory = iterator->is_directory(error);
    if (error) return std::nullopt;
    const bool symlink = iterator->is_symlink(error);
    if (error) return std::nullopt;
    if (!directory && !symlink) continue;
    paths.push_back(iterator->path());
  }
  if (error) return std::nullopt;
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    const auto status_path = path / "status";
    const bool status_exists = std::filesystem::exists(status_path, error);
    if (error) return std::nullopt;
    if (!status_exists) continue;
    const auto connected = Connected(path);
    if (!connected) return std::nullopt;
    std::optional<std::vector<std::uint8_t>> edid;
    if (*connected) {
      auto sysfs = ReadBinary(path / "edid");
      std::optional<std::vector<std::uint8_t>> drm;
      if (drm_edid_reader) drm = drm_edid_reader(path);
      edid = SelectEdid(std::move(sysfs), std::move(drm));
    }
    connectors.push_back({path.filename().string(), *connected,
                          std::move(edid)});
  }
  return connectors;
}

}  // namespace

LinuxPhysicalAddressResolver::LinuxPhysicalAddressResolver(
    std::filesystem::path drm_root, std::filesystem::path cache_path,
    DrmEdidReader drm_edid_reader)
    : drm_root_(std::move(drm_root)), cache_path_(std::move(cache_path)),
      drm_edid_reader_(std::move(drm_edid_reader)) {
  if (drm_root_.empty() || cache_path_.empty()) {
    throw std::invalid_argument("DRM root and PA cache path are required");
  }
}

PhysicalAddressResolution LinuxPhysicalAddressResolver::resolve() const {
  const auto connectors = ReadConnectors(drm_root_, drm_edid_reader_);
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

PhysicalAddressResolution LinuxPhysicalAddressResolver::resolve_live() const {
  const auto connectors = ReadConnectors(drm_root_, drm_edid_reader_);
  if (!connectors) {
    return {PhysicalAddressStatus::Unavailable, std::nullopt};
  }
  return observe_physical_address(*connectors).resolution;
}

DisplaySummary LinuxPhysicalAddressResolver::display_summary() const {
  const auto connectors = ReadConnectors(drm_root_, drm_edid_reader_);
  if (!connectors) return {};
  const DrmConnectorObservation* selected = nullptr;
  for (const auto& connector : *connectors) {
    if (!connector.connected || !connector.edid) continue;
    if (selected) return {};
    selected = &connector;
  }
  if (!selected) return {};
  return {selected->name, true, selected->edid->size()};
}

}  // namespace cec_injector::mate::linux_platform

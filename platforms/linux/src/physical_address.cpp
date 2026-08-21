#include "cec_injector/mate/linux/linux_physical_address.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>

namespace cec_injector::mate::linux_platform {
namespace {
constexpr std::string_view kTopologyPrefix = "topology-v1:";

std::optional<std::string> TopologyIdentity(
    const std::vector<DrmConnectorObservation>& connectors) {
  std::vector<const DrmConnectorObservation*> connected;
  for (const auto& connector : connectors) {
    if (!connector.connected) continue;
    if (!connector.edid ||
        parse_edid_physical_address(*connector.edid).status ==
            EdidPhysicalAddressStatus::Invalid) {
      return std::nullopt;
    }
    connected.push_back(&connector);
  }
  if (connected.empty()) return std::nullopt;
  std::sort(connected.begin(), connected.end(), [](const auto* left,
                                                    const auto* right) {
    return left->name < right->name;
  });

  // FNV-1a is only a compact deterministic cache key, not a security digest.
  std::uint64_t digest = 14695981039346656037ULL;
  const auto add = [&digest](std::uint8_t byte) {
    digest ^= byte;
    digest *= 1099511628211ULL;
  };
  for (const auto* connector : connected) {
    for (const unsigned char byte : connector->name) add(byte);
    add(0);
    for (const auto byte : std::span(*connector->edid).first(128)) {
      add(byte);
    }
  }
  std::ostringstream formatted;
  formatted << kTopologyPrefix << std::hex << std::setw(16)
            << std::setfill('0') << digest;
  return formatted.str();
}

bool TopologyIdentityValid(std::string_view identity) {
  if (!identity.starts_with(kTopologyPrefix) ||
      identity.size() != kTopologyPrefix.size() + 16) {
    return false;
  }
  for (const char value : identity.substr(kTopologyPrefix.size())) {
    if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
      return false;
    }
  }
  return true;
}

}  // namespace

LinuxPhysicalAddressObservation observe_physical_address(
    const std::vector<DrmConnectorObservation>& connectors) {
  std::set<std::uint16_t> addresses;
  bool unresolved_connected = false;
  for (const auto& connector : connectors) {
    if (!connector.connected) continue;
    if (!connector.edid) {
      unresolved_connected = true;
      continue;
    }
    const auto parsed = parse_edid_physical_address(*connector.edid);
    if (parsed.status == EdidPhysicalAddressStatus::PhysicalAddress) {
      addresses.insert(*parsed.address);
    } else if (parsed.status == EdidPhysicalAddressStatus::Invalid) {
      unresolved_connected = true;
    }
  }

  LinuxPhysicalAddressObservation observation;
  observation.all_connectors_disconnected =
      !connectors.empty() &&
      std::none_of(connectors.begin(), connectors.end(),
                   [](const auto& connector) { return connector.connected; });
  observation.topology_identity = TopologyIdentity(connectors);
  if (addresses.size() > 1 ||
      (addresses.size() == 1 && unresolved_connected)) {
    observation.resolution = {PhysicalAddressStatus::Ambiguous, std::nullopt};
  } else if (addresses.empty()) {
    observation.resolution = {PhysicalAddressStatus::Unavailable, std::nullopt};
  } else {
    observation.resolution = {PhysicalAddressStatus::Available,
                              *addresses.begin()};
  }
  return observation;
}

PhysicalAddressResolution apply_physical_address_cache(
    const LinuxPhysicalAddressObservation& live,
    const std::optional<PhysicalAddressCacheEntry>& cache) {
  if (live.resolution.status != PhysicalAddressStatus::Unavailable) {
    return live.resolution;
  }
  const bool topology_matches =
      live.topology_identity &&
      cache && cache->topology_identity == *live.topology_identity;
  if (!cache || (!topology_matches && !live.all_connectors_disconnected) ||
      !physical_address_valid_for_source(cache->address)) {
    return live.resolution;
  }
  return {PhysicalAddressStatus::Available, cache->address};
}

std::optional<PhysicalAddressCacheEntry> load_physical_address_cache(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string version;
  std::string address_text;
  std::string topology_text;
  std::string extra;
  if (!std::getline(input, version) || !std::getline(input, address_text) ||
      !std::getline(input, topology_text) || std::getline(input, extra) ||
      version != "version=2" || !address_text.starts_with("address=") ||
      !topology_text.starts_with("topology=")) {
    return std::nullopt;
  }
  const auto value_text = std::string_view(address_text).substr(8);
  unsigned value = 0;
  const auto [end, error] = std::from_chars(
      value_text.data(), value_text.data() + value_text.size(), value, 16);
  const auto identity = topology_text.substr(9);
  if (error != std::errc{} || end != value_text.data() + value_text.size() ||
      value > 0xffff ||
      !physical_address_valid_for_source(static_cast<std::uint16_t>(value)) ||
      !TopologyIdentityValid(identity)) {
    return std::nullopt;
  }
  return PhysicalAddressCacheEntry{static_cast<std::uint16_t>(value), identity};
}

bool store_physical_address_cache(const std::filesystem::path& path,
                                  const PhysicalAddressCacheEntry& entry) {
  if (!physical_address_valid_for_source(entry.address) ||
      !TopologyIdentityValid(entry.topology_identity) || path.empty()) {
    return false;
  }
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
  }
  const auto temporary = path.string() + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  output << "version=2\naddress=" << std::uppercase << std::hex
         << std::setw(4) << std::setfill('0') << entry.address
         << "\ntopology=" << entry.topology_identity << '\n';
  output.close();
  if (!output) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

}  // namespace cec_injector::mate::linux_platform

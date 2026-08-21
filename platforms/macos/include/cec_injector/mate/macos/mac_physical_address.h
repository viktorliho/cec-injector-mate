#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::macos_platform {

struct MacDisplayCandidate {
  std::uint64_t registry_entry_id{};
  std::string service_class;
  std::string product_name;
  std::string transport_description;
  bool active{};
  bool plausible_external{};
  std::optional<std::vector<std::uint8_t>> edid;
};

struct MacPhysicalAddressObservation {
  PhysicalAddressResolution resolution;
  std::size_t plausible_external_candidates{};
  std::size_t candidates_with_edid{};
  std::vector<std::uint16_t> unique_addresses;
};

[[nodiscard]] MacPhysicalAddressObservation observe_physical_address(
    const std::vector<MacDisplayCandidate>& candidates);

class MacPhysicalAddressResolver {
 public:
  using Enumerator = std::function<std::vector<MacDisplayCandidate>()>;

  MacPhysicalAddressResolver();
  explicit MacPhysicalAddressResolver(Enumerator enumerator);

  [[nodiscard]] std::vector<MacDisplayCandidate> enumerate_candidates() const;
  [[nodiscard]] MacPhysicalAddressObservation observe() const;
  [[nodiscard]] PhysicalAddressResolution resolve() const;

 private:
  Enumerator enumerator_;
};

}  // namespace cec_injector::mate::macos_platform

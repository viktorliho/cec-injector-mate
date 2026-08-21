#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace cec_injector::mate::edid {

enum class EdidPhysicalAddressStatus {
  Invalid,
  NoPhysicalAddress,
  PhysicalAddress,
};

struct EdidPhysicalAddressResult {
  EdidPhysicalAddressStatus status{EdidPhysicalAddressStatus::Invalid};
  std::optional<std::uint16_t> address;
};

[[nodiscard]] bool edid_base_block_valid(std::span<const std::uint8_t> edid);
[[nodiscard]] bool physical_address_valid_for_source(std::uint16_t address);
[[nodiscard]] EdidPhysicalAddressResult parse_edid_physical_address(
    std::span<const std::uint8_t> edid);

}  // namespace cec_injector::mate::edid

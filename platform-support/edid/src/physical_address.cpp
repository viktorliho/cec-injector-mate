#include "cec_injector/mate/edid/physical_address.h"

#include <algorithm>
#include <array>

namespace cec_injector::mate::edid {
namespace {

constexpr std::size_t kEdidBlockSize = 128;
constexpr std::array<std::uint8_t, 8> kEdidHeader{
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

bool block_checksum_valid(std::span<const std::uint8_t> block) {
  if (block.size() != kEdidBlockSize) return false;
  std::uint8_t checksum = 0;
  for (const auto byte : block) {
    checksum = static_cast<std::uint8_t>(checksum + byte);
  }
  return checksum == 0;
}

bool base_block_valid(std::span<const std::uint8_t> edid) {
  return edid.size() >= kEdidBlockSize &&
         std::equal(kEdidHeader.begin(), kEdidHeader.end(), edid.begin()) &&
         block_checksum_valid(edid.first(kEdidBlockSize));
}

}  // namespace

bool physical_address_valid_for_source(std::uint16_t address) {
  if (address == 0 || address == 0xffff) return false;
  bool zero_seen = false;
  for (int shift = 12; shift >= 0; shift -= 4) {
    const auto nibble = static_cast<unsigned>((address >> shift) & 0xf);
    if (nibble == 0) {
      zero_seen = true;
    } else if (zero_seen) {
      return false;
    }
  }
  return true;
}

EdidPhysicalAddressResult parse_edid_physical_address(
    std::span<const std::uint8_t> edid) {
  if (!base_block_valid(edid) || edid.size() % kEdidBlockSize != 0) return {};
  const std::size_t declared_blocks = static_cast<std::size_t>(edid[126]) + 1;
  if (declared_blocks != edid.size() / kEdidBlockSize) return {};
  for (std::size_t offset = 0; offset < edid.size(); offset += kEdidBlockSize) {
    if (!block_checksum_valid(edid.subspan(offset, kEdidBlockSize))) return {};
  }

  std::optional<std::uint16_t> found;
  for (std::size_t block = 1; block < declared_blocks; ++block) {
    const std::size_t base = block * kEdidBlockSize;
    if (edid[base] != 0x02) continue;
    const std::size_t data_end = edid[base + 2] == 0 ? 127 : edid[base + 2];
    if (data_end < 4 || data_end > 127) return {};
    std::size_t position = 4;
    while (position < data_end) {
      const std::uint8_t header = edid[base + position];
      const std::uint8_t tag = header >> 5;
      const std::size_t length = header & 0x1f;
      const std::size_t payload = position + 1;
      if (payload + length > data_end) return {};
      if (tag == 0x03 && length >= 3 && edid[base + payload] == 0x03 &&
          edid[base + payload + 1] == 0x0c &&
          edid[base + payload + 2] == 0x00) {
        if (length < 5) return {};
        const auto address = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(edid[base + payload + 3]) << 8) |
            edid[base + payload + 4]);
        if (!physical_address_valid_for_source(address)) return {};
        if (found && *found != address) return {};
        found = address;
      }
      position = payload + length;
    }
  }
  if (!found) {
    return {EdidPhysicalAddressStatus::NoPhysicalAddress, std::nullopt};
  }
  return {EdidPhysicalAddressStatus::PhysicalAddress, found};
}

}  // namespace cec_injector::mate::edid

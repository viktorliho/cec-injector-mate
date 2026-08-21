#include <array>
#include <iostream>
#include <utility>

#include "cec_injector/mate/macos/mac_physical_address.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::macos_platform;

namespace {

constexpr std::size_t kBlockSize = 128;

void FixChecksum(std::vector<std::uint8_t>& bytes, std::size_t offset) {
  bytes[offset + 127] = 0;
  std::uint8_t sum = 0;
  for (std::size_t index = 0; index < 127; ++index) {
    sum = static_cast<std::uint8_t>(sum + bytes[offset + index]);
  }
  bytes[offset + 127] = static_cast<std::uint8_t>(0U - sum);
}

std::vector<std::uint8_t> BaseEdid(std::uint8_t extensions) {
  std::vector<std::uint8_t> edid(kBlockSize);
  constexpr std::array header{0x00, 0xff, 0xff, 0xff,
                              0xff, 0xff, 0xff, 0x00};
  std::copy(header.begin(), header.end(), edid.begin());
  edid[8] = 0x12;
  edid[9] = 0x34;
  edid[10] = 0x56;
  edid[11] = 0x78;
  edid[126] = extensions;
  FixChecksum(edid, 0);
  return edid;
}

std::vector<std::uint8_t> EdidWithPa(std::uint16_t address) {
  auto edid = BaseEdid(1);
  edid.resize(kBlockSize * 2);
  const std::size_t base = kBlockSize;
  edid[base] = 0x02;
  edid[base + 1] = 0x03;
  edid[base + 2] = 10;
  edid[base + 4] = 0x65;
  edid[base + 5] = 0x03;
  edid[base + 6] = 0x0c;
  edid[base + 7] = 0x00;
  edid[base + 8] = static_cast<std::uint8_t>(address >> 8);
  edid[base + 9] = static_cast<std::uint8_t>(address);
  FixChecksum(edid, base);
  return edid;
}

MacDisplayCandidate candidate(std::uint64_t id, bool external, bool active,
                              std::optional<std::vector<std::uint8_t>> edid) {
  MacDisplayCandidate value;
  value.registry_entry_id = id;
  value.service_class = "IOPortTransportStateDisplayPort";
  value.product_name = "test";
  value.transport_description = "test-port";
  value.active = active;
  value.plausible_external = external && active;
  value.edid = std::move(edid);
  return value;
}

void check(bool value, const char* message) {
  if (!value) throw message;
}

}  // namespace

int main() {
  try {
    auto observation = observe_physical_address({});
    check(observation.resolution.status == PhysicalAddressStatus::Unavailable,
          "empty snapshot unavailable");

    observation = observe_physical_address(
        {candidate(1, false, true, EdidWithPa(0x1000))});
    check(observation.resolution.status == PhysicalAddressStatus::Unavailable,
          "internal candidate ignored");

    observation = observe_physical_address(
        {candidate(2, true, true, EdidWithPa(0x1000))});
    check(observation.resolution ==
              PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000},
          "single valid external candidate");

    observation = observe_physical_address(
        {candidate(2, true, true, EdidWithPa(0x1000)),
         candidate(3, true, true, EdidWithPa(0x1000))});
    check(observation.resolution ==
              PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000},
          "duplicate identical PA deduplicated");

    observation = observe_physical_address(
        {candidate(2, true, true, EdidWithPa(0x1000)),
         candidate(3, true, true, EdidWithPa(0x2100))});
    check(observation.resolution.status == PhysicalAddressStatus::Ambiguous,
          "multiple different PA ambiguous");

    observation = observe_physical_address(
        {candidate(2, true, true, EdidWithPa(0x1000)),
         candidate(3, true, true, std::vector<std::uint8_t>{1, 2, 3})});
    check(observation.resolution.status == PhysicalAddressStatus::Ambiguous,
          "unreadable plausible external display blocks selection");

    observation = observe_physical_address(
        {candidate(2, true, false, std::vector<std::uint8_t>{1, 2, 3}),
         candidate(3, true, true, EdidWithPa(0x1000))});
    check(observation.resolution ==
              PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000},
          "inactive unresolved candidate ignored");

    std::cout << "macOS physical address tests passed\n";
    return 0;
  } catch (const char* message) {
    std::cerr << message << '\n';
    return 1;
  }
}

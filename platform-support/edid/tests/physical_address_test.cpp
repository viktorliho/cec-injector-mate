#include <array>
#include <vector>

#include "cec_injector/mate/edid/physical_address.h"
#include "test_support.h"

using namespace cec_injector::mate::edid;

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

std::vector<std::uint8_t> CtaWithoutHdmiVsdb() {
  auto edid = BaseEdid(1);
  edid.resize(kBlockSize * 2);
  const std::size_t base = kBlockSize;
  edid[base] = 0x02;
  edid[base + 1] = 0x03;
  edid[base + 2] = 6;
  edid[base + 4] = 0x21;
  edid[base + 5] = 0x01;
  FixChecksum(edid, base);
  return edid;
}

}  // namespace

int main() {
  CHECK(physical_address_valid_for_source(0x1000));
  CHECK(physical_address_valid_for_source(0x2100));
  CHECK(physical_address_valid_for_source(0x2130));
  CHECK(!physical_address_valid_for_source(0x0000));
  CHECK(!physical_address_valid_for_source(0xffff));
  CHECK(!physical_address_valid_for_source(0x2010));

  for (const auto address : {std::uint16_t{0x1000}, std::uint16_t{0x2100},
                             std::uint16_t{0x2130}}) {
    const auto parsed = parse_edid_physical_address(EdidWithPa(address));
    CHECK(parsed.status == EdidPhysicalAddressStatus::PhysicalAddress);
    CHECK(parsed.address == address);
  }

  CHECK(parse_edid_physical_address(BaseEdid(0)).status ==
        EdidPhysicalAddressStatus::NoPhysicalAddress);
  CHECK(parse_edid_physical_address(CtaWithoutHdmiVsdb()).status ==
        EdidPhysicalAddressStatus::NoPhysicalAddress);

  auto base_only = EdidWithPa(0x1000);
  base_only.resize(kBlockSize);
  CHECK(edid_base_block_valid(base_only));
  CHECK(parse_edid_physical_address(base_only).status ==
        EdidPhysicalAddressStatus::Invalid);

  auto invalid = EdidWithPa(0x1000);
  invalid[0] = 1;
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);
  invalid = EdidWithPa(0x1000);
  invalid.pop_back();
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);
  invalid = BaseEdid(1);
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);
  invalid = EdidWithPa(0x1000);
  invalid[20] ^= 1;
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);
  invalid = EdidWithPa(0x1000);
  invalid[140] ^= 1;
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);

  invalid = EdidWithPa(0x1000);
  invalid[130] = 8;
  FixChecksum(invalid, kBlockSize);
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);

  invalid = EdidWithPa(0x1000);
  invalid[132] = 0x63;
  invalid[133] = 0x03;
  invalid[134] = 0x0c;
  invalid[135] = 0x00;
  invalid[130] = 8;
  FixChecksum(invalid, kBlockSize);
  CHECK(parse_edid_physical_address(invalid).status ==
        EdidPhysicalAddressStatus::Invalid);

  auto wrong_oui = EdidWithPa(0x1000);
  wrong_oui[133] = 0x04;
  FixChecksum(wrong_oui, kBlockSize);
  CHECK(parse_edid_physical_address(wrong_oui).status ==
        EdidPhysicalAddressStatus::NoPhysicalAddress);

  auto conflicting = EdidWithPa(0x1000);
  conflicting.resize(kBlockSize * 3);
  const std::size_t base = kBlockSize * 2;
  conflicting[126] = 2;
  conflicting[base] = 0x02;
  conflicting[base + 1] = 0x03;
  conflicting[base + 2] = 10;
  conflicting[base + 4] = 0x65;
  conflicting[base + 5] = 0x03;
  conflicting[base + 6] = 0x0c;
  conflicting[base + 7] = 0x00;
  conflicting[base + 8] = 0x21;
  conflicting[base + 9] = 0x00;
  FixChecksum(conflicting, 0);
  FixChecksum(conflicting, base);
  CHECK(parse_edid_physical_address(conflicting).status ==
        EdidPhysicalAddressStatus::Invalid);

  return EXIT_SUCCESS;
}

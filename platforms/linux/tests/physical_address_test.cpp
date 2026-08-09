#include "cec_injector/mate/linux/linux_physical_address.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "test_support.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::linux_platform;

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

bool WriteBinary(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

bool WriteConnector(const std::filesystem::path& root, std::string name,
                    bool connected,
                    const std::optional<std::vector<std::uint8_t>>& edid) {
  const auto connector = root / name;
  std::filesystem::create_directories(connector);
  std::ofstream status(connector / "status");
  status << (connected ? "connected\n" : "disconnected\n");
  status.close();
  return status.good() && (!edid || WriteBinary(connector / "edid", *edid));
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

  const auto pa1000 = EdidWithPa(0x1000);
  const auto pa2100 = EdidWithPa(0x2100);
  auto observation = observe_physical_address({});
  CHECK(observation.resolution.status == PhysicalAddressStatus::Unavailable);
  CHECK(!observation.topology_identity);

  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, std::nullopt}});
  CHECK(observation.resolution.status == PhysicalAddressStatus::Unavailable);
  CHECK(!observation.topology_identity);

  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, pa1000}});
  CHECK(observation.resolution ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});
  CHECK(observation.topology_identity);
  const auto first_identity = *observation.topology_identity;

  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, pa1000},
       {"card1-HDMI-A-2", true, pa2100}});
  CHECK(observation.resolution.status == PhysicalAddressStatus::Ambiguous);
  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, pa1000},
       {"card1-HDMI-A-2", true, pa1000}});
  CHECK(observation.resolution ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});
  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, pa1000},
       {"card1-HDMI-A-2", false, pa2100}});
  CHECK(observation.resolution ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});

  const PhysicalAddressCacheEntry cache{0x1000, first_identity};
  auto partial = pa1000;
  partial.resize(kBlockSize);
  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, partial}});
  CHECK(observation.resolution.status == PhysicalAddressStatus::Unavailable);
  CHECK(observation.topology_identity == first_identity);
  CHECK(apply_physical_address_cache(observation, cache) ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});

  auto changed_base = partial;
  changed_base[20] ^= 1;
  FixChecksum(changed_base, 0);
  observation = observe_physical_address(
      {{"card0-HDMI-A-1", true, changed_base}});
  CHECK(observation.topology_identity != first_identity);
  CHECK(apply_physical_address_cache(observation, cache).status ==
        PhysicalAddressStatus::Unavailable);
  observation = observe_physical_address(
      {{"card1-HDMI-A-2", true, partial}});
  CHECK(observation.topology_identity != first_identity);
  CHECK(apply_physical_address_cache(observation, cache).status ==
        PhysicalAddressStatus::Unavailable);
  CHECK(apply_physical_address_cache(
            observe_physical_address({{"card0-HDMI-A-1", true, pa1000},
                                      {"card1-HDMI-A-2", true, pa2100}}),
            cache)
            .status == PhysicalAddressStatus::Ambiguous);

  const auto root = std::filesystem::temp_directory_path() /
                    ("mate-pa-test-" + std::to_string(getpid()));
  const auto drm = root / "drm";
  const auto cache_path = root / "state" / "physical-address";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(drm);

  CHECK(store_physical_address_cache(cache_path, cache));
  CHECK(load_physical_address_cache(cache_path) == cache);
  CHECK(!std::filesystem::exists(cache_path.string() + ".tmp"));
  { std::ofstream(cache_path) << "malformed\n"; }
  CHECK(!load_physical_address_cache(cache_path));

  LinuxPhysicalAddressResolver resolver(drm, cache_path);
  CHECK(resolver.resolve().status == PhysicalAddressStatus::Unavailable);

  // Real /sys/class/drm contains card/render nodes and a version file in
  // addition to connector symlinks. Non-connectors must not poison the
  // complete connector snapshot.
  std::filesystem::create_directories(drm / "card0");
  std::filesystem::create_directories(drm / "card1");
  std::filesystem::create_directories(drm / "renderD128");
  std::filesystem::create_directories(drm / "renderD129");
  { std::ofstream(drm / "version") << "drm 1.1.0\n"; }
  CHECK(resolver.resolve().status == PhysicalAddressStatus::Unavailable);
  CHECK(WriteConnector(drm, "card0-HDMI-A-1", true, pa1000));
  CHECK(resolver.resolve() ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});
  CHECK(load_physical_address_cache(cache_path));

  CHECK(WriteBinary(drm / "card0-HDMI-A-1" / "edid", partial));
  CHECK(resolver.resolve() ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});
  CHECK(WriteBinary(drm / "card0-HDMI-A-1" / "edid", pa1000));
  CHECK(WriteConnector(drm, "card1-HDMI-A-2", true, pa2100));
  CHECK(resolver.resolve().status == PhysicalAddressStatus::Ambiguous);
  std::filesystem::remove(drm / "card0-HDMI-A-1" / "edid");
  CHECK(resolver.resolve().status == PhysicalAddressStatus::Ambiguous);
  { std::ofstream(drm / "card1-HDMI-A-2" / "status") << "disconnected\n"; }
  CHECK(resolver.resolve().status == PhysicalAddressStatus::Unavailable);

  const auto source_root = root / "drm-source";
  const auto source_cache = root / "state" / "source-physical-address";
  std::filesystem::create_directories(source_root);
  std::map<std::string, std::optional<std::vector<std::uint8_t>>> drm_edids;
  std::vector<std::string> drm_requests;
  DrmEdidReader fake_drm = [&](const std::filesystem::path& connector) {
    const auto name = connector.filename().string();
    drm_requests.push_back(name);
    const auto found = drm_edids.find(name);
    return found == drm_edids.end() ? std::nullopt : found->second;
  };
  LinuxPhysicalAddressResolver source_resolver(source_root, source_cache,
                                                fake_drm);

  // SteamOS shape: empty sysfs EDID and valid KMS property.
  CHECK(WriteConnector(source_root, "card0-HDMI-A-2", true,
                       std::vector<std::uint8_t>{}));
  drm_edids["card0-HDMI-A-2"] = pa1000;
  CHECK(source_resolver.resolve() ==
        PhysicalAddressResolution{PhysicalAddressStatus::Available, 0x1000});
  CHECK(drm_requests == std::vector<std::string>{"card0-HDMI-A-2"});
  CHECK(load_physical_address_cache(source_cache));

  // Missing and structurally invalid sysfs EDIDs may use valid KMS evidence.
  std::filesystem::remove(source_root / "card0-HDMI-A-2" / "edid");
  CHECK(source_resolver.resolve().address == 0x1000);
  CHECK(WriteBinary(source_root / "card0-HDMI-A-2" / "edid",
                    std::vector<std::uint8_t>{1, 2, 3}));
  CHECK(source_resolver.resolve().address == 0x1000);

  // Matching valid sources are accepted; conflicting valid source evidence
  // is unresolved rather than selected arbitrarily or satisfied by cache.
  CHECK(WriteBinary(source_root / "card0-HDMI-A-2" / "edid", pa1000));
  CHECK(source_resolver.resolve().address == 0x1000);
  drm_edids["card0-HDMI-A-2"] = pa2100;
  CHECK(source_resolver.resolve().status == PhysicalAddressStatus::Unavailable);

  // Multiple cards remain correlated by the exact connector passed to the
  // DRM reader. Disconnected connectors never request a property.
  drm_edids["card0-HDMI-A-2"] = pa1000;
  CHECK(WriteConnector(source_root, "card1-HDMI-A-3", true,
                       std::vector<std::uint8_t>{}));
  drm_edids["card1-HDMI-A-3"] = pa2100;
  drm_requests.clear();
  CHECK(source_resolver.resolve().status == PhysicalAddressStatus::Ambiguous);
  CHECK(drm_requests ==
        (std::vector<std::string>{"card0-HDMI-A-2", "card1-HDMI-A-3"}));
  { std::ofstream(source_root / "card1-HDMI-A-3" / "status")
        << "disconnected\n"; }
  drm_requests.clear();
  CHECK(source_resolver.resolve().address == 0x1000);
  CHECK(drm_requests == std::vector<std::string>{"card0-HDMI-A-2"});

  // Missing property/blob/read failure remains unresolved and prevents a
  // known connector from justifying a partial Available topology.
  drm_edids["card0-HDMI-A-2"] = std::nullopt;
  std::filesystem::remove(source_root / "card0-HDMI-A-2" / "edid");
  CHECK(source_resolver.resolve().status == PhysicalAddressStatus::Unavailable);
  CHECK(WriteConnector(source_root, "card1-HDMI-A-3", true, pa2100));
  CHECK(source_resolver.resolve().status == PhysicalAddressStatus::Ambiguous);

  std::filesystem::remove_all(root);
  return EXIT_SUCCESS;
}

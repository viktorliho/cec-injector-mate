#include <iomanip>
#include <iostream>
#include <sstream>

#include "cec_injector/mate/edid/physical_address.h"
#include "cec_injector/mate/macos/mac_physical_address.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::macos_platform;

namespace {

std::string format_pa(std::uint16_t value) {
  std::ostringstream out;
  out << std::hex << std::nouppercase
      << ((value >> 12) & 0xf) << '.'
      << ((value >> 8) & 0xf) << '.'
      << ((value >> 4) & 0xf) << '.'
      << (value & 0xf);
  return out.str();
}

const char* status_name(PhysicalAddressStatus status) {
  switch (status) {
    case PhysicalAddressStatus::Available:
      return "Available";
    case PhysicalAddressStatus::Unavailable:
      return "Unavailable";
    case PhysicalAddressStatus::Ambiguous:
      return "Ambiguous";
  }
  return "Unavailable";
}

}  // namespace

int main() {
  try {
    MacPhysicalAddressResolver resolver;
    const auto candidates = resolver.enumerate_candidates();
    const auto observation = observe_physical_address(candidates);
    std::cout << "displays=" << candidates.size() << '\n';
    std::cout << "external_candidates="
              << observation.plausible_external_candidates << '\n';
    std::cout << "pa_status=" << status_name(observation.resolution.status) << '\n';
    for (const auto& candidate : candidates) {
      std::cout << "candidate registry=0x" << std::hex << candidate.registry_entry_id
                << std::dec
                << " active=" << (candidate.active ? "yes" : "no")
                << " external=" << (candidate.plausible_external ? "yes" : "no")
                << " edid_bytes="
                << (candidate.edid ? candidate.edid->size() : 0)
                << " product=" << candidate.product_name
                << " transport=" << candidate.transport_description;
      if (candidate.edid) {
        const auto parsed = edid::parse_edid_physical_address(*candidate.edid);
        std::cout << " parsed_status=";
        switch (parsed.status) {
          case edid::EdidPhysicalAddressStatus::Invalid:
            std::cout << "Invalid";
            break;
          case edid::EdidPhysicalAddressStatus::NoPhysicalAddress:
            std::cout << "NoPhysicalAddress";
            break;
          case edid::EdidPhysicalAddressStatus::PhysicalAddress:
            std::cout << "PhysicalAddress";
            break;
        }
        if (parsed.address) {
          std::cout << " pa=" << format_pa(*parsed.address)
                    << " pa_raw=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << *parsed.address << std::dec;
        }
      }
      std::cout << '\n';
    }
    if (observation.resolution.address) {
      std::cout << "pa=" << format_pa(*observation.resolution.address) << '\n';
      std::cout << "pa_raw=0x" << std::hex << std::setw(4) << std::setfill('0')
                << *observation.resolution.address << std::dec << '\n';
    }
    return observation.resolution.status == PhysicalAddressStatus::Available ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

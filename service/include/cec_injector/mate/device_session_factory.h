#pragma once

#include <cstdint>
#include <memory>

#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate {

struct DeviceSessionConfiguration {
  std::uint16_t physical_address{0xffff};

  [[nodiscard]] bool valid() const noexcept {
    if (physical_address == 0x0000 || physical_address == 0xffff) return false;
    bool zero_seen = false;
    for (int shift = 12; shift >= 0; shift -= 4) {
      const auto nibble =
          static_cast<std::uint8_t>(physical_address >> shift) & 0x0f;
      if (nibble == 0) {
        zero_seen = true;
      } else if (zero_seen) {
        return false;
      }
    }
    return true;
  }
};

// Service composition passes the complete discovery record without
// interpreting its platform-private locator. A platform/adapter-specific
// implementation resolves the locator using the inventory it owns.
class DeviceSessionFactory {
 public:
  virtual ~DeviceSessionFactory() = default;

  [[nodiscard]] virtual std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device,
      const DeviceSessionConfiguration& configuration) = 0;
};

}  // namespace cec_injector::mate

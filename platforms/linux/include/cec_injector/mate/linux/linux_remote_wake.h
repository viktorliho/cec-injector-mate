#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::linux_platform {

struct LinuxWakeDeviceMetadata {
  DeviceId id;
  std::filesystem::path stable_locator;
  std::uint16_t vendor_id{};
  std::uint16_t product_id{};
  std::string serial;
};

class LinuxRemoteWakeEnvironment {
 public:
  explicit LinuxRemoteWakeEnvironment(std::filesystem::path sys_root);

  [[nodiscard]] AuthorizationState authorization(
      const DeviceId& selected,
      const std::vector<LinuxWakeDeviceMetadata>& inventory) const;

 private:
  std::filesystem::path sys_root_;
};

}  // namespace cec_injector::mate::linux_platform

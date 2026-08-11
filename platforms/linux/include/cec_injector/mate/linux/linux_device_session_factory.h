#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

#include "cec_injector/mate/device_session_factory.h"
#include "cec_injector/mate/linux/linux_host_platform.h"

namespace cec_injector::mate::linux_platform {

class LinuxDeviceSessionFactory final : public DeviceSessionFactory {
 public:
  explicit LinuxDeviceSessionFactory(
      std::shared_ptr<LinuxDeviceInventory> inventory,
      std::chrono::milliseconds operation_timeout =
          std::chrono::milliseconds(2500),
      std::function<void(std::string_view)> diagnostics = {});

  [[nodiscard]] std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device,
      const DeviceSessionConfiguration& configuration) override;

 private:
  std::shared_ptr<LinuxDeviceInventory> inventory_;
  std::chrono::milliseconds operation_timeout_;
  std::function<void(std::string_view)> diagnostics_;
};

}  // namespace cec_injector::mate::linux_platform

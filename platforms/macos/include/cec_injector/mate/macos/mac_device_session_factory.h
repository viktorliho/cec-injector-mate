#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

#include "cec_injector/mate/device_session_factory.h"
#include "cec_injector/mate/libcec/libcec_device_session.h"
#include "cec_injector/mate/macos/mac_device_inventory.h"

namespace cec_injector::mate::macos_platform {

class MacDeviceSessionFactory final : public DeviceSessionFactory {
 public:
  using SessionBuilder = std::function<std::unique_ptr<DeviceSession>(
      DeviceId, DeviceSessionConfiguration, libcec::LibCecSessionOptions)>;

  explicit MacDeviceSessionFactory(
      std::shared_ptr<MacDeviceInventory> inventory,
      std::chrono::milliseconds operation_timeout =
          std::chrono::milliseconds(2500),
      std::function<void(std::string_view)> diagnostics = {},
      SessionBuilder session_builder = {});

  [[nodiscard]] std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device,
      const DeviceSessionConfiguration& configuration) override;

 private:
  std::shared_ptr<MacDeviceInventory> inventory_;
  std::chrono::milliseconds operation_timeout_;
  std::function<void(std::string_view)> diagnostics_;
  SessionBuilder session_builder_;
};

}  // namespace cec_injector::mate::macos_platform

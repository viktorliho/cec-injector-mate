#include "cec_injector/mate/linux/linux_device_session_factory.h"

#include <stdexcept>
#include <utility>

#include "cec_injector/mate/libcec/libcec_device_session.h"

namespace cec_injector::mate::linux_platform {

LinuxDeviceSessionFactory::LinuxDeviceSessionFactory(
    std::shared_ptr<LinuxDeviceInventory> inventory,
    std::chrono::milliseconds operation_timeout,
    std::function<void(std::string_view)> diagnostics)
    : inventory_(std::move(inventory)),
      operation_timeout_(operation_timeout),
      diagnostics_(std::move(diagnostics)) {
  if (!inventory_ || operation_timeout_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("invalid Linux session factory configuration");
  }
}

std::unique_ptr<DeviceSession> LinuxDeviceSessionFactory::create_session(
    const DiscoveredDevice& device,
    const DeviceSessionConfiguration& configuration) {
  if (!device.id.valid() || !device.locator.valid() || !configuration.valid()) {
    return {};
  }
  const auto current = inventory_->resolve(device);
  if (!current || current->explicit_port.empty()) return {};
  return std::make_unique<libcec::LibCecDeviceSession>(
      device.id, configuration,
      libcec::LibCecSessionOptions{current->explicit_port.string(),
                                   operation_timeout_, diagnostics_});
}

}  // namespace cec_injector::mate::linux_platform

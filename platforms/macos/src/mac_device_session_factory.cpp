#include "cec_injector/mate/macos/mac_device_session_factory.h"

#include <stdexcept>
#include <utility>

namespace cec_injector::mate::macos_platform {
namespace {

std::unique_ptr<DeviceSession> build_default_session(
    DeviceId id, DeviceSessionConfiguration configuration,
    libcec::LibCecSessionOptions options) {
  return std::make_unique<libcec::LibCecDeviceSession>(
      std::move(id), configuration, std::move(options));
}

}  // namespace

MacDeviceSessionFactory::MacDeviceSessionFactory(
    std::shared_ptr<MacDeviceInventory> inventory,
    std::chrono::milliseconds operation_timeout,
    std::function<void(std::string_view)> diagnostics,
    SessionBuilder session_builder)
    : inventory_(std::move(inventory)),
      operation_timeout_(operation_timeout),
      diagnostics_(std::move(diagnostics)),
      session_builder_(session_builder ? std::move(session_builder)
                                       : SessionBuilder(build_default_session)) {
  if (!inventory_ || operation_timeout_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("invalid macOS session factory configuration");
  }
}

std::unique_ptr<DeviceSession> MacDeviceSessionFactory::create_session(
    const DiscoveredDevice& device,
    const DeviceSessionConfiguration& configuration) {
  if (!device.id.valid() || !device.locator.valid() || !configuration.valid()) {
    return {};
  }
  const auto current = inventory_->resolve(device);
  if (!current || current->endpoint_kind != MacEndpointKind::Callout ||
      current->explicit_port.empty()) {
    return {};
  }
  return session_builder_(device.id, configuration,
                          libcec::LibCecSessionOptions{current->explicit_port,
                                                       operation_timeout_,
                                                       diagnostics_});
}

}  // namespace cec_injector::mate::macos_platform

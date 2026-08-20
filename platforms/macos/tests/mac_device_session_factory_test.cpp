#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "cec_injector/mate/macos/mac_device_session_factory.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::macos_platform;

namespace {

void check(bool value, const char* message) {
  if (!value) throw message;
}

MacDeviceCandidate candidate(std::string serial, std::string callout,
                             std::uint64_t registry_id) {
  MacDeviceCandidate value;
  value.callout_device = std::move(callout);
  value.dialin_device = "/dev/tty.usbmodem1101";
  value.vendor_id = kReferenceVendorId;
  value.product_id = kReferenceProductId;
  value.serial = std::move(serial);
  value.tty_device = "usbmodem1101";
  value.serial_client_type = "IOSerialStream";
  value.has_usb_device_ancestry = true;
  value.registry_entry_id = registry_id;
  return value;
}

struct FakeSession final : DeviceSession {
  FakeSession(DeviceId value_id, DeviceSessionConfiguration value_configuration,
              std::string value_port)
      : id(std::move(value_id)),
        configuration(value_configuration),
        port(std::move(value_port)) {}

  [[nodiscard]] const DeviceId& device_id() const noexcept override { return id; }
  [[nodiscard]] SessionState state() const noexcept override {
    return SessionState::Discovered;
  }
  void set_observer(DeviceSessionObserver*) override {}
  OperationResult connect() override { return OperationResult::Unsupported; }
  OperationResult disconnect() override { return OperationResult::Success; }
  OperationResult refresh_state() override { return OperationResult::Unsupported; }
  [[nodiscard]] DeviceStateSnapshot snapshot() const override { return {}; }
  OperationResult power_on_tv() override { return OperationResult::Unsupported; }
  OperationResult standby_tv() override { return OperationResult::Unsupported; }
  OperationResult make_active_source() override {
    return OperationResult::Unsupported;
  }
  [[nodiscard]] WakeState query_wake_state() override { return {}; }
  OperationResult configure_wake(std::uint16_t) override {
    return OperationResult::Unsupported;
  }
  OperationResult arm_wake() override { return OperationResult::Unsupported; }
  OperationResult disarm_wake() override { return OperationResult::Unsupported; }
  [[nodiscard]] WakeReason read_last_wake_reason() override { return {}; }
  OperationResult clear_last_wake_reason() override {
    return OperationResult::Unsupported;
  }

  DeviceId id;
  DeviceSessionConfiguration configuration;
  std::string port;
};

}  // namespace

int main() {
  try {
    auto inventory = std::make_shared<MacDeviceInventory>();
    auto first = candidate("TEST-SERIAL-A", "/dev/cu.usbmodem1101", 0x101);
    inventory->replace(make_discovery_snapshot({first}), {first});
    const auto discovered = inventory->snapshot().devices.front();

    std::string built_port;
    DeviceId built_id;
    DeviceSessionConfiguration built_configuration;
    MacDeviceSessionFactory factory(
        inventory, std::chrono::milliseconds(900), {},
        [&](DeviceId id, DeviceSessionConfiguration configuration,
            libcec::LibCecSessionOptions options) -> std::unique_ptr<DeviceSession> {
          built_port = options.explicit_port;
          built_id = id;
          built_configuration = configuration;
          return std::make_unique<FakeSession>(std::move(id), configuration,
                                               std::move(options.explicit_port));
        });

    const auto invalid_device =
        factory.create_session({}, DeviceSessionConfiguration{0x2100});
    check(!invalid_device, "invalid device rejected");

    const auto invalid_configuration =
        factory.create_session(discovered, DeviceSessionConfiguration{0xffff});
    check(!invalid_configuration, "invalid configuration rejected");

    const auto session =
        factory.create_session(discovered, DeviceSessionConfiguration{0x2100});
    check(session != nullptr, "valid session created");
    check(built_port == first.callout_device, "callout path selected");
    check(built_id == discovered.id, "session gets device id");
    check(built_configuration.physical_address == 0x2100,
          "session gets physical address");

    auto dialin_only = candidate("TEST-SERIAL-A", "", 0x404);
    dialin_only.dialin_device = "/dev/tty.usbmodem4401";
    inventory->replace(make_discovery_snapshot({dialin_only}), {dialin_only});
    check(inventory->snapshot().devices.empty(),
          "dialin-only candidate does not enter inventory");

    inventory->replace({}, {});
    check(!factory.create_session(discovered, DeviceSessionConfiguration{0x2100}),
          "stale locator rejected");

    auto renamed = candidate("TEST-SERIAL-A", "/dev/cu.usbmodem2201", 0x202);
    inventory->replace(make_discovery_snapshot({renamed}), {renamed});
    const auto refreshed = inventory->snapshot().devices.front();
    built_port.clear();
    const auto refreshed_session =
        factory.create_session(refreshed, DeviceSessionConfiguration{0x2100});
    check(refreshed_session != nullptr, "refreshed device creates session");
    check(built_port == renamed.callout_device, "refreshed callout path selected");

    std::cout << "macOS session factory tests passed\n";
    return 0;
  } catch (const char* message) {
    std::cerr << message << '\n';
    return 1;
  }
}

#include "cec_injector/mate/linux/linux_remote_wake.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "cec_injector/mate/linux/linux_device_discovery.h"
#include "test_support.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::linux_platform;

namespace {

void Write(const std::filesystem::path& path, const std::string& value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value << '\n';
}

class Fixture {
 public:
  Fixture() {
    const auto stamp = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    root = std::filesystem::temp_directory_path() /
           ("mate-remote-wake-" + std::to_string(stamp));
    sys = root / "sys";
    usb = sys / "devices" / "pci0000:00" / "usb1" / "1-1";
    tty_device = usb / "1-1:1.0" / "tty" / "ttyACM0";
    locator = root / "dev" / "serial" / "by-id" / "reference";
    SetTty("ttyACM0");
    Write(usb / "idVendor", "cafe");
    Write(usb / "idProduct", "cec1");
    Write(usb / "serial", serial);
    Write(usb / "power" / "wakeup", "enabled");
  }

  ~Fixture() { std::filesystem::remove_all(root); }

  void SetTty(const std::string& name) {
    const auto node = root / "dev" / name;
    Write(node, "");
    std::filesystem::create_directories(locator.parent_path());
    std::error_code ignored;
    std::filesystem::remove(locator, ignored);
    std::filesystem::create_symlink(node, locator);

    tty_device = usb / "1-1:1.0" / "tty" / name;
    std::filesystem::create_directories(tty_device);
    const auto class_device = sys / "class" / "tty" / name / "device";
    std::filesystem::create_directories(class_device.parent_path());
    std::filesystem::create_symlink(tty_device, class_device);
  }

  LinuxWakeDeviceMetadata Metadata() const {
    return {.id = make_linux_device_id(serial),
            .stable_locator = locator,
            .vendor_id = kReferenceVendorId,
            .product_id = kReferenceProductId,
            .serial = serial};
  }

  std::filesystem::path root;
  std::filesystem::path sys;
  std::filesystem::path usb;
  std::filesystem::path tty_device;
  std::filesystem::path locator;
  std::string serial{"SERIAL-A"};
};

}  // namespace

int main() {
  Fixture fixture;
  LinuxRemoteWakeEnvironment resolver(fixture.sys);
  auto metadata = fixture.Metadata();

  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Authorized);
  Write(fixture.usb / "power" / "wakeup", "disabled");
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Denied);

  std::filesystem::remove(fixture.usb / "power" / "wakeup");
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Unavailable);
  Write(fixture.usb / "power" / "wakeup", "enabled");

  const auto class_device =
      fixture.sys / "class" / "tty" / "ttyACM0" / "device";
  std::filesystem::remove(class_device);
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Unavailable);
  std::filesystem::create_symlink(fixture.tty_device, class_device);

  Write(fixture.usb / "serial", "OTHER-BOARD");
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Unavailable);
  Write(fixture.usb / "serial", fixture.serial);
  Write(fixture.usb / "idVendor", "1234");
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Unavailable);
  Write(fixture.usb / "idVendor", "cafe");

  auto wrong_serial = metadata;
  wrong_serial.serial = "SERIAL-B";
  wrong_serial.id = make_linux_device_id(wrong_serial.serial);
  CHECK(resolver.authorization(wrong_serial.id, {wrong_serial}) ==
        AuthorizationState::Unavailable);

  auto wrong_profile = metadata;
  wrong_profile.vendor_id = 0x1234;
  CHECK(resolver.authorization(wrong_profile.id, {wrong_profile}) ==
        AuthorizationState::Unavailable);
  wrong_profile = metadata;
  wrong_profile.product_id = 0x5678;
  CHECK(resolver.authorization(wrong_profile.id, {wrong_profile}) ==
        AuthorizationState::Unavailable);

  Write(fixture.tty_device / "idVendor", "cafe");
  Write(fixture.tty_device / "idProduct", "cec1");
  Write(fixture.tty_device / "serial", fixture.serial);
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Unavailable);
  std::filesystem::remove(fixture.tty_device / "idVendor");
  std::filesystem::remove(fixture.tty_device / "idProduct");
  std::filesystem::remove(fixture.tty_device / "serial");

  const auto wake_before = [&] {
    std::ifstream input(fixture.usb / "power" / "wakeup");
    std::string value;
    input >> value;
    return value;
  }();
  fixture.SetTty("ttyACM7");
  metadata.stable_locator = fixture.locator;
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Authorized);
  std::ifstream wake_after_file(fixture.usb / "power" / "wakeup");
  std::string wake_after;
  wake_after_file >> wake_after;
  CHECK(wake_after == wake_before);

  CHECK(resolver.authorization(metadata.id, {metadata, metadata}) ==
        AuthorizationState::Unavailable);
  std::filesystem::remove(fixture.locator);
  CHECK(resolver.authorization(metadata.id, {metadata}) ==
        AuthorizationState::Unavailable);
  return EXIT_SUCCESS;
}

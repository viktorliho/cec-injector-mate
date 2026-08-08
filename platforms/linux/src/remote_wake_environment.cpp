#include "cec_injector/mate/linux/linux_remote_wake.h"

#include <charconv>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "cec_injector/mate/linux/linux_device_discovery.h"

namespace cec_injector::mate::linux_platform {
namespace {

std::optional<std::string> ReadText(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string value;
  input >> value;
  if (!input) return std::nullopt;
  return value;
}

std::optional<std::uint16_t> ReadHex(const std::filesystem::path& path) {
  const auto text = ReadText(path);
  if (!text) return std::nullopt;
  std::uint16_t value{};
  const auto result =
      std::from_chars(text->data(), text->data() + text->size(), value, 16);
  if (result.ec != std::errc{} || result.ptr != text->data() + text->size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

LinuxRemoteWakeEnvironment::LinuxRemoteWakeEnvironment(
    std::filesystem::path sys_root)
    : sys_root_(std::move(sys_root)) {
  if (sys_root_.empty()) throw std::invalid_argument("sys root is required");
}

AuthorizationState LinuxRemoteWakeEnvironment::authorization(
    const DeviceId& selected,
    const std::vector<LinuxWakeDeviceMetadata>& inventory) const {
  const LinuxWakeDeviceMetadata* device = nullptr;
  for (const auto& candidate : inventory) {
    if (candidate.id != selected) continue;
    if (device) return AuthorizationState::Unavailable;
    device = &candidate;
  }
  if (!device || device->vendor_id != kReferenceVendorId ||
      device->product_id != kReferenceProductId || device->serial.empty() ||
      device->id != make_linux_device_id(device->serial)) {
    return AuthorizationState::Unavailable;
  }

  std::error_code error;
  const auto tty = std::filesystem::canonical(device->stable_locator, error);
  if (error) return AuthorizationState::Unavailable;
  const auto sys_root = std::filesystem::canonical(sys_root_, error);
  if (error) return AuthorizationState::Unavailable;
  auto ancestor = std::filesystem::canonical(
      sys_root / "class" / "tty" / tty.filename() / "device", error);
  if (error) return AuthorizationState::Unavailable;
  const auto relative = std::filesystem::relative(ancestor, sys_root, error);
  if (error || relative.empty() || *relative.begin() == "..") {
    return AuthorizationState::Unavailable;
  }

  std::vector<std::filesystem::path> usb_devices;
  while (ancestor != sys_root) {
    const auto vendor = ReadHex(ancestor / "idVendor");
    const auto product = ReadHex(ancestor / "idProduct");
    const auto serial = ReadText(ancestor / "serial");
    if (vendor || product || serial) {
      if (!vendor || !product || !serial) return AuthorizationState::Unavailable;
      usb_devices.push_back(ancestor);
    }
    ancestor = ancestor.parent_path();
  }
  if (usb_devices.size() != 1) return AuthorizationState::Unavailable;

  const auto& usb = usb_devices.front();
  if (ReadHex(usb / "idVendor") != device->vendor_id ||
      ReadHex(usb / "idProduct") != device->product_id ||
      ReadText(usb / "serial") != device->serial) {
    return AuthorizationState::Unavailable;
  }
  const auto wakeup = ReadText(usb / "power" / "wakeup");
  if (!wakeup) return AuthorizationState::Unavailable;
  if (*wakeup == "enabled") return AuthorizationState::Authorized;
  if (*wakeup == "disabled") return AuthorizationState::Denied;
  return AuthorizationState::Unavailable;
}

}  // namespace cec_injector::mate::linux_platform

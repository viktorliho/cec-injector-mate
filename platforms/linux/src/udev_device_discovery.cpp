#include "cec_injector/mate/linux/linux_device_discovery.h"

#include <libudev.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cec_injector::mate::linux_platform {
namespace {

struct UdevDeleter {
  void operator()(udev* value) const { if (value) udev_unref(value); }
};
struct EnumerateDeleter {
  void operator()(udev_enumerate* value) const {
    if (value) udev_enumerate_unref(value);
  }
};
struct DeviceDeleter {
  void operator()(udev_device* value) const {
    if (value) udev_device_unref(value);
  }
};
struct MonitorDeleter {
  void operator()(udev_monitor* value) const {
    if (value) udev_monitor_unref(value);
  }
};

using UdevPtr = std::unique_ptr<udev, UdevDeleter>;
using EnumeratePtr = std::unique_ptr<udev_enumerate, EnumerateDeleter>;
using DevicePtr = std::unique_ptr<udev_device, DeviceDeleter>;
using MonitorPtr = std::unique_ptr<udev_monitor, MonitorDeleter>;

std::string Property(udev_device* device, const char* name) {
  const char* value = udev_device_get_property_value(device, name);
  return value ? value : "";
}

std::uint16_t ParseHex(std::string_view value) {
  std::uint16_t parsed{};
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, 16);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size()
             ? parsed
             : 0;
}

std::string StablePath(udev_device* device) {
  constexpr std::string_view prefix = "/dev/serial/by-id/";
  const auto links = Property(device, "DEVLINKS");
  std::vector<std::string> matches;
  std::size_t begin = 0;
  while (begin < links.size()) {
    const auto end = links.find(' ', begin);
    const auto link = links.substr(
        begin, end == std::string::npos ? end : end - begin);
    if (link.starts_with(prefix)) matches.push_back(link);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  if (matches.empty()) return {};
  return *std::min_element(matches.begin(), matches.end());
}

LinuxDeviceCandidate CandidateFrom(udev_device* device) {
  const char* node = udev_device_get_devnode(device);
  auto driver = Property(device, "ID_USB_DRIVER");
  if (driver.empty()) driver = Property(device, "ID_DRIVER");
  const auto interfaces = Property(device, "ID_USB_INTERFACES");
  return {.device_node = node ? node : "",
          .stable_path = StablePath(device),
          .vendor_id = ParseHex(Property(device, "ID_VENDOR_ID")),
          .product_id = ParseHex(Property(device, "ID_MODEL_ID")),
          .serial = Property(device, "ID_SERIAL_SHORT"),
          .driver = std::move(driver),
          .interface_class = interfaces.find(":0202") != std::string::npos
                                 ? "0202"
                                 : ""};
}

UdevAction ParseAction(const char* action) {
  if (!action) return UdevAction::Other;
  const std::string_view value(action);
  if (value == "add") return UdevAction::Add;
  if (value == "remove") return UdevAction::Remove;
  if (value == "change") return UdevAction::Change;
  return UdevAction::Other;
}

}  // namespace

LinuxDeviceDiscovery::LinuxDeviceDiscovery(SnapshotCallback callback)
    : callback_(std::move(callback)) {
  if (!callback_) throw std::invalid_argument("discovery callback is required");
}

LinuxDeviceDiscovery::~LinuxDeviceDiscovery() { stop(); }

std::vector<LinuxDeviceCandidate>
LinuxDeviceDiscovery::enumerate_candidates() const {
  UdevPtr context(udev_new());
  if (!context) throw std::runtime_error("udev_new failed");
  EnumeratePtr enumeration(udev_enumerate_new(context.get()));
  if (!enumeration ||
      udev_enumerate_add_match_subsystem(enumeration.get(), "tty") < 0 ||
      udev_enumerate_scan_devices(enumeration.get()) < 0) {
    throw std::runtime_error("udev tty enumeration failed");
  }

  std::vector<LinuxDeviceCandidate> candidates;
  udev_list_entry* devices = udev_enumerate_get_list_entry(enumeration.get());
  udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry, devices) {
    DevicePtr device(udev_device_new_from_syspath(
        context.get(), udev_list_entry_get_name(entry)));
    if (device) candidates.push_back(CandidateFrom(device.get()));
  }
  return candidates;
}

LinuxDiscoverySnapshot LinuxDeviceDiscovery::enumerate() const {
  return make_discovery_snapshot(enumerate_candidates());
}

void LinuxDeviceDiscovery::start() {
  if (thread_.joinable()) return;
  wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (wake_fd_ < 0) throw std::runtime_error("eventfd failed");
  stopping_ = false;
  thread_ = std::thread(&LinuxDeviceDiscovery::run, this);
}

void LinuxDeviceDiscovery::stop() {
  stopping_ = true;
  if (wake_fd_ >= 0) {
    const std::uint64_t wake = 1;
    (void)write(wake_fd_, &wake, sizeof(wake));
  }
  if (thread_.joinable()) thread_.join();
  if (wake_fd_ >= 0) close(wake_fd_);
  wake_fd_ = -1;
}

void LinuxDeviceDiscovery::run() {
  UdevPtr context(udev_new());
  if (!context) return;
  MonitorPtr monitor(udev_monitor_new_from_netlink(context.get(), "udev"));
  if (!monitor ||
      udev_monitor_filter_add_match_subsystem_devtype(
          monitor.get(), "tty", nullptr) < 0 ||
      udev_monitor_enable_receiving(monitor.get()) < 0) {
    return;
  }

  try {
    callback_(enumerate());
  } catch (...) {
    return;
  }

  std::array<pollfd, 2> descriptors{{
      {udev_monitor_get_fd(monitor.get()), POLLIN, 0},
      {wake_fd_, POLLIN, 0}}};
  while (!stopping_) {
    const int ready = poll(descriptors.data(), descriptors.size(), -1);
    if (ready < 0) {
      if (errno == EINTR) continue;
      return;
    }
    if ((descriptors[1].revents & POLLIN) != 0) return;
    if ((descriptors[0].revents & POLLIN) == 0) continue;
    DevicePtr changed(udev_monitor_receive_device(monitor.get()));
    if (!changed) continue;
    const char* subsystem = udev_device_get_subsystem(changed.get());
    const UdevEvent event{ParseAction(udev_device_get_action(changed.get())),
                          subsystem ? subsystem : ""};
    if (!should_reenumerate(event)) continue;
    try {
      callback_(enumerate());
    } catch (...) {
      return;
    }
  }
}

}  // namespace cec_injector::mate::linux_platform

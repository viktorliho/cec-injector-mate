#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate::linux_platform {

// Current development/reference hardware matcher. These values are not a
// project-wide Device or protocol identity.
inline constexpr std::uint16_t kReferenceVendorId = 0xcafe;
inline constexpr std::uint16_t kReferenceProductId = 0xcec1;

struct LinuxDeviceCandidate {
  std::string device_node;
  std::string stable_path;
  std::uint16_t vendor_id{};
  std::uint16_t product_id{};
  std::string serial;
  std::string driver;
  std::string interface_class;
};

enum class DiscoverySnapshotStatus { Complete, DuplicateIdentity };

struct LinuxDiscoverySnapshot {
  DiscoverySnapshotStatus status{DiscoverySnapshotStatus::Complete};
  std::vector<DiscoveredDevice> devices;
  std::vector<DeviceId> duplicate_ids;
};

enum class UdevAction { Add, Remove, Change, Other };

struct UdevEvent {
  UdevAction action{UdevAction::Other};
  std::string subsystem;
};

[[nodiscard]] bool is_reference_device_candidate(
    const LinuxDeviceCandidate& candidate);
[[nodiscard]] DeviceId make_linux_device_id(std::string_view serial);
[[nodiscard]] LinuxDiscoverySnapshot make_discovery_snapshot(
    const std::vector<LinuxDeviceCandidate>& candidates);
[[nodiscard]] bool should_reenumerate(const UdevEvent& event);

// Linux-private production seam. Every relevant udev event publishes a fresh
// complete snapshot; it never performs Core selection or session creation.
class LinuxDeviceDiscovery {
 public:
  using SnapshotCallback = std::function<void(LinuxDiscoverySnapshot)>;

  explicit LinuxDeviceDiscovery(SnapshotCallback callback);
  ~LinuxDeviceDiscovery();
  LinuxDeviceDiscovery(const LinuxDeviceDiscovery&) = delete;
  LinuxDeviceDiscovery& operator=(const LinuxDeviceDiscovery&) = delete;

  void start();
  void stop();
  [[nodiscard]] std::vector<LinuxDeviceCandidate> enumerate_candidates() const;
  [[nodiscard]] LinuxDiscoverySnapshot enumerate() const;

 private:
  void run();

  SnapshotCallback callback_;
  std::atomic<bool> stopping_{false};
  int wake_fd_{-1};
  std::thread thread_;
};

}  // namespace cec_injector::mate::linux_platform

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "cec_injector/mate/host_platform.h"
#include "cec_injector/mate/linux/linux_device_discovery.h"
#include "cec_injector/mate/linux/linux_remote_input.h"
#include "cec_injector/mate/linux/linux_remote_wake.h"

namespace cec_injector::mate::linux_platform {

enum class LinuxHostPlatformProfile { Daemon, Boot };

struct LinuxInventoryEntry {
  DiscoveredDevice discovered;
  std::filesystem::path explicit_port;
  LinuxWakeDeviceMetadata wake_metadata;
};

class LinuxDeviceInventory {
 public:
  LinuxDeviceInventory();
  void replace(LinuxDiscoverySnapshot snapshot,
               const std::vector<LinuxDeviceCandidate>& candidates);
  [[nodiscard]] LinuxDiscoverySnapshot snapshot() const;
  [[nodiscard]] std::optional<LinuxInventoryEntry> resolve(
      const DiscoveredDevice& device) const;
  [[nodiscard]] std::vector<LinuxWakeDeviceMetadata> wake_metadata() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct LinuxHostPlatformConfiguration {
  std::filesystem::path drm_root{"/sys/class/drm"};
  std::filesystem::path physical_address_cache;
  std::filesystem::path sys_root{"/sys"};
  std::filesystem::path uinput_endpoint{"/dev/uinput"};
  LinuxRemoteInputProfile input_profile;
  LinuxHostPlatformProfile profile{LinuxHostPlatformProfile::Daemon};
};

class LinuxHostPlatform final : public HostPlatform {
 public:
  explicit LinuxHostPlatform(LinuxHostPlatformConfiguration configuration);
  ~LinuxHostPlatform() override;

  LinuxHostPlatform(const LinuxHostPlatform&) = delete;
  LinuxHostPlatform& operator=(const LinuxHostPlatform&) = delete;

  void set_lifecycle_observer(HostLifecycleObserver* observer) override;
  [[nodiscard]] std::vector<DiscoveredDevice> discovered_devices() override;
  [[nodiscard]] PhysicalAddressResolution resolve_physical_address() override;
  OperationResult request_system_suspend() override;
  [[nodiscard]] HostPowerState host_power_state() const noexcept override;
  [[nodiscard]] AuthorizationState suspend_authorization() const override;
  [[nodiscard]] AuthorizationState remote_wake_authorization(
      const DeviceId& device) const override;
  OperationResult emit_remote_action(SemanticRemoteAction action,
                                     RemoteActionState state) override;
  OperationResult reset_remote_input() override;

  [[nodiscard]] std::shared_ptr<LinuxDeviceInventory> inventory() const {
    return inventory_;
  }

 private:
  struct Impl;
  std::shared_ptr<LinuxDeviceInventory> inventory_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cec_injector::mate::linux_platform

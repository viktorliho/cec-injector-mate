#include "cec_injector/mate/linux/linux_host_platform.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "cec_injector/mate/linux/linux_physical_address.h"
#include "cec_injector/mate/linux/linux_power_session.h"

namespace cec_injector::mate::linux_platform {

struct LinuxDeviceInventory::Impl {
  mutable std::mutex mutex;
  LinuxDiscoverySnapshot snapshot;
  std::vector<LinuxInventoryEntry> entries;
};

LinuxDeviceInventory::LinuxDeviceInventory() : impl_(std::make_shared<Impl>()) {}

void LinuxDeviceInventory::replace(
    LinuxDiscoverySnapshot snapshot,
    const std::vector<LinuxDeviceCandidate>& candidates) {
  std::vector<LinuxInventoryEntry> entries;
  if (snapshot.status == DiscoverySnapshotStatus::Complete) {
    for (const auto& discovered : snapshot.devices) {
      for (const auto& candidate : candidates) {
        if (!is_reference_device_candidate(candidate) ||
            make_linux_device_id(candidate.serial) != discovered.id ||
            PlatformDeviceLocator(candidate.stable_path) != discovered.locator) {
          continue;
        }
        entries.push_back({discovered, candidate.stable_path,
                           {discovered.id, candidate.stable_path,
                            candidate.vendor_id, candidate.product_id,
                            candidate.serial}});
        break;
      }
    }
  }
  std::lock_guard lock(impl_->mutex);
  impl_->snapshot = std::move(snapshot);
  impl_->entries = std::move(entries);
}

LinuxDiscoverySnapshot LinuxDeviceInventory::snapshot() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->snapshot;
}

std::optional<LinuxInventoryEntry> LinuxDeviceInventory::resolve(
    const DiscoveredDevice& device) const {
  std::lock_guard lock(impl_->mutex);
  const LinuxInventoryEntry* match = nullptr;
  for (const auto& entry : impl_->entries) {
    if (entry.discovered.id != device.id ||
        entry.discovered.locator != device.locator) {
      continue;
    }
    if (match) return std::nullopt;
    match = &entry;
  }
  return match ? std::optional<LinuxInventoryEntry>(*match) : std::nullopt;
}

std::vector<LinuxWakeDeviceMetadata> LinuxDeviceInventory::wake_metadata()
    const {
  std::lock_guard lock(impl_->mutex);
  std::vector<LinuxWakeDeviceMetadata> result;
  result.reserve(impl_->entries.size());
  for (const auto& entry : impl_->entries) result.push_back(entry.wake_metadata);
  return result;
}

struct LinuxHostPlatform::Impl {
  Impl(LinuxHostPlatformConfiguration configuration,
       std::shared_ptr<LinuxDeviceInventory> inventory)
      : inventory(std::move(inventory)),
        discovery([this](LinuxDiscoverySnapshot) { refresh_inventory(); }),
        physical_address(std::move(configuration.drm_root),
                         std::move(configuration.physical_address_cache),
                         create_libdrm_edid_reader()),
        profile(configuration.profile),
        remote_wake(std::move(configuration.sys_root)) {}

  void start_daemon_components(LinuxHostPlatformConfiguration configuration) {
    if (profile != LinuxHostPlatformProfile::Daemon) return;
    power = create_linux_power_session_runtime();
    remote_input = std::make_unique<LinuxRemoteInputController>(
        create_linux_uinput_sink(std::move(configuration.uinput_endpoint)),
        std::move(configuration.input_profile));
  }

  void refresh_inventory() noexcept {
    try {
      auto candidates = discovery.enumerate_candidates();
      inventory->replace(make_discovery_snapshot(candidates), candidates);
    } catch (...) {
      inventory->replace({}, {});
    }
  }

  std::shared_ptr<LinuxDeviceInventory> inventory;
  LinuxDeviceDiscovery discovery;
  LinuxPhysicalAddressResolver physical_address;
  LinuxHostPlatformProfile profile;
  std::unique_ptr<LinuxPowerSessionRuntime> power;
  std::unique_ptr<LinuxRemoteInputController> remote_input;
  LinuxRemoteWakeEnvironment remote_wake;
  std::mutex input_mutex;
  mutable std::mutex observer_mutex;
  HostLifecycleObserver* observer{};
  std::atomic<bool> stopping{};
  std::thread input_timer;
};

LinuxHostPlatform::LinuxHostPlatform(LinuxHostPlatformConfiguration configuration)
    : inventory_(std::make_shared<LinuxDeviceInventory>()),
      impl_(std::make_unique<Impl>(configuration, inventory_)) {
  impl_->refresh_inventory();
  impl_->start_daemon_components(std::move(configuration));
  if (impl_->profile == LinuxHostPlatformProfile::Boot) {
    impl_->discovery.start();
    return;
  }
  impl_->power->set_notification_sink([this](HostLifecycleNotification value) {
    std::lock_guard lock(impl_->observer_mutex);
    if (impl_->observer) {
      impl_->observer->on_host_lifecycle_notification(std::move(value));
    }
  });
  if (impl_->power->start() != OperationResult::Success) {
    throw std::runtime_error("Linux power/session runtime failed to start");
  }
  {
    std::lock_guard lock(impl_->input_mutex);
    if (impl_->remote_input->initialize() != OperationResult::Success) {
      impl_->power->stop();
      throw std::runtime_error("Linux remote-input backend failed to initialize");
    }
  }
  impl_->discovery.start();
  impl_->input_timer = std::thread([this] {
    using namespace std::chrono_literals;
    while (!impl_->stopping.load()) {
      std::this_thread::sleep_for(50ms);
      std::lock_guard lock(impl_->input_mutex);
      (void)impl_->remote_input->tick();
    }
  });
}

LinuxHostPlatform::~LinuxHostPlatform() {
  {
    std::lock_guard lock(impl_->observer_mutex);
    impl_->observer = nullptr;
  }
  impl_->stopping = true;
  if (impl_->input_timer.joinable()) impl_->input_timer.join();
  impl_->discovery.stop();
  if (impl_->power) {
    impl_->power->set_notification_sink({});
    impl_->power->stop();
  }
  if (impl_->remote_input) {
    std::lock_guard lock(impl_->input_mutex);
    (void)impl_->remote_input->reset_remote_input();
  }
}

void LinuxHostPlatform::set_lifecycle_observer(HostLifecycleObserver* observer) {
  std::lock_guard lock(impl_->observer_mutex);
  impl_->observer = observer;
}

std::vector<DiscoveredDevice> LinuxHostPlatform::discovered_devices() {
  const auto snapshot = inventory_->snapshot();
  if (snapshot.status != DiscoverySnapshotStatus::Complete) return {};
  return snapshot.devices;
}

PhysicalAddressResolution LinuxHostPlatform::resolve_physical_address() {
  return impl_->physical_address.resolve();
}

DisplaySummary LinuxHostPlatform::display_summary() {
  return impl_->physical_address.display_summary();
}

OperationResult LinuxHostPlatform::request_system_suspend() {
  return impl_->power ? impl_->power->request_system_suspend()
                      : OperationResult::Unsupported;
}

HostPowerState LinuxHostPlatform::host_power_state() const noexcept {
  return impl_->power ? impl_->power->host_power_state()
                      : HostPowerState::Running;
}

AuthorizationState LinuxHostPlatform::suspend_authorization() const {
  return impl_->power ? impl_->power->suspend_authorization()
                      : AuthorizationState::Unavailable;
}

AuthorizationState LinuxHostPlatform::remote_wake_authorization(
    const DeviceId& device) const {
  return impl_->remote_wake.authorization(device, inventory_->wake_metadata());
}

OperationResult LinuxHostPlatform::emit_remote_action(
    SemanticRemoteAction action, RemoteActionState state) {
  std::lock_guard lock(impl_->input_mutex);
  return impl_->remote_input
             ? impl_->remote_input->emit_remote_action(action, state)
             : OperationResult::Unsupported;
}

OperationResult LinuxHostPlatform::reset_remote_input() {
  std::lock_guard lock(impl_->input_mutex);
  return impl_->remote_input ? impl_->remote_input->reset_remote_input()
                             : OperationResult::Success;
}

OperationResult LinuxHostPlatform::apply_remote_input_configuration(
    const RemoteInputConfiguration& configuration) {
  std::lock_guard lock(impl_->input_mutex);
  if (!impl_->remote_input) return OperationResult::Unsupported;
  if (configuration.profile != RemoteInputProfile::SteamOsParity) {
    return OperationResult::InvalidConfiguration;
  }
  return impl_->remote_input->reconfigure(
      configuration.enabled, steamos_parity_remote_input_profile());
}

}  // namespace cec_injector::mate::linux_platform

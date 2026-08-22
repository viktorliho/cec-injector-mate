#include "cec_injector/mate/gui/macos/mac_in_process_mate_frontend_backend.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "cec_injector/mate/frontend/frontend_state_projector.h"
#include "cec_injector/mate/macos/mac_device_discovery.h"
#include "cec_injector/mate/macos/mac_device_inventory.h"
#include "cec_injector/mate/macos/mac_device_session_factory.h"
#include "cec_injector/mate/macos/mac_physical_address.h"
#include "cec_injector/mate/runtime_configuration.h"

namespace cec_injector::mate::gui::macos {
namespace {

using macos_platform::MacDeviceCandidate;
using macos_platform::MacDeviceDiscovery;
using macos_platform::MacDeviceInventory;
using macos_platform::MacDeviceSessionFactory;
using macos_platform::MacDisplayCandidate;
using macos_platform::MacPhysicalAddressObservation;
using macos_platform::MacPhysicalAddressResolver;

frontend::FrontendCapabilities default_capabilities() {
  return {.configuration = false,
          .remote_input = false,
          .remote_wake = false,
          .tv_power_actions = true,
          .cec_monitor = true};
}

frontend::FrontendConfigurationSnapshot default_configuration() {
  return {1, production_runtime_defaults()};
}

MateManualAction manual_action(frontend::FrontendAction action) {
  switch (action) {
    case frontend::FrontendAction::PowerOnTv:
      return MateManualAction::PowerOnTv;
    case frontend::FrontendAction::MakeActiveSource:
      return MateManualAction::MakeActiveSource;
    case frontend::FrontendAction::StandbyTv:
      return MateManualAction::StandbyTv;
  }
  return MateManualAction::PowerOnTv;
}

DisplaySummary summarize_display(
    const std::vector<MacDisplayCandidate>& candidates,
    const MacPhysicalAddressObservation& observation) {
  DisplaySummary summary;
  if (observation.plausible_external_candidates > 1) {
    summary.name = "Multiple external displays";
  } else {
    const MacDisplayCandidate* chosen = nullptr;
    for (const auto& candidate : candidates) {
      if (!candidate.plausible_external) continue;
      chosen = &candidate;
      break;
    }
    if (!chosen) {
      for (const auto& candidate : candidates) {
        if (!candidate.product_name.empty() || !candidate.transport_description.empty()) {
          chosen = &candidate;
          break;
        }
      }
    }
    if (chosen) {
      summary.name = !chosen->product_name.empty() ? chosen->product_name
                                                   : chosen->transport_description;
      summary.edid_available = chosen->edid.has_value();
      summary.edid_byte_count =
          chosen->edid ? chosen->edid->size() : std::size_t{};
    }
  }
  if (!summary.edid_available) {
    summary.edid_available = observation.candidates_with_edid > 0;
    if (summary.edid_byte_count == 0) {
      for (const auto& candidate : candidates) {
        if (candidate.edid) {
          summary.edid_byte_count = candidate.edid->size();
          break;
        }
      }
    }
  }
  return summary;
}

class MacGuiHostPlatform final : public HostPlatform {
 public:
  explicit MacGuiHostPlatform(std::shared_ptr<MacDeviceInventory> inventory)
      : inventory_(std::move(inventory)) {
    if (!inventory_) {
      throw std::invalid_argument("macOS GUI host platform requires inventory");
    }
  }

  void set_lifecycle_observer(HostLifecycleObserver* observer) override {
    std::lock_guard lock(mutex_);
    observer_ = observer;
  }

  std::vector<DiscoveredDevice> discovered_devices() override {
    auto candidates = discovery_.enumerate_candidates();
    auto snapshot = macos_platform::make_discovery_snapshot(candidates);
    inventory_->replace(snapshot, candidates);
    std::lock_guard lock(mutex_);
    last_devices_ = snapshot.devices;
    return snapshot.devices;
  }

  PhysicalAddressResolution resolve_physical_address() override {
    auto candidates = physical_address_.enumerate_candidates();
    auto observation = macos_platform::observe_physical_address(candidates);
    auto summary = summarize_display(candidates, observation);
    std::lock_guard lock(mutex_);
    last_resolution_ = observation.resolution;
    last_display_ = std::move(summary);
    return observation.resolution;
  }

  DisplaySummary display_summary() override {
    auto candidates = physical_address_.enumerate_candidates();
    auto observation = macos_platform::observe_physical_address(candidates);
    auto summary = summarize_display(candidates, observation);
    std::lock_guard lock(mutex_);
    last_resolution_ = observation.resolution;
    last_display_ = summary;
    return summary;
  }

  OperationResult request_system_suspend() override {
    return OperationResult::Unsupported;
  }

  HostPowerState host_power_state() const noexcept override {
    return HostPowerState::Running;
  }

  AuthorizationState suspend_authorization() const override {
    return AuthorizationState::Unavailable;
  }

  AuthorizationState remote_wake_authorization(const DeviceId&) const override {
    return AuthorizationState::Unavailable;
  }

  OperationResult emit_remote_action(SemanticRemoteAction,
                                     RemoteActionState) override {
    return OperationResult::Unsupported;
  }

  OperationResult reset_remote_input() override {
    return OperationResult::Success;
  }

  OperationResult apply_remote_input_configuration(
      const RemoteInputConfiguration&) override {
    return OperationResult::Unsupported;
  }

 private:
  std::shared_ptr<MacDeviceInventory> inventory_;
  MacDeviceDiscovery discovery_;
  MacPhysicalAddressResolver physical_address_;
  mutable std::mutex mutex_;
  HostLifecycleObserver* observer_{};
  std::vector<DiscoveredDevice> last_devices_;
  PhysicalAddressResolution last_resolution_;
  DisplaySummary last_display_;
};

class DisplayRefreshSubscription final : public MacFrontendRefreshSubscription {
 public:
  explicit DisplayRefreshSubscription(std::function<void()> callback)
      : callback_(std::move(callback)) {
    CGDisplayRegisterReconfigurationCallback(&DisplayRefreshSubscription::handle,
                                             this);
  }

  ~DisplayRefreshSubscription() override {
    CGDisplayRemoveReconfigurationCallback(&DisplayRefreshSubscription::handle,
                                           this);
  }

 private:
  static void handle(CGDirectDisplayID, CGDisplayChangeSummaryFlags, void* info) {
    auto* self = static_cast<DisplayRefreshSubscription*>(info);
    if (self && self->callback_) self->callback_();
  }

  std::function<void()> callback_;
};

class SerialRefreshSubscription final : public MacFrontendRefreshSubscription {
 public:
  explicit SerialRefreshSubscription(std::function<void()> callback)
      : callback_(std::move(callback)),
        notification_port_(IONotificationPortCreate(kIOMainPortDefault)) {
    if (!notification_port_) {
      throw std::runtime_error("failed to create IOKit notification port");
    }
    auto* source = IONotificationPortGetRunLoopSource(notification_port_);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);

    register_notification(kIOMatchedNotification, &matched_iterator_);
    register_notification(kIOTerminatedNotification, &terminated_iterator_);
  }

  ~SerialRefreshSubscription() override {
    if (matched_iterator_ != IO_OBJECT_NULL) IOObjectRelease(matched_iterator_);
    if (terminated_iterator_ != IO_OBJECT_NULL) {
      IOObjectRelease(terminated_iterator_);
    }
    if (notification_port_) {
      if (auto* source = IONotificationPortGetRunLoopSource(notification_port_)) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
      }
      IONotificationPortDestroy(notification_port_);
    }
  }

 private:
  static void handle(void* context, io_iterator_t iterator) {
    auto* self = static_cast<SerialRefreshSubscription*>(context);
    while (const auto object = IOIteratorNext(iterator)) {
      IOObjectRelease(object);
    }
    if (self && self->callback_) self->callback_();
  }

  void register_notification(const char* notification, io_iterator_t* iterator) {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOSerialBSDServiceValue);
    if (!matching) {
      throw std::runtime_error("failed to build IOSerialBSD matching dictionary");
    }
    CFDictionarySetValue(matching, CFSTR(kIOSerialBSDTypeKey),
                         CFSTR(kIOSerialBSDAllTypes));
    const auto result = IOServiceAddMatchingNotification(
        notification_port_, notification, matching, &SerialRefreshSubscription::handle,
        this, iterator);
    if (result != KERN_SUCCESS) {
      throw std::runtime_error("failed to register IOSerialBSD notification");
    }
    handle(this, *iterator);
  }

  std::function<void()> callback_;
  IONotificationPortRef notification_port_{};
  io_iterator_t matched_iterator_{IO_OBJECT_NULL};
  io_iterator_t terminated_iterator_{IO_OBJECT_NULL};
};

std::vector<std::unique_ptr<MacFrontendRefreshSubscription>>
install_default_refresh_sources(std::function<void()> callback) {
  std::vector<std::unique_ptr<MacFrontendRefreshSubscription>> subscriptions;
  subscriptions.push_back(
      std::make_unique<SerialRefreshSubscription>(callback));
  subscriptions.push_back(
      std::make_unique<DisplayRefreshSubscription>(std::move(callback)));
  return subscriptions;
}

MacInProcessMateFrontendBackendDependencies make_default_dependencies() {
  auto inventory = std::make_shared<MacDeviceInventory>();
  MacInProcessMateFrontendBackendDependencies dependencies;
  dependencies.host = std::make_unique<MacGuiHostPlatform>(inventory);
  dependencies.session_factory =
      std::make_unique<MacDeviceSessionFactory>(std::move(inventory));
  dependencies.install_refresh_sources = install_default_refresh_sources;
  dependencies.capabilities = default_capabilities();
  dependencies.configuration = default_configuration();
  return dependencies;
}

}  // namespace

MacInProcessMateFrontendBackend::MacInProcessMateFrontendBackend()
    : MacInProcessMateFrontendBackend(make_default_dependencies()) {}

MacInProcessMateFrontendBackend::MacInProcessMateFrontendBackend(
    MacInProcessMateFrontendBackendDependencies dependencies)
    : host_(std::move(dependencies.host)),
      session_factory_(std::move(dependencies.session_factory)),
      capabilities_(dependencies.capabilities),
      configuration_(dependencies.configuration) {
  if (!host_ || !session_factory_) {
    throw std::invalid_argument(
        "macOS in-process frontend backend requires host and session factory");
  }
  service_ = std::make_unique<MateService>(*host_, *session_factory_,
                                           std::move(dependencies.service_configuration));
  const auto start_result = service_->start();
  if (start_result == OperationResult::Success) {
    service_->set_frontend_observer(this);
    status_ = frontend::FrontendStateProjector::status(service_->frontend_snapshot());
    if (dependencies.install_refresh_sources) {
      refresh_sources_ = dependencies.install_refresh_sources(
          [this] { request_refresh(); });
    }
  } else {
    status_ = {};
    status_.service_available = false;
  }
}

MacInProcessMateFrontendBackend::~MacInProcessMateFrontendBackend() {
  refresh_sources_.clear();
  if (service_) {
    service_->set_frontend_observer(nullptr);
    service_->shutdown();
  }
  std::lock_guard lock(mutex_);
  observer_ = nullptr;
}

void MacInProcessMateFrontendBackend::set_observer(
    frontend::MateFrontendObserver* observer) {
  frontend::FrontendStatus status;
  frontend::FrontendConfigurationSnapshot configuration;
  {
    std::lock_guard lock(mutex_);
    observer_ = observer;
    status = status_;
    configuration = configuration_;
  }
  if (!observer) return;
  observer->on_frontend_status_changed(status);
  observer->on_frontend_configuration_changed(configuration);
}

frontend::FrontendStatus MacInProcessMateFrontendBackend::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

frontend::FrontendCapabilities MacInProcessMateFrontendBackend::capabilities()
    const {
  std::lock_guard lock(mutex_);
  return capabilities_;
}

frontend::FrontendConfigurationSnapshot
MacInProcessMateFrontendBackend::configuration() const {
  std::lock_guard lock(mutex_);
  return configuration_;
}

frontend::FrontendConfigurationRequestId
MacInProcessMateFrontendBackend::request_configuration(
    std::uint64_t, const frontend::FrontendConfiguration&) {
  return 0;
}

frontend::FrontendActionRequestId
MacInProcessMateFrontendBackend::request_action(frontend::FrontendAction action) {
  if (!service_) return 0;
  const auto snapshot = status();
  const auto flags = capabilities();
  if (!flags.tv_power_actions || !snapshot.service_available ||
      snapshot.session_state != SessionState::Connected) {
    return 0;
  }
  return service_->request_manual_action(manual_action(action));
}

void MacInProcessMateFrontendBackend::on_service_snapshot(
    const MateServiceSnapshot& snapshot) {
  frontend::MateFrontendObserver* observer = nullptr;
  frontend::FrontendStatus projected =
      frontend::FrontendStateProjector::status(snapshot);
  {
    std::lock_guard lock(mutex_);
    status_ = projected;
    observer = observer_;
  }
  if (observer) observer->on_frontend_status_changed(projected);
}

void MacInProcessMateFrontendBackend::on_service_cec_event(
    const MateServiceCecEvent& event) {
  frontend::MateFrontendObserver* observer = nullptr;
  const auto projected = frontend::FrontendStateProjector::cec_event(event);
  {
    std::lock_guard lock(mutex_);
    observer = observer_;
  }
  if (observer) observer->on_frontend_cec_event(projected);
}

void MacInProcessMateFrontendBackend::on_manual_action_completed(
    MateManualActionRequestId request, const MateManualActionResult& result) {
  frontend::MateFrontendObserver* observer = nullptr;
  const auto projected = frontend::FrontendStateProjector::action_result(result);
  {
    std::lock_guard lock(mutex_);
    observer = observer_;
  }
  if (observer) observer->on_frontend_action_completed(request, projected);
}

void MacInProcessMateFrontendBackend::request_refresh() {
  if (service_) service_->refresh_discovery();
}

}  // namespace cec_injector::mate::gui::macos

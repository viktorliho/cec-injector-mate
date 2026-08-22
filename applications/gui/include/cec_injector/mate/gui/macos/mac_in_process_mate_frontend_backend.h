#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "cec_injector/mate/frontend/frontend_backend.h"
#include "cec_injector/mate/mate_service.h"

namespace cec_injector::mate {
class DeviceSessionFactory;
class HostPlatform;
}

namespace cec_injector::mate::gui::macos {

class MacFrontendRefreshSubscription {
 public:
  virtual ~MacFrontendRefreshSubscription() = default;
};

struct MacInProcessMateFrontendBackendDependencies {
  std::unique_ptr<HostPlatform> host;
  std::unique_ptr<DeviceSessionFactory> session_factory;
  std::function<std::vector<std::unique_ptr<MacFrontendRefreshSubscription>>(
      std::function<void()>)>
      install_refresh_sources;
  frontend::FrontendCapabilities capabilities;
  frontend::FrontendConfigurationSnapshot configuration;
  MateServiceConfiguration service_configuration;
};

class MacInProcessMateFrontendBackend final
    : public frontend::MateFrontendBackend,
      public MateServiceFrontendObserver {
 public:
  MacInProcessMateFrontendBackend();
  explicit MacInProcessMateFrontendBackend(
      MacInProcessMateFrontendBackendDependencies dependencies);
  ~MacInProcessMateFrontendBackend() override;

  MacInProcessMateFrontendBackend(const MacInProcessMateFrontendBackend&) =
      delete;
  MacInProcessMateFrontendBackend& operator=(
      const MacInProcessMateFrontendBackend&) = delete;

  void set_observer(frontend::MateFrontendObserver* observer) override;
  [[nodiscard]] frontend::FrontendStatus status() const override;
  [[nodiscard]] frontend::FrontendCapabilities capabilities() const override;
  [[nodiscard]] frontend::FrontendConfigurationSnapshot configuration()
      const override;
  [[nodiscard]] frontend::FrontendConfigurationRequestId request_configuration(
      std::uint64_t base_revision,
      const frontend::FrontendConfiguration& configuration) override;
  [[nodiscard]] frontend::FrontendActionRequestId request_action(
      frontend::FrontendAction action) override;

  void on_service_snapshot(const MateServiceSnapshot& snapshot) override;
  void on_service_cec_event(const MateServiceCecEvent& event) override;
  void on_manual_action_completed(
      MateManualActionRequestId request,
      const MateManualActionResult& result) override;

 private:
  void request_refresh();

  std::unique_ptr<HostPlatform> host_;
  std::unique_ptr<DeviceSessionFactory> session_factory_;
  std::unique_ptr<MateService> service_;
  std::vector<std::unique_ptr<MacFrontendRefreshSubscription>>
      refresh_sources_;

  mutable std::mutex mutex_;
  frontend::MateFrontendObserver* observer_{};
  frontend::FrontendStatus status_;
  frontend::FrontendCapabilities capabilities_;
  frontend::FrontendConfigurationSnapshot configuration_;
};

}  // namespace cec_injector::mate::gui::macos

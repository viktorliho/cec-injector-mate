#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "cec_injector/mate/device_registry.h"
#include "cec_injector/mate/device_session_factory.h"
#include "cec_injector/mate/host_platform.h"
#include "cec_injector/mate/policy.h"

namespace cec_injector::mate {

struct MateServiceConfiguration {
  PolicyConfiguration policy;
  std::optional<DeviceId> selected_device;
};

class MateService final : public HostLifecycleObserver {
 public:
  using DiagnosticSink = std::function<void(std::string_view)>;

  MateService(HostPlatform& host, DeviceSessionFactory& session_factory,
              MateServiceConfiguration configuration = {},
              DiagnosticSink diagnostics = {});
  ~MateService() override;

  MateService(const MateService&) = delete;
  MateService& operator=(const MateService&) = delete;

  OperationResult start();
  void shutdown() noexcept;
  void refresh_discovery();
  void trigger_boot();
  void wait_idle();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] bool has_active_session() const noexcept;
  [[nodiscard]] std::optional<DeviceId> active_device() const;
  [[nodiscard]] std::uint64_t session_generation() const noexcept;

  void on_host_lifecycle_notification(
      HostLifecycleNotification notification) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cec_injector::mate

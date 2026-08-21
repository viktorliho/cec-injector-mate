#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <string>

#include "cec_injector/mate/frontend/frontend_state_projector.h"

struct sd_bus;
struct sd_bus_slot;

namespace cec_injector::mate::frontend_linux {

inline constexpr char kMateBusName[] = "org.cecinjector.Mate";
inline constexpr char kMateObjectPath[] = "/org/cecinjector/Mate";
inline constexpr char kMateInterface[] = "org.cecinjector.Mate1";

class LinuxDbusFrontendService final : public MateServiceFrontendObserver {
 public:
  LinuxDbusFrontendService(MateService& service,
                           RuntimeConfigurationController& configuration,
                           bool test_allow_mutation = false,
                           std::string bus_address = {});
  ~LinuxDbusFrontendService() override;
  LinuxDbusFrontendService(const LinuxDbusFrontendService&) = delete;
  LinuxDbusFrontendService& operator=(const LinuxDbusFrontendService&) = delete;

  OperationResult start();
  void stop() noexcept;

  void on_service_snapshot(const MateServiceSnapshot& snapshot) override;
  void on_service_cec_event(const MateServiceCecEvent& event) override;
  void on_manual_action_completed(MateManualActionRequestId request,
                                  const MateManualActionResult& result) override;
  void configuration_changed(const VersionedRuntimeConfiguration& value);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cec_injector::mate::frontend_linux

#pragma once
#include <chrono>
#include <memory>
#include <string>
#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/device_session_factory.h"
#include "cec_injector/mate/libcec/libcec_api.h"
namespace cec_injector::mate::libcec {
struct LibCecSessionOptions { std::string explicit_port;
  std::chrono::milliseconds operation_timeout{2500}; };
class LibCecDeviceSession final : public DeviceSession {
 public:
  LibCecDeviceSession(DeviceId, DeviceSessionConfiguration,
                      LibCecSessionOptions,
                      LibCecApiFactory = create_real_libcec_api);
  ~LibCecDeviceSession() override;
  [[nodiscard]] const DeviceId& device_id() const noexcept override;
  [[nodiscard]] SessionState state() const noexcept override;
  void set_observer(DeviceSessionObserver*) override;
  OperationResult connect() override; OperationResult disconnect() override;
  OperationResult refresh_state() override;
  [[nodiscard]] DeviceStateSnapshot snapshot() const override;
  OperationResult power_on_tv() override; OperationResult standby_tv() override;
  OperationResult make_active_source() override;
  [[nodiscard]] WakeState query_wake_state() override;
  OperationResult configure_wake(std::uint16_t) override;
  OperationResult arm_wake() override; OperationResult disarm_wake() override;
  [[nodiscard]] WakeReason read_last_wake_reason() override;
  OperationResult clear_last_wake_reason() override;
 private: class Impl; std::unique_ptr<Impl> impl_;
};
[[nodiscard]] OperationResult map_backend_result(BackendResult);
}  // namespace cec_injector::mate::libcec

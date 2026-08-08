#pragma once

#include <cstdint>
#include <span>

#include "cec_injector/mate/types.h"

namespace cec_injector::mate {

class DeviceSessionObserver {
 public:
  virtual ~DeviceSessionObserver() = default;

  virtual void on_session_state_changed(const DeviceId& device,
                                        SessionState state) = 0;
  virtual void on_external_cec_frame(const DeviceId& device,
                                     std::span<const std::uint8_t> frame) = 0;
  virtual void on_wake_state_changed(const DeviceId& device,
                                     const WakeState& state) = 0;
};

class DeviceSession {
 public:
  virtual ~DeviceSession() = default;

  [[nodiscard]] virtual const DeviceId& device_id() const noexcept = 0;
  [[nodiscard]] virtual SessionState state() const noexcept = 0;
  virtual void set_observer(DeviceSessionObserver* observer) = 0;

  virtual OperationResult connect() = 0;
  virtual OperationResult disconnect() = 0;
  virtual OperationResult refresh_state() = 0;
  [[nodiscard]] virtual DeviceStateSnapshot snapshot() const = 0;

  virtual OperationResult power_on_tv() = 0;
  virtual OperationResult standby_tv() = 0;
  virtual OperationResult make_active_source() = 0;

  [[nodiscard]] virtual WakeState query_wake_state() = 0;
  virtual OperationResult configure_wake(std::uint16_t physical_address) = 0;
  virtual OperationResult arm_wake() = 0;
  virtual OperationResult disarm_wake() = 0;
  [[nodiscard]] virtual WakeReason read_last_wake_reason() = 0;
  virtual OperationResult clear_last_wake_reason() = 0;
};

}  // namespace cec_injector::mate

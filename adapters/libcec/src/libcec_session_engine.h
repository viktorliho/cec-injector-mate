#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "cec_injector/mate/libcec/libcec_device_session.h"
#include "wake_extension_client.h"

namespace cec_injector::mate::libcec::detail {

class SessionEventObserver {
 public:
  virtual ~SessionEventObserver() = default;
  virtual void on_state_changed(SessionState state) = 0;
  virtual void on_external_cec_frame(std::span<const std::uint8_t> frame) = 0;
};

class LibCecSessionEngine {
 public:
  LibCecSessionEngine(DeviceId id, LibCecSessionOptions options,
                      LibCecApiFactory factory, SessionEventObserver& observer);
  ~LibCecSessionEngine();

  [[nodiscard]] const DeviceId& device_id() const noexcept { return id_; }
  [[nodiscard]] SessionState state() const noexcept { return state_value_.load(); }

  OperationResult connect(std::function<OperationResult(LibCecApi&)> initialize,
                          std::function<void(DeviceStateSnapshot&, LibCecApi&)>
                              update_snapshot,
                          std::chrono::milliseconds timeout);
  OperationResult disconnect();

  OperationResult invoke(std::function<OperationResult()> fn,
                         std::chrono::milliseconds timeout = {});
  OperationResult operation(std::function<BackendResult(LibCecApi&)> fn);

  [[nodiscard]] DeviceStateSnapshot snapshot() const;
  void set_wake_state(const WakeState& state);
  [[nodiscard]] WakeExtensionClient* wake() const noexcept {
    return wake_.get();
  }
  void clear_wake_physical_address() noexcept { wake_physical_address_ = 0xffff; }
  [[nodiscard]] std::uint16_t wake_physical_address() const noexcept {
    return wake_physical_address_;
  }
  void set_wake_physical_address(std::uint16_t value) noexcept {
    wake_physical_address_ = value;
  }

 private:
  struct Event {
    bool lost{};
    PortableCecFrame frame;
  };
  using Task = std::packaged_task<OperationResult()>;

  void run();
  void enqueue(Event event);
  void dispatch(const Event& event);
  void set_state(SessionState state);
  void log(std::string message) const;
  [[nodiscard]] std::string frame_context(const PortableCecFrame& frame) const;
  [[nodiscard]] static bool is_standby(const Event& event);

  DeviceId id_;
  LibCecSessionOptions options_;
  LibCecApiFactory factory_;
  SessionEventObserver& observer_;

  std::unique_ptr<LibCecApi> api_;
  std::unique_ptr<WakeExtensionClient> wake_;
  std::uint16_t wake_physical_address_{0xffff};

  std::atomic<SessionState> state_value_{SessionState::Discovered};
  mutable std::mutex state_mu_;
  DeviceStateSnapshot snapshot_value_{};

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Task> tasks_;
  std::deque<Event> events_;
  bool stopping_{};
  std::thread worker_;
};

}  // namespace cec_injector::mate::libcec::detail

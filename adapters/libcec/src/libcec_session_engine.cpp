#include "libcec_session_engine.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace cec_injector::mate::libcec::detail {

LibCecSessionEngine::LibCecSessionEngine(DeviceId id, LibCecSessionOptions options,
                                         LibCecApiFactory factory,
                                         SessionEventObserver& observer)
    : id_(std::move(id)),
      options_(std::move(options)),
      factory_(std::move(factory)),
      observer_(observer),
      worker_(&LibCecSessionEngine::run, this) {}

LibCecSessionEngine::~LibCecSessionEngine() {
  disconnect();
  {
    std::lock_guard lock(mu_);
    stopping_ = true;
  }
  cv_.notify_all();
  worker_.join();
}

OperationResult LibCecSessionEngine::invoke(std::function<OperationResult()> fn,
                                            std::chrono::milliseconds timeout) {
  Task task(std::move(fn));
  auto future = task.get_future();
  {
    std::lock_guard lock(mu_);
    if (stopping_ || tasks_.size() >= 8) return OperationResult::Rejected;
    tasks_.push_back(std::move(task));
  }
  cv_.notify_one();
  if (timeout == std::chrono::milliseconds::zero()) {
    timeout = options_.operation_timeout;
  }
  if (future.wait_for(timeout) != std::future_status::ready) {
    return OperationResult::Timeout;
  }
  try {
    return future.get();
  } catch (...) {
    return OperationResult::InternalError;
  }
}

OperationResult LibCecSessionEngine::connect(
    std::function<OperationResult(LibCecApi&)> initialize,
    std::function<void(DeviceStateSnapshot&, LibCecApi&)> update_snapshot,
    std::chrono::milliseconds timeout) {
  set_state(SessionState::Connecting);
  const auto result = invoke(
      [this, initialize = std::move(initialize),
       update_snapshot = std::move(update_snapshot)] {
        if (api_) api_->close();
        api_ = factory_ ? factory_() : nullptr;
        if (!api_) return OperationResult::Unsupported;

        api_->set_disconnect_callback([this] { enqueue({.lost = true}); });
        api_->set_frame_callback(
            [this](PortableCecFrame frame) { enqueue({.frame = std::move(frame)}); });

        auto init_result = initialize(*api_);
        if (init_result != OperationResult::Success) {
          api_.reset();
          return init_result;
        }
        init_result =
            map_backend_result(api_->open(options_.explicit_port, std::chrono::milliseconds(5000)));
        if (init_result != OperationResult::Success) {
          api_.reset();
          return init_result;
        }

        DeviceStateSnapshot next_snapshot;
        next_snapshot.capabilities.cec = true;
        next_snapshot.addresses.logical = api_->logical_address();
        update_snapshot(next_snapshot, *api_);
        {
          std::lock_guard lock(state_mu_);
          snapshot_value_ = std::move(next_snapshot);
        }
        wake_ = std::make_unique<WakeExtensionClient>(
            [this](auto command, auto request, auto& response, auto wait) {
              return api_->project_transaction(command, request, response, wait);
            });
        return OperationResult::Success;
      },
      timeout);
  set_state(result == OperationResult::Success ? SessionState::Connected
                                               : SessionState::Disconnected);
  return result;
}

OperationResult LibCecSessionEngine::disconnect() {
  if (!worker_.joinable()) return OperationResult::Success;
  set_state(SessionState::Disconnecting);
  const auto result = invoke([this] {
    if (api_) api_->close();
    api_.reset();
    wake_.reset();
    clear_wake_physical_address();
    return OperationResult::Success;
  });
  set_state(SessionState::Disconnected);
  return result;
}

OperationResult LibCecSessionEngine::operation(
    std::function<BackendResult(LibCecApi&)> fn) {
  return invoke([this, fn = std::move(fn)] {
    if (!api_ || state_value_ != SessionState::Connected) {
      return OperationResult::Disconnected;
    }
    return map_backend_result(fn(*api_));
  });
}

DeviceStateSnapshot LibCecSessionEngine::snapshot() const {
  std::lock_guard lock(state_mu_);
  return snapshot_value_;
}

void LibCecSessionEngine::set_wake_state(const WakeState& state) {
  std::lock_guard lock(state_mu_);
  snapshot_value_.wake = state;
}

void LibCecSessionEngine::run() {
  for (;;) {
    Task task;
    std::optional<Event> event;
    {
      std::unique_lock lock(mu_);
      cv_.wait(lock, [&] { return stopping_ || !tasks_.empty() || !events_.empty(); });
      if (stopping_ && tasks_.empty() && events_.empty()) break;
      if (!events_.empty()) {
        event = std::move(events_.front());
        events_.pop_front();
      } else {
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
    }
    if (event) {
      dispatch(*event);
    } else {
      task();
    }
  }
}

bool LibCecSessionEngine::is_standby(const Event& event) {
  return !event.lost && event.frame.initiator == 0 &&
         event.frame.destination == 0x0f && event.frame.opcode &&
         *event.frame.opcode == 0x36 && event.frame.parameters.empty();
}

std::string LibCecSessionEngine::frame_context(const PortableCecFrame& frame) const {
  std::ostringstream out;
  out << "source=" << std::hex << static_cast<unsigned>(frame.initiator)
      << " destination=" << static_cast<unsigned>(frame.destination)
      << " opcode=";
  if (frame.opcode) {
    out << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(*frame.opcode);
  } else {
    out << "none";
  }
  out << std::dec << " parameter_count=" << frame.parameters.size();
  return out.str();
}

void LibCecSessionEngine::log(std::string message) const {
  if (options_.diagnostics) options_.diagnostics(message);
}

void LibCecSessionEngine::enqueue(Event event) {
  std::lock_guard lock(mu_);
  const bool standby = is_standby(event);
  if (stopping_) {
    if (!event.lost) {
      log("cec_rx dropped reason=session_closing " + frame_context(event.frame));
    }
    return;
  }
  if (events_.size() >= 32) {
    if (event.lost) {
      const auto ordinary = std::find_if(events_.begin(), events_.end(), [](const Event& queued) {
        return !queued.lost && !is_standby(queued);
      });
      if (ordinary != events_.end()) {
        events_.erase(ordinary);
        log("cec_rx dropped reason=queue_full evicted=ordinary_for_session_lost");
      } else {
        const bool displaced_standby = is_standby(events_.front());
        events_.pop_front();
        log(std::string("cec_rx dropped reason=queue_full evicted=") +
            (displaced_standby ? "external_tv_standby_for_session_lost"
                               : "session_lost_for_session_lost"));
      }
    } else if (standby) {
      const auto ordinary = std::find_if(events_.begin(), events_.end(),
                                         [](const Event& queued) {
                                           return !queued.lost && !is_standby(queued);
                                         });
      if (ordinary == events_.end()) {
        log("cec_rx dropped reason=queue_full semantic=external_tv_standby " +
            frame_context(event.frame));
        return;
      }
      events_.erase(ordinary);
      log("cec_rx dropped reason=queue_full evicted=ordinary_for_external_tv_standby");
    } else {
      log("cec_rx dropped reason=queue_full " + frame_context(event.frame));
      return;
    }
  }
  if (standby) {
    log("cec_rx enqueue semantic=external_tv_standby " + frame_context(event.frame));
  }
  events_.push_back(std::move(event));
  cv_.notify_one();
}

void LibCecSessionEngine::dispatch(const Event& event) {
  if (event.lost) {
    set_state(SessionState::Disconnected);
    return;
  }
  std::vector<std::uint8_t> bytes;
  bytes.push_back(
      static_cast<std::uint8_t>(event.frame.initiator << 4 | event.frame.destination));
  if (event.frame.opcode) bytes.push_back(*event.frame.opcode);
  bytes.insert(bytes.end(), event.frame.parameters.begin(), event.frame.parameters.end());
  observer_.on_external_cec_frame(bytes);
}

void LibCecSessionEngine::set_state(SessionState state) {
  state_value_ = state;
  observer_.on_state_changed(state);
}

}  // namespace cec_injector::mate::libcec::detail

#include "cec_injector/mate/mate_service.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/semantic_event_queue.h"

namespace cec_injector::mate {
namespace {

std::optional<SemanticRemoteAction> RemoteAction(std::uint8_t code) {
  switch (code) {
    case 0x00: return SemanticRemoteAction::Select;
    case 0x01: return SemanticRemoteAction::Up;
    case 0x02: return SemanticRemoteAction::Down;
    case 0x03: return SemanticRemoteAction::Left;
    case 0x04: return SemanticRemoteAction::Right;
    case 0x09: return SemanticRemoteAction::Home;
    case 0x0d: return SemanticRemoteAction::Back;
    case 0x35: return SemanticRemoteAction::Info;
    case 0x44: return SemanticRemoteAction::Play;
    case 0x45: return SemanticRemoteAction::Stop;
    case 0x46: return SemanticRemoteAction::Pause;
    case 0x4b: return SemanticRemoteAction::Next;
    case 0x4c: return SemanticRemoteAction::Previous;
    case 0x53: return SemanticRemoteAction::Guide;
    case 0x61: return SemanticRemoteAction::PlayPause;
    case 0x71: return SemanticRemoteAction::Next;
    case 0x72: return SemanticRemoteAction::Previous;
    case 0x73: return SemanticRemoteAction::Pause;
    case 0x74: return SemanticRemoteAction::Play;
    default: return std::nullopt;
  }
}

bool IsDeviceIntent(PolicyIntent intent) {
  switch (intent) {
    case PolicyIntent::PrepareWake:
    case PolicyIntent::DisarmWake:
    case PolicyIntent::StandbyTv:
    case PolicyIntent::PowerOnTv:
    case PolicyIntent::MakeActiveSource:
    case PolicyIntent::RefreshDeviceSession:
    case PolicyIntent::ReadWakeReason:
      return true;
    case PolicyIntent::RequestHostSuspend:
    case PolicyIntent::ResetRemoteInput:
    case PolicyIntent::CompleteHostTransition:
      return false;
  }
  return false;
}

bool IsRecoverableResumeRefreshResult(OperationResult result) {
  return result == OperationResult::Timeout ||
         result == OperationResult::Disconnected ||
         result == OperationResult::InvalidState;
}

std::string_view IntentName(PolicyIntent intent) {
  switch (intent) {
    case PolicyIntent::RequestHostSuspend: return "RequestHostSuspend";
    case PolicyIntent::PrepareWake: return "PrepareWake";
    case PolicyIntent::DisarmWake: return "DisarmWake";
    case PolicyIntent::StandbyTv: return "StandbyTv";
    case PolicyIntent::PowerOnTv: return "PowerOnTv";
    case PolicyIntent::MakeActiveSource: return "MakeActiveSource";
    case PolicyIntent::ResetRemoteInput: return "ResetRemoteInput";
    case PolicyIntent::RefreshDeviceSession: return "RefreshDeviceSession";
    case PolicyIntent::ReadWakeReason: return "ReadWakeReason";
    case PolicyIntent::CompleteHostTransition: return "CompleteHostTransition";
  }
  return "Unknown";
}

std::string_view EventName(LifecycleEvent event) {
  switch (event) {
    case LifecycleEvent::BootRequested: return "boot";
    case LifecycleEvent::SessionConnected: return "session_connected";
    case LifecycleEvent::SessionLost: return "session_lost";
    case LifecycleEvent::ExternalTvStandby: return "external_tv_standby";
    case LifecycleEvent::HostPreparingSuspend: return "preparing_suspend";
    case LifecycleEvent::HostResumed: return "resume";
    case LifecycleEvent::HostPreparingShutdown: return "preparing_shutdown";
  }
  return "unknown";
}

std::string_view ResultName(OperationResult result) {
  switch (result) {
    case OperationResult::Success: return "Success";
    case OperationResult::Unsupported: return "Unsupported";
    case OperationResult::InvalidState: return "InvalidState";
    case OperationResult::InvalidConfiguration: return "InvalidConfiguration";
    case OperationResult::Rejected: return "Rejected";
    case OperationResult::Timeout: return "Timeout";
    case OperationResult::Disconnected: return "Disconnected";
    case OperationResult::MalformedResponse: return "MalformedResponse";
    case OperationResult::PermissionDenied: return "PermissionDenied";
    case OperationResult::InternalError: return "InternalError";
  }
  return "Unknown";
}

std::string_view SessionStateName(SessionState state) {
  switch (state) {
    case SessionState::Discovered: return "discovered";
    case SessionState::Connecting: return "connecting";
    case SessionState::Connected: return "connected";
    case SessionState::Degraded: return "degraded";
    case SessionState::Disconnecting: return "disconnecting";
    case SessionState::Disconnected: return "disconnected";
  }
  return "unknown";
}

}  // namespace

struct MateService::Impl {
  using Clock = std::chrono::steady_clock;

  enum class TaskKind {
    RefreshDiscovery,
    ResumeDeadline,
    Boot,
    SessionState,
    Frame,
    Wake,
    Host,
    Configuration,
    ManualAction,
  };
  struct Task {
    explicit Task(TaskKind value) : kind(value) {}
    Task(TaskKind value, std::uint64_t token, DeviceId id)
        : kind(value), generation(token), device(std::move(id)) {}
    TaskKind kind;
    std::uint64_t generation{};
    DeviceId device;
    SessionState session_state{SessionState::Disconnected};
    bool received_from_current_session{};
    std::vector<std::uint8_t> frame;
    WakeState wake;
    std::optional<HostLifecycleNotification> host;
    std::optional<MateRuntimeConfiguration> configuration;
    std::shared_ptr<std::promise<OperationResult>> configuration_result;
    MateManualAction manual_action{MateManualAction::PowerOnTv};
    MateManualActionRequestId manual_request{};
  };

  struct ResumeRecovery {
    std::uint64_t epoch{};
    DeviceId device;
    std::uint64_t selection_epoch{};
    Clock::time_point deadline;
    PolicyPlan plan;
    std::vector<OperationResult> results;
    std::size_t refresh_index{};
    std::size_t next_step{};
    std::uint64_t lifecycle_epoch{};
  };

  class SessionObserver final : public DeviceSessionObserver {
   public:
    SessionObserver(Impl& owner, std::uint64_t generation)
        : owner_(owner), generation_(generation) {}
    void disable() noexcept { enabled_ = false; }
    void on_session_state_changed(const DeviceId& device,
                                  SessionState state) override {
      if (!enabled_) return;
      if (state == SessionState::Disconnected) {
        auto expected = generation_;
        (void)owner_.generation.compare_exchange_strong(expected,
                                                        generation_ + 1);
      }
      Task task{TaskKind::SessionState, generation_, device};
      task.session_state = state;
      owner_.post(std::move(task));
    }
    void on_external_cec_frame(const DeviceId& device,
                               std::span<const std::uint8_t> frame) override {
      if (!enabled_) return;
      Task task{TaskKind::Frame, generation_, device};
      task.received_from_current_session =
          owner_.generation.load() == generation_;
      task.frame.assign(frame.begin(), frame.end());
      owner_.post(std::move(task));
    }
    void on_wake_state_changed(const DeviceId& device,
                               const WakeState& wake) override {
      if (!enabled_) return;
      Task task{TaskKind::Wake, generation_, device};
      task.wake = wake;
      owner_.post(std::move(task));
    }

   private:
    Impl& owner_;
    std::uint64_t generation_;
    std::atomic<bool> enabled_{true};
  };

  Impl(HostPlatform& host_value, DeviceSessionFactory& factory_value,
       MateServiceConfiguration configuration, DiagnosticSink sink)
      : host(host_value), factory(factory_value),
        registry(), policy(configuration.policy), events(32),
        explicit_selection(std::move(configuration.selected_device)),
        resume_recovery_timeout(configuration.resume_recovery_timeout),
        maintain_session_wake(configuration.maintain_session_wake),
        runtime_configuration{configuration.policy, configuration.remote},
        diagnostics(std::move(sink)) {
    const auto epoch = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    next_manual_request.store((epoch & 0x7fffffffULL) << 32 | 1ULL);
  }

  void log(std::string_view message) {
    if (diagnostics) diagnostics(message);
  }

  std::string context(std::uint64_t transaction_epoch,
                      std::string_view lifecycle,
                      std::optional<std::uint64_t> operation_generation) const {
    const auto device = active_device_id ? active_device_id : selection_device;
    return std::string("lifecycle=") + std::string(lifecycle) +
        " epoch=" + std::to_string(transaction_epoch) +
        " device=" + (device ? std::string(device->diagnostic_value()) : "none") +
        " selection=" + std::to_string(selection_epoch) +
        " generation=" +
        (operation_generation ? std::to_string(*operation_generation) : "none");
  }

  std::string operation_context(PolicyIntent intent,
                                std::uint64_t transaction_epoch,
                                std::string_view lifecycle,
                                std::uint64_t operation_generation) const {
    return context(transaction_epoch, lifecycle,
                   IsDeviceIntent(intent)
                       ? std::optional<std::uint64_t>(operation_generation)
                       : std::nullopt);
  }

  void post(Task task) {
    std::lock_guard lock(mutex);
    if (stopping) {
      if (task.kind == TaskKind::Frame) {
        log("cec_rx dropped reason=session_closing device=" +
            std::string(task.device.diagnostic_value()) + " generation=" +
            std::to_string(task.generation));
      }
      if (task.host && task.host->transition_hold) {
        task.host->transition_hold->complete();
      }
      return;
    }
    tasks.push_back(std::move(task));
    changed.notify_one();
  }

  void run() {
    for (;;) {
      Task task{TaskKind::RefreshDiscovery};
      {
        std::unique_lock lock(mutex);
        if (!stopping && tasks.empty() && resume_recovery) {
          if (!changed.wait_until(
                  lock, resume_recovery->deadline,
                  [this] { return stopping || !tasks.empty(); })) {
            tasks.emplace_back(TaskKind::ResumeDeadline);
          }
        } else {
          changed.wait(lock, [this] { return stopping || !tasks.empty(); });
        }
        if (stopping && tasks.empty()) break;
        task = std::move(tasks.front());
        tasks.pop_front();
        busy = true;
      }
      handle(std::move(task));
      refresh_frontend_snapshot();
      {
        std::lock_guard lock(mutex);
        busy = false;
      }
      idle.notify_all();
    }
  }

  void handle(Task task) {
    switch (task.kind) {
      case TaskKind::RefreshDiscovery: reconcile_discovery(); break;
      case TaskKind::ResumeDeadline: expire_resume_recovery(); break;
      case TaskKind::Boot: execute_event(LifecycleEvent::BootRequested, nullptr); break;
      case TaskKind::SessionState: handle_session_state(task); break;
      case TaskKind::Frame: handle_frame(task); break;
      case TaskKind::Wake:
        if (active_device_id && task.device == *active_device_id &&
            task.generation == active_generation) {
          last_wake_state = task.wake;
          log("Wake state changed");
        }
        break;
      case TaskKind::Host: handle_host(std::move(*task.host)); break;
      case TaskKind::Configuration: {
        MateRuntimeConfiguration previous;
        {
          std::lock_guard lock(mutex);
          previous = runtime_configuration;
        }
        auto result = host.apply_remote_input_configuration(
            task.configuration->remote);
        if (result == OperationResult::Success ||
            result == OperationResult::Unsupported) {
          policy.set_configuration(task.configuration->policy);
          {
            std::lock_guard lock(mutex);
            runtime_configuration = *task.configuration;
          }
          result = OperationResult::Success;
        } else {
          (void)host.apply_remote_input_configuration(previous.remote);
        }
        task.configuration_result->set_value(result);
        break;
      }
      case TaskKind::ManualAction: execute_manual_action(task); break;
    }
  }

  void notify_cec(const MateServiceCecEvent& event) {
    std::lock_guard lock(frontend_observer_mutex);
    if (frontend_observer) frontend_observer->on_service_cec_event(event);
  }

  void refresh_frontend_snapshot() {
    MateServiceSnapshot next;
    next.running = true;
    next.device_count = inventory.size();
    next.selected_device = registry.snapshot().selected_active;
    if (next.selected_device) {
      if (const auto* selected = find_device(*next.selected_device)) {
        next.device_display_name = selected->display_name;
      }
    }
    next.host_power_state = host.host_power_state();
    next.display = host.display_summary();
    next.session_generation = generation.load();
    next.physical_address_status = last_physical_address_resolution.status;
    next.physical_address = last_physical_address_resolution.address;
    if (session) {
      next.session_state = session->state();
      next.device_state = session->snapshot();
    }
    {
      std::lock_guard lock(mutex);
      frontend_snapshot = next;
    }
    std::lock_guard observer_lock(frontend_observer_mutex);
    if (frontend_observer) frontend_observer->on_service_snapshot(next);
  }

  void execute_manual_action(const Task& task) {
    const auto started = Clock::now();
    OperationResult result = OperationResult::Disconnected;
    if (session) {
      switch (task.manual_action) {
        case MateManualAction::PowerOnTv: result = session->power_on_tv(); break;
        case MateManualAction::MakeActiveSource:
          result = session->make_active_source();
          break;
        case MateManualAction::StandbyTv: result = session->standby_tv(); break;
      }
    }
    MateManualActionResult completed{
        task.manual_action, result,
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started),
        {}};
    if (session) {
      MateServiceCecEvent event;
      event.timestamp = std::chrono::system_clock::now();
      event.received = false;
      const auto snapshot = session->snapshot();
      event.source = snapshot.addresses.logical.value_or(4);
      event.destination = task.manual_action == MateManualAction::MakeActiveSource
                              ? 15 : 0;
      switch (task.manual_action) {
      case MateManualAction::PowerOnTv:
        event.opcode = 0x04;
        event.semantic = "power_on_tv";
        break;
      case MateManualAction::MakeActiveSource:
        event.opcode = 0x82;
        event.parameters = {static_cast<std::uint8_t>(active_physical_address >> 8),
                            static_cast<std::uint8_t>(active_physical_address)};
        event.semantic = "make_active_source";
        break;
      case MateManualAction::StandbyTv:
        event.opcode = 0x36;
        event.semantic = "standby_tv";
        break;
      }
      notify_cec(event);
    }
    std::lock_guard lock(frontend_observer_mutex);
    if (frontend_observer) {
      frontend_observer->on_manual_action_completed(task.manual_request,
                                                    completed);
    }
  }

  void reconcile_discovery() {
    expire_resume_recovery();
    auto discovered = host.discovered_devices();
    const bool inventory_changed = discovered != inventory;
    inventory = discovered;
    std::vector<DeviceId> ids;
    ids.reserve(discovered.size());
    for (const auto& device : discovered) ids.push_back(device.id);
    if (inventory_changed || !discovery_snapshot_logged) {
      discovery_snapshot_logged = true;
      if (diagnostics) {
        diagnostics(std::string("discovery changed count=") +
                    std::to_string(discovered.size()));
        diagnostics(std::string("discovery Device count ") +
                    std::to_string(discovered.size()));
      }
    }
    const auto intents = registry.update_discovered(std::move(ids));
    update_selection_episode();
    for (const auto& intent : intents) {
      if (intent.kind == CoreIntentKind::DisconnectDevice) {
        drop_session();
      } else {
        establish_session(intent.device);
      }
    }
    const auto selected = registry.snapshot().selected_active;
    if (!selected) return;
    if (!session) {
      establish_session(*selected);
      return;
    }
    const auto resolution = host.resolve_physical_address();
    last_physical_address_resolution = resolution;
    if (resolution.status == PhysicalAddressStatus::Available &&
        resolution.address && *resolution.address != active_physical_address) {
      log("physical address changed; replacing session");
      drop_session();
      establish_session(*selected);
    }
  }

  void update_selection_episode() {
    const auto& snapshot = registry.snapshot();
    const auto target = snapshot.explicit_selection
        ? snapshot.explicit_selection
        : snapshot.selected_active;
    if (target == selection_device) return;
    selection_device = target;
    ++selection_epoch;
    if (diagnostics) {
      diagnostics(std::string("selection changed device=") +
                  (selection_device
                       ? std::string(selection_device->diagnostic_value())
                       : "none") +
                  " selection=" + std::to_string(selection_epoch));
    }
    if (resume_recovery) cancel_resume_recovery("selection changed");
  }

  const DiscoveredDevice* find_device(const DeviceId& id) const {
    const DiscoveredDevice* result = nullptr;
    for (const auto& device : inventory) {
      if (device.id != id) continue;
      if (result) return nullptr;
      result = &device;
    }
    return result;
  }

  void establish_session(const DeviceId& id) {
    if (session) return;
    const auto* discovered = find_device(id);
    if (!discovered) return;
    if (diagnostics) {
      const auto message = std::string("Device selected device=") +
                           std::string(id.diagnostic_value()) +
                           " selection=" + std::to_string(selection_epoch);
      diagnostics(message);
    }
    const auto resolution = host.resolve_physical_address();
    last_physical_address_resolution = resolution;
    if (resolution.status != PhysicalAddressStatus::Available ||
        !resolution.address) {
      log(resolution.status == PhysicalAddressStatus::Ambiguous
              ? "physical address ambiguous"
              : "physical address unavailable");
      return;
    }
    DeviceSessionConfiguration configuration{*resolution.address};
    if (!configuration.valid()) return;
    if (diagnostics) {
      const auto address = configuration.physical_address;
      const auto message = std::string("physical address available ") +
          std::to_string((address >> 12) & 0xf) + "." +
          std::to_string((address >> 8) & 0xf) + "." +
          std::to_string((address >> 4) & 0xf) + "." +
          std::to_string(address & 0xf);
      diagnostics(message);
    }
    auto created = factory.create_session(*discovered, configuration);
    if (!created || created->state() == SessionState::Connected) return;
    const auto next = generation.fetch_add(1) + 1;
    active_generation = next;
    active_physical_address = configuration.physical_address;
    active_device_id = id;
    if (diagnostics && last_closed_generation != 0) {
      diagnostics(std::string("DeviceSession replaced device=") +
                  std::string(id.diagnostic_value()) +
                  " selection=" + std::to_string(selection_epoch) +
                  " old_generation=" +
                  std::to_string(last_closed_generation) +
                  " generation=" + std::to_string(next));
    }
    last_closed_generation = 0;
    session_observer = std::make_unique<SessionObserver>(*this, next);
    session = std::move(created);
    session->set_observer(session_observer.get());
    if (diagnostics) {
      diagnostics(std::string("DeviceSession connecting device=") +
                  std::string(id.diagnostic_value()) +
                  " selection=" + std::to_string(selection_epoch) +
                  " generation=" + std::to_string(next));
    }
    const auto result = session->connect();
    if (result != OperationResult::Success) {
      if (diagnostics) {
        diagnostics(std::string("DeviceSession connect_failed device=") +
                    std::string(id.diagnostic_value()) +
                    " selection=" + std::to_string(selection_epoch) +
                    " generation=" + std::to_string(next) +
                    " result=" + std::string(ResultName(result)));
      }
      drop_session();
    } else {
      if (diagnostics) {
        diagnostics(std::string("DeviceSession connected device=") +
                    std::string(id.diagnostic_value()) +
                    " selection=" + std::to_string(selection_epoch) +
                    " generation=" + std::to_string(next));
        diagnostics("DeviceSession connected");
      }
    }
  }

  void drop_session() {
    const auto dropped_device = active_device_id;
    const auto dropped_generation = active_generation;
    generation.fetch_add(1);
    (void)host.reset_remote_input();
    active_remote_action.reset();
    if (session_observer) session_observer->disable();
    if (session) {
      if (diagnostics && dropped_device) {
        diagnostics(std::string("DeviceSession disconnecting device=") +
                    std::string(dropped_device->diagnostic_value()) +
                    " selection=" + std::to_string(selection_epoch) +
                    " generation=" + std::to_string(dropped_generation));
      }
      session->set_observer(nullptr);
      (void)session->disconnect();
    }
    session.reset();
    session_observer.reset();
    active_device_id.reset();
    active_physical_address = 0xffff;
    active_generation = 0;
    if (diagnostics && dropped_device) {
      diagnostics(std::string("DeviceSession closed device=") +
                  std::string(dropped_device->diagnostic_value()) +
                  " selection=" + std::to_string(selection_epoch) +
                  " old_generation=" + std::to_string(dropped_generation) +
                  " current_generation=" + std::to_string(generation.load()));
      last_closed_generation = dropped_generation;
    }
  }

  void handle_session_state(const Task& task) {
    if (!active_device_id || task.device != *active_device_id ||
        task.generation != active_generation) return;
    if (task.session_state == SessionState::Connected) {
      if (diagnostics) {
        diagnostics(std::string("DeviceSession state=") +
                    std::string(SessionStateName(task.session_state)) +
                    " device=" + std::string(task.device.diagnostic_value()) +
                    " selection=" + std::to_string(selection_epoch) +
                    " generation=" + std::to_string(task.generation) +
                    " resume_continuation=" +
                    (resume_recovery ? "pending" : "none"));
      }
      expire_resume_recovery();
      if (maintain_session_wake) {
        execute_event(LifecycleEvent::SessionConnected, nullptr,
                      task.generation);
      }
      continue_resume_recovery(task);
    } else if (task.session_state == SessionState::Disconnected) {
      if (diagnostics) {
        diagnostics(std::string("DeviceSession state=lost device=") +
                    std::string(task.device.diagnostic_value()) +
                    " selection=" + std::to_string(selection_epoch) +
                    " generation=" + std::to_string(task.generation));
      }
      execute_event(LifecycleEvent::SessionLost, nullptr, task.generation);
      const auto desired = registry.snapshot().selected_active;
      drop_session();
      if (desired) establish_session(*desired);
    }
  }

  void handle_frame(const Task& task) {
    if (task.frame.size() >= 2) {
      MateServiceCecEvent event;
      event.timestamp = std::chrono::system_clock::now();
      event.received = true;
      event.source = task.frame[0] >> 4;
      event.destination = task.frame[0] & 0x0f;
      event.opcode = task.frame[1];
      event.parameters.assign(task.frame.begin() + 2, task.frame.end());
      if (event.opcode == 0x36) event.semantic = "external_tv_standby";
      notify_cec(event);
    }
    const auto frame_context = [&task] {
      if (task.frame.empty()) return std::string("source=unknown destination=unknown opcode=none parameter_count=unknown");
      const auto header = task.frame[0];
      std::ostringstream out;
      out << std::hex << "source=" << static_cast<unsigned>(header >> 4)
          << " destination=" << static_cast<unsigned>(header & 0x0f)
          << " opcode=";
      if (task.frame.size() >= 2) {
        out << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(task.frame[1]);
      } else {
        out << "none";
      }
      out << std::dec << " parameter_count="
          << (task.frame.size() >= 2 ? task.frame.size() - 2 : 0);
      return out.str();
    };
    const auto drop = [this, &task, &frame_context](std::string_view reason) {
      log("cec_rx dropped reason=" + std::string(reason) + " " +
          frame_context() + " device=" +
          std::string(task.device.diagnostic_value()) + " generation=" +
          std::to_string(task.generation));
    };
    if (!session || !active_device_id) {
      drop("no_active_session");
      return;
    }
    if (!task.received_from_current_session ||
        task.device != *active_device_id ||
        task.generation != active_generation) {
      drop("stale_generation");
      return;
    }
    if (task.frame.size() < 2) {
      drop("malformed_frame");
      return;
    }
    const auto header = task.frame[0];
    const auto initiator = static_cast<std::uint8_t>(header >> 4);
    const auto destination = static_cast<std::uint8_t>(header & 0x0f);
    const auto opcode = task.frame[1];
    if (opcode == 0x36) {
      if (initiator != 0) {
        drop("wrong_initiator");
        return;
      }
      if (destination != 0x0f) {
        drop("wrong_destination");
        return;
      }
      if (task.frame.size() != 2) {
        drop("malformed_frame");
        return;
      }
      log("cec_rx accepted " + frame_context() + " device=" +
          std::string(task.device.diagnostic_value()) + " selection=" +
          std::to_string(selection_epoch) + " generation=" +
          std::to_string(task.generation));
      log("cec_rx semantic=external_tv_standby device=" +
          std::string(task.device.diagnostic_value()) + " selection=" +
          std::to_string(selection_epoch) + " generation=" +
          std::to_string(task.generation));
      execute_event(LifecycleEvent::ExternalTvStandby, nullptr,
                    task.generation);
      return;
    }
    const auto logical = session->snapshot().addresses.logical;
    if (initiator != 0 || !logical || destination != *logical) return;
    if (opcode == 0x44 && task.frame.size() == 3) {
      const auto action = RemoteAction(task.frame[2]);
      if (!action) return;
      if (host.emit_remote_action(*action, RemoteActionState::Pressed) ==
          OperationResult::Success) {
        active_remote_action = action;
      }
    } else if (opcode == 0x45 && task.frame.size() == 2 &&
               active_remote_action) {
      (void)host.emit_remote_action(*active_remote_action,
                                    RemoteActionState::Released);
      active_remote_action.reset();
    }
  }

  void handle_host(HostLifecycleNotification notification) {
    if (diagnostics) {
      diagnostics(std::string("host lifecycle event=") +
                  (notification.event == HostLifecycleEvent::PreparingSuspend
                       ? "preparing_suspend"
                       : notification.event == HostLifecycleEvent::Resumed
                             ? "resume"
                             : notification.event == HostLifecycleEvent::PreparingShutdown
                                   ? "preparing_shutdown"
                                   : "shutdown_cancelled") +
                  " generation=" + std::to_string(generation.load()));
    }
    switch (notification.event) {
      case HostLifecycleEvent::PreparingSuspend:
        cancel_resume_recovery("host preparing suspend");
        execute_event(LifecycleEvent::HostPreparingSuspend,
                      notification.transition_hold
                          ? &*notification.transition_hold
                          : nullptr);
        break;
      case HostLifecycleEvent::Resumed:
        cancel_resume_recovery("superseded by HostResumed");
        execute_event(LifecycleEvent::HostResumed, nullptr);
        break;
      case HostLifecycleEvent::PreparingShutdown:
        cancel_resume_recovery("host preparing shutdown");
        execute_event(LifecycleEvent::HostPreparingShutdown,
                      notification.transition_hold
                          ? &*notification.transition_hold
                          : nullptr);
        break;
      case HostLifecycleEvent::ShutdownCancelled:
        break;
    }
    if (notification.transition_hold && notification.transition_hold->active()) {
      (void)notification.transition_hold->complete();
    }
  }

  void execute_event(LifecycleEvent event, HostTransitionHold* hold,
                     std::optional<std::uint64_t> event_generation = std::nullopt) {
    if (events.submit(event) != EventSubmitResult::Accepted) {
      if (hold) (void)hold->complete();
      return;
    }
    const auto active = events.begin_next();
    if (!active) {
      if (hold) (void)hold->complete();
      return;
    }
    const auto plan_generation = event_generation.value_or(generation.load());
    const auto transaction_epoch = ++lifecycle_epoch;
    const auto lifecycle = EventName(event);
    if (diagnostics) {
      diagnostics(std::string("lifecycle transaction start ") +
                  context(transaction_epoch, lifecycle, plan_generation));
    }
    const auto plan = policy.handle(*active);
    if (event == LifecycleEvent::HostResumed) {
      execute_resume_plan(plan, plan_generation, transaction_epoch);
      events.complete_active();
      return;
    }
    std::vector<OperationResult> results;
    results.reserve(plan.steps.size());
    for (const auto& step : plan.steps) {
      OperationResult result = OperationResult::InvalidState;
      bool skipped = false;
      if (step.requires_success &&
          (*step.requires_success >= results.size() ||
           results[*step.requires_success] != OperationResult::Success)) {
        result = OperationResult::Rejected;
        skipped = true;
        last_operation_duration = std::chrono::milliseconds::zero();
        log_operation_skipped(step.intent, transaction_epoch, lifecycle,
                              plan_generation);
      } else if (IsDeviceIntent(step.intent) &&
                 (generation.load() != plan_generation || !session ||
                  active_generation != plan_generation)) {
        result = OperationResult::Disconnected;
        last_operation_duration = std::chrono::milliseconds::zero();
        log("discarded stale Device work");
      } else {
        result = execute_timed_intent(step.intent, hold, transaction_epoch,
                                      lifecycle, plan_generation);
      }
      results.push_back(result);
      record_intent_result(step.intent, result, transaction_epoch, lifecycle,
                           plan_generation, last_operation_duration, !skipped);
    }
    events.complete_active();
    if (diagnostics) {
      diagnostics(std::string("lifecycle transaction complete ") +
                  context(transaction_epoch, lifecycle, plan_generation));
    }
    if (hold && hold->active()) (void)hold->complete();
  }

  void record_intent_result(PolicyIntent intent, OperationResult result,
                            std::uint64_t transaction_epoch = 0,
                            std::string_view lifecycle = "maintenance",
                            std::uint64_t operation_generation = 0,
                            std::chrono::milliseconds duration = {},
                            bool emit_completion = true) {
    policy.record_result(intent, result);
    if (diagnostics && emit_completion) {
      const auto message = std::string("operation complete name=") +
          std::string(IntentName(intent)) + " result=" +
          std::string(ResultName(result)) + " duration_ms=" +
          std::to_string(duration.count()) + " " +
          operation_context(intent, transaction_epoch, lifecycle,
                            operation_generation);
      diagnostics(message);
    }
  }

  void log_operation_skipped(PolicyIntent intent,
                             std::uint64_t transaction_epoch,
                             std::string_view lifecycle,
                             std::uint64_t operation_generation) {
    if (!diagnostics) return;
    diagnostics(std::string("operation skipped name=") +
                std::string(IntentName(intent)) +
                " reason=skipped_due_to_dependency " +
                operation_context(intent, transaction_epoch, lifecycle,
                                  operation_generation));
  }

  OperationResult execute_timed_intent(PolicyIntent intent,
                                       HostTransitionHold* hold,
                                       std::uint64_t transaction_epoch,
                                       std::string_view lifecycle,
                                       std::uint64_t operation_generation) {
    if (diagnostics) {
      diagnostics(std::string("operation start name=") +
                  std::string(IntentName(intent)) + " " +
                  operation_context(intent, transaction_epoch, lifecycle,
                                    operation_generation));
    }
    const auto started = Clock::now();
    const auto result = execute_intent(intent, hold);
    last_operation_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - started);
    return result;
  }

  OperationResult execute_plan_step(const PolicyStep& step,
                                    std::vector<OperationResult>& results,
                                    std::uint64_t plan_generation,
                                    HostTransitionHold* hold,
                                    std::uint64_t transaction_epoch,
                                    std::string_view lifecycle) {
    OperationResult result = OperationResult::InvalidState;
    bool skipped = false;
    if (step.requires_success &&
        (*step.requires_success >= results.size() ||
         results[*step.requires_success] != OperationResult::Success)) {
      result = OperationResult::Rejected;
      skipped = true;
      last_operation_duration = std::chrono::milliseconds::zero();
      log_operation_skipped(step.intent, transaction_epoch, lifecycle,
                            plan_generation);
    } else if (IsDeviceIntent(step.intent) &&
               (generation.load() != plan_generation || !session ||
                active_generation != plan_generation)) {
      result = OperationResult::Disconnected;
      last_operation_duration = std::chrono::milliseconds::zero();
      log("discarded stale Device work");
    } else {
      result = execute_timed_intent(step.intent, hold, transaction_epoch,
                                    lifecycle, plan_generation);
    }
    results.push_back(result);
    record_intent_result(step.intent, result, transaction_epoch, lifecycle,
                         plan_generation, last_operation_duration, !skipped);
    return result;
  }

  void execute_resume_plan(const PolicyPlan& plan,
                           std::uint64_t plan_generation,
                           std::uint64_t transaction_epoch) {
    std::vector<OperationResult> results;
    results.reserve(plan.steps.size());
    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
      const auto result = execute_plan_step(plan.steps[index], results,
                                            plan_generation, nullptr,
                                            transaction_epoch, "resume");
      if (plan.steps[index].intent != PolicyIntent::RefreshDeviceSession ||
          !IsRecoverableResumeRefreshResult(result)) {
        continue;
      }
      const auto selected = selection_device
          ? selection_device
          : registry.snapshot().selected_active;
      if (!selected) continue;
      ResumeRecovery pending;
      pending.epoch = ++resume_epoch;
      pending.device = *selected;
      pending.selection_epoch = selection_epoch;
      pending.deadline = Clock::now() + resume_recovery_timeout;
      pending.plan = plan;
      pending.results = std::move(results);
      pending.refresh_index = index;
      pending.next_step = index + 1;
      pending.lifecycle_epoch = transaction_epoch;
      resume_recovery.emplace(std::move(pending));
      if (diagnostics) {
        diagnostics(std::string("resume path=DEFERRED_RESUME_RECOVERY state=pending resume_epoch=") +
                    std::to_string(resume_recovery->epoch) + " " +
                    context(transaction_epoch, "resume", plan_generation));
      }
      return;
    }
    if (diagnostics) {
      diagnostics(std::string("resume path=IMMEDIATE_RESUME state=complete ") +
                  context(transaction_epoch, "resume", plan_generation));
      diagnostics(std::string("lifecycle transaction complete ") +
                  context(transaction_epoch, "resume", plan_generation));
    }
  }

  void continue_resume_recovery(const Task& task) {
    if (!resume_recovery || task.device != resume_recovery->device ||
        task.generation != active_generation ||
        generation.load() != task.generation) {
      return;
    }
    if (resume_recovery->selection_epoch != selection_epoch ||
        selection_device != std::optional<DeviceId>(task.device)) {
      cancel_resume_recovery("selection changed");
      return;
    }
    if (Clock::now() >= resume_recovery->deadline) {
      expire_resume_recovery();
      return;
    }
    if (diagnostics) {
      diagnostics(std::string("resume path=DEFERRED_RESUME_RECOVERY state=session_restored resume_epoch=") +
                  std::to_string(resume_recovery->epoch) + " " +
                  context(resume_recovery->lifecycle_epoch, "resume",
                          task.generation));
    }
    const auto refresh = execute_timed_intent(
        PolicyIntent::RefreshDeviceSession, nullptr,
        resume_recovery->lifecycle_epoch, "resume", task.generation);
    resume_recovery->results[resume_recovery->refresh_index] = refresh;
    record_intent_result(PolicyIntent::RefreshDeviceSession, refresh,
                         resume_recovery->lifecycle_epoch, "resume",
                         task.generation, last_operation_duration);
    if (IsRecoverableResumeRefreshResult(refresh)) return;
    if (refresh == OperationResult::Success && diagnostics) {
      diagnostics(std::string("resume continuation generation=") +
                  std::to_string(task.generation) + " resume_epoch=" +
                  std::to_string(resume_recovery->epoch));
    }
    const auto continuation_generation = task.generation;
    while (resume_recovery &&
           resume_recovery->next_step < resume_recovery->plan.steps.size()) {
      const auto index = resume_recovery->next_step++;
      const auto& step = resume_recovery->plan.steps[index];
      if (IsDeviceIntent(step.intent) &&
          (generation.load() != continuation_generation || !session ||
           active_generation != continuation_generation)) {
        cancel_resume_recovery("session generation changed");
        return;
      }
      OperationResult result = OperationResult::InvalidState;
      bool skipped = false;
      if (step.requires_success &&
          (*step.requires_success >= resume_recovery->results.size() ||
           resume_recovery->results[*step.requires_success] !=
               OperationResult::Success)) {
        result = OperationResult::Rejected;
        skipped = true;
        last_operation_duration = std::chrono::milliseconds::zero();
        log_operation_skipped(step.intent, resume_recovery->lifecycle_epoch,
                              "resume", continuation_generation);
      } else {
        result = execute_timed_intent(step.intent, nullptr,
                                      resume_recovery->lifecycle_epoch,
                                      "resume", continuation_generation);
      }
      resume_recovery->results.push_back(result);
      record_intent_result(step.intent, result,
                           resume_recovery->lifecycle_epoch, "resume",
                           continuation_generation, last_operation_duration,
                           !skipped);
      if (IsDeviceIntent(step.intent) &&
          generation.load() != continuation_generation) {
        cancel_resume_recovery("session generation changed");
        return;
      }
    }
    if (resume_recovery && diagnostics) {
      diagnostics(std::string("resume path=DEFERRED_RESUME_RECOVERY state=complete resume_epoch=") +
                  std::to_string(resume_recovery->epoch) + " " +
                  context(resume_recovery->lifecycle_epoch, "resume",
                          continuation_generation));
      diagnostics(std::string("lifecycle transaction complete ") +
                  context(resume_recovery->lifecycle_epoch, "resume",
                          continuation_generation));
    }
    resume_recovery.reset();
  }

  void expire_resume_recovery() {
    if (!resume_recovery || Clock::now() < resume_recovery->deadline) return;
    if (diagnostics) {
      diagnostics(std::string("resume path=RESUME_RECOVERY_EXPIRED state=expired resume_epoch=") +
                  std::to_string(resume_recovery->epoch));
    }
    finish_cancelled_resume();
  }

  void cancel_resume_recovery(std::string_view reason) {
    if (!resume_recovery) return;
    if (diagnostics) {
      diagnostics(std::string("resume recovery cancelled reason=") +
                  std::string(reason) + " resume_epoch=" +
                  std::to_string(resume_recovery->epoch));
    }
    finish_cancelled_resume();
  }

  void finish_cancelled_resume() {
    if (!resume_recovery) return;
    const auto epoch = resume_recovery->lifecycle_epoch;
    const auto operation_generation = generation.load();
    const auto complete = execute_timed_intent(
        PolicyIntent::CompleteHostTransition, nullptr, epoch, "resume",
        operation_generation);
    record_intent_result(PolicyIntent::CompleteHostTransition, complete, epoch,
                         "resume", operation_generation,
                         last_operation_duration);
    if (diagnostics) {
      diagnostics(std::string("lifecycle transaction complete state=cancelled ") +
                  context(epoch, "resume", operation_generation));
    }
    resume_recovery.reset();
  }

  OperationResult execute_intent(PolicyIntent intent, HostTransitionHold* hold) {
    switch (intent) {
      case PolicyIntent::RequestHostSuspend:
        return host.request_system_suspend();
      case PolicyIntent::ResetRemoteInput:
        active_remote_action.reset();
        return host.reset_remote_input();
      case PolicyIntent::PrepareWake: {
        if (!session || !active_device_id) return OperationResult::Disconnected;
        const auto authorization = host.remote_wake_authorization(*active_device_id);
        if (authorization == AuthorizationState::Denied) {
          return OperationResult::PermissionDenied;
        }
        if (authorization != AuthorizationState::Authorized) {
          return OperationResult::InvalidState;
        }
        log("Remote Wake authorization Authorized");
        (void)session->query_wake_state();
        const auto configured = session->configure_wake(active_physical_address);
        if (configured != OperationResult::Success) return configured;
        const auto armed = session->arm_wake();
        if (armed == OperationResult::Success) log("Wake configured and armed");
        return armed;
      }
      case PolicyIntent::DisarmWake:
        return session ? session->disarm_wake() : OperationResult::Disconnected;
      case PolicyIntent::StandbyTv:
        return session ? session->standby_tv() : OperationResult::Disconnected;
      case PolicyIntent::PowerOnTv:
        return session ? session->power_on_tv() : OperationResult::Disconnected;
      case PolicyIntent::MakeActiveSource:
        return session ? session->make_active_source()
                       : OperationResult::Disconnected;
      case PolicyIntent::RefreshDeviceSession:
        return session ? session->refresh_state() : OperationResult::Disconnected;
      case PolicyIntent::ReadWakeReason: {
        if (!session) return OperationResult::Disconnected;
        const auto reason = session->read_last_wake_reason();
        if (reason.result == OperationResult::Success) {
          (void)session->clear_last_wake_reason();
        }
        return reason.result;
      }
      case PolicyIntent::CompleteHostTransition:
        if (hold) (void)hold->complete();
        return OperationResult::Success;
    }
    return OperationResult::InternalError;
  }

  HostPlatform& host;
  DeviceSessionFactory& factory;
  DeviceRegistry registry;
  PolicyStateMachine policy;
  SemanticEventQueue events;
  std::optional<DeviceId> explicit_selection;
  const std::chrono::milliseconds resume_recovery_timeout;
  const bool maintain_session_wake;
  MateRuntimeConfiguration runtime_configuration;
  DiagnosticSink diagnostics;
  std::vector<DiscoveredDevice> inventory;
  bool discovery_snapshot_logged{};
  std::unique_ptr<DeviceSession> session;
  std::unique_ptr<SessionObserver> session_observer;
  std::optional<DeviceId> active_device_id;
  std::optional<DeviceId> selection_device;
  std::optional<SemanticRemoteAction> active_remote_action;
  WakeState last_wake_state;
  std::uint16_t active_physical_address{0xffff};
  PhysicalAddressResolution last_physical_address_resolution;
  std::uint64_t active_generation{};
  std::uint64_t last_closed_generation{};
  std::uint64_t selection_epoch{};
  std::uint64_t resume_epoch{};
  std::uint64_t lifecycle_epoch{};
  std::chrono::milliseconds last_operation_duration{};
  std::optional<ResumeRecovery> resume_recovery;
  std::atomic<std::uint64_t> generation{};
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::condition_variable idle;
  std::deque<Task> tasks;
  bool stopping{};
  bool busy{};
  std::thread worker;
  MateServiceSnapshot frontend_snapshot;
  mutable std::mutex frontend_observer_mutex;
  MateServiceFrontendObserver* frontend_observer{};
  std::atomic<MateManualActionRequestId> next_manual_request{1};
};

MateService::MateService(HostPlatform& host, DeviceSessionFactory& factory,
                         MateServiceConfiguration configuration,
                         DiagnosticSink diagnostics)
    : impl_(std::make_unique<Impl>(host, factory, std::move(configuration),
                                   std::move(diagnostics))) {}

MateService::~MateService() { shutdown(); }

OperationResult MateService::start() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->worker.joinable()) return OperationResult::InvalidState;
  const auto remote = impl_->host.apply_remote_input_configuration(
      impl_->runtime_configuration.remote);
  if (remote != OperationResult::Success &&
      remote != OperationResult::Unsupported) {
    return remote;
  }
  impl_->stopping = false;
  impl_->host.set_lifecycle_observer(this);
  if (impl_->explicit_selection) {
    (void)impl_->registry.set_explicit_selection(impl_->explicit_selection);
  }
  impl_->worker = std::thread(&Impl::run, impl_.get());
  impl_->tasks.emplace_back(Impl::TaskKind::RefreshDiscovery);
  impl_->changed.notify_one();
  return OperationResult::Success;
}

void MateService::shutdown() noexcept {
  if (!impl_) return;
  impl_->host.set_lifecycle_observer(nullptr);
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->worker.joinable()) return;
    impl_->stopping = true;
    for (auto& task : impl_->tasks) {
      if (task.host && task.host->transition_hold) {
        (void)task.host->transition_hold->complete();
      }
    }
    impl_->tasks.clear();
  }
  impl_->changed.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
  impl_->cancel_resume_recovery("service stop");
  impl_->events.clear();
  impl_->drop_session();
  (void)impl_->host.reset_remote_input();
  impl_->log("Mate Service stopped cleanly");
}

void MateService::refresh_discovery() {
  impl_->post(Impl::Task(Impl::TaskKind::RefreshDiscovery));
}

void MateService::trigger_boot() {
  impl_->post(Impl::Task(Impl::TaskKind::Boot));
}

void MateService::wait_idle() {
  std::unique_lock lock(impl_->mutex);
  impl_->idle.wait(lock, [this] { return impl_->tasks.empty() && !impl_->busy; });
}

bool MateService::running() const noexcept {
  std::lock_guard lock(impl_->mutex);
  return impl_->worker.joinable() && !impl_->stopping;
}

bool MateService::has_active_session() const noexcept {
  std::lock_guard lock(impl_->mutex);
  return impl_->session != nullptr;
}

std::optional<DeviceId> MateService::active_device() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->active_device_id;
}

std::uint64_t MateService::session_generation() const noexcept {
  return impl_->generation.load();
}

OperationResult MateService::apply_runtime_configuration(
    const MateRuntimeConfiguration& configuration) {
  auto result = std::make_shared<std::promise<OperationResult>>();
  auto completed = result->get_future();
  Impl::Task task{Impl::TaskKind::Configuration};
  task.configuration = configuration;
  task.configuration_result = std::move(result);
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->worker.joinable() || impl_->stopping) {
      return OperationResult::InvalidState;
    }
  }
  impl_->post(std::move(task));
  return completed.get();
}

MateRuntimeConfiguration MateService::runtime_configuration() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->runtime_configuration;
}

void MateService::set_frontend_observer(MateServiceFrontendObserver* observer) {
  const auto snapshot = frontend_snapshot();
  std::lock_guard lock(impl_->frontend_observer_mutex);
  impl_->frontend_observer = observer;
  if (observer) observer->on_service_snapshot(snapshot);
}

MateServiceSnapshot MateService::frontend_snapshot() const {
  std::lock_guard lock(impl_->mutex);
  auto snapshot = impl_->frontend_snapshot;
  snapshot.running = impl_->worker.joinable() && !impl_->stopping;
  return snapshot;
}

MateManualActionRequestId MateService::request_manual_action(
    MateManualAction action) {
  {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->worker.joinable() || impl_->stopping) return 0;
  }
  const auto request = impl_->next_manual_request.fetch_add(1);
  Impl::Task task{Impl::TaskKind::ManualAction};
  task.manual_action = action;
  task.manual_request = request;
  impl_->post(std::move(task));
  return request;
}

void MateService::on_host_lifecycle_notification(
    HostLifecycleNotification notification) {
  Impl::Task task{Impl::TaskKind::Host};
  task.host.emplace(std::move(notification));
  impl_->post(std::move(task));
}

}  // namespace cec_injector::mate

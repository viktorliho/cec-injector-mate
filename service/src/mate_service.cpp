#include "cec_injector/mate/mate_service.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
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

}  // namespace

struct MateService::Impl {
  enum class TaskKind { RefreshDiscovery, Boot, SessionState, Frame, Wake, Host };
  struct Task {
    explicit Task(TaskKind value) : kind(value) {}
    Task(TaskKind value, std::uint64_t token, DeviceId id)
        : kind(value), generation(token), device(std::move(id)) {}
    TaskKind kind;
    std::uint64_t generation{};
    DeviceId device;
    SessionState session_state{SessionState::Disconnected};
    std::vector<std::uint8_t> frame;
    WakeState wake;
    std::optional<HostLifecycleNotification> host;
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
        diagnostics(std::move(sink)) {}

  void log(std::string_view message) {
    if (diagnostics) diagnostics(message);
  }

  void post(Task task) {
    std::lock_guard lock(mutex);
    if (stopping) {
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
        changed.wait(lock, [this] { return stopping || !tasks.empty(); });
        if (stopping && tasks.empty()) break;
        task = std::move(tasks.front());
        tasks.pop_front();
        busy = true;
      }
      handle(std::move(task));
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
    }
  }

  void reconcile_discovery() {
    auto discovered = host.discovered_devices();
    inventory = discovered;
    std::vector<DeviceId> ids;
    ids.reserve(discovered.size());
    for (const auto& device : discovered) ids.push_back(device.id);
    log("discovery inventory refreshed");
    if (diagnostics) {
      diagnostics(std::string("discovery Device count ") +
                  std::to_string(discovered.size()));
    }
    const auto intents = registry.update_discovered(std::move(ids));
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
    if (resolution.status == PhysicalAddressStatus::Available &&
        resolution.address && *resolution.address != active_physical_address) {
      log("physical address changed; replacing session");
      drop_session();
      establish_session(*selected);
    }
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
      const auto message = std::string("selected Device ") +
                           std::string(id.diagnostic_value());
      diagnostics(message);
    }
    const auto resolution = host.resolve_physical_address();
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
    session_observer = std::make_unique<SessionObserver>(*this, next);
    session = std::move(created);
    session->set_observer(session_observer.get());
    const auto result = session->connect();
    if (result != OperationResult::Success) {
      log("DeviceSession connect failed");
      drop_session();
    } else {
      log("DeviceSession connected");
    }
  }

  void drop_session() {
    generation.fetch_add(1);
    (void)host.reset_remote_input();
    active_remote_action.reset();
    if (session_observer) session_observer->disable();
    if (session) {
      session->set_observer(nullptr);
      (void)session->disconnect();
    }
    session.reset();
    session_observer.reset();
    active_device_id.reset();
    active_physical_address = 0xffff;
    active_generation = 0;
  }

  void handle_session_state(const Task& task) {
    if (!active_device_id || task.device != *active_device_id ||
        task.generation != active_generation) return;
    if (task.session_state == SessionState::Connected) {
      execute_event(LifecycleEvent::SessionConnected, nullptr,
                    task.generation);
    } else if (task.session_state == SessionState::Disconnected) {
      execute_event(LifecycleEvent::SessionLost, nullptr, task.generation);
      const auto desired = registry.snapshot().selected_active;
      drop_session();
      if (desired) establish_session(*desired);
    }
  }

  void handle_frame(const Task& task) {
    if (!session || !active_device_id || task.device != *active_device_id ||
        task.generation != active_generation ||
        generation.load() != task.generation || task.frame.size() < 2) {
      return;
    }
    const auto header = task.frame[0];
    const auto initiator = static_cast<std::uint8_t>(header >> 4);
    const auto destination = static_cast<std::uint8_t>(header & 0x0f);
    const auto opcode = task.frame[1];
    if (initiator == 0 && destination == 0x0f && opcode == 0x36 &&
        task.frame.size() == 2) {
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
    log("host lifecycle notification");
    switch (notification.event) {
      case HostLifecycleEvent::PreparingSuspend:
        execute_event(LifecycleEvent::HostPreparingSuspend,
                      notification.transition_hold
                          ? &*notification.transition_hold
                          : nullptr);
        break;
      case HostLifecycleEvent::Resumed:
        execute_event(LifecycleEvent::HostResumed, nullptr);
        break;
      case HostLifecycleEvent::PreparingShutdown:
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
    const auto plan = policy.handle(*active);
    std::vector<OperationResult> results;
    results.reserve(plan.steps.size());
    for (const auto& step : plan.steps) {
      OperationResult result = OperationResult::InvalidState;
      if (step.requires_success &&
          (*step.requires_success >= results.size() ||
           results[*step.requires_success] != OperationResult::Success)) {
        result = OperationResult::Rejected;
      } else if (IsDeviceIntent(step.intent) &&
                 (generation.load() != plan_generation || !session ||
                  active_generation != plan_generation)) {
        result = OperationResult::Disconnected;
        log("discarded stale Device work");
      } else {
        result = execute_intent(step.intent, hold);
      }
      results.push_back(result);
      policy.record_result(step.intent, result);
      if (diagnostics) {
        const auto message = std::string("policy intent ") +
            std::string(IntentName(step.intent)) + " result " +
            std::to_string(static_cast<int>(result));
        diagnostics(message);
      }
    }
    events.complete_active();
    if (hold && hold->active()) (void)hold->complete();
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
  DiagnosticSink diagnostics;
  std::vector<DiscoveredDevice> inventory;
  std::unique_ptr<DeviceSession> session;
  std::unique_ptr<SessionObserver> session_observer;
  std::optional<DeviceId> active_device_id;
  std::optional<SemanticRemoteAction> active_remote_action;
  WakeState last_wake_state;
  std::uint16_t active_physical_address{0xffff};
  std::uint64_t active_generation{};
  std::atomic<std::uint64_t> generation{};
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::condition_variable idle;
  std::deque<Task> tasks;
  bool stopping{};
  bool busy{};
  std::thread worker;
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

void MateService::on_host_lifecycle_notification(
    HostLifecycleNotification notification) {
  Impl::Task task{Impl::TaskKind::Host};
  task.host.emplace(std::move(notification));
  impl_->post(std::move(task));
}

}  // namespace cec_injector::mate

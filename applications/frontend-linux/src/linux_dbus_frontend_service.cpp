#include "cec_injector/mate/frontend_linux/linux_dbus_frontend_service.h"

#include <grp.h>
#include <systemd/sd-bus.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <utility>
#include <deque>

namespace cec_injector::mate::frontend_linux {
namespace {

int BeginMap(sd_bus_message* message) {
  return sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}");
}
int EndMap(sd_bus_message* message) {
  return sd_bus_message_close_container(message);
}
int AddBool(sd_bus_message* message, const char* key, bool value) {
  int result = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (result < 0) return result;
  if ((result = sd_bus_message_append(message, "s", key)) < 0) return result;
  if ((result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "b")) < 0) return result;
  const int wire = value;
  if ((result = sd_bus_message_append(message, "b", wire)) < 0) return result;
  if ((result = sd_bus_message_close_container(message)) < 0) return result;
  return sd_bus_message_close_container(message);
}
int AddString(sd_bus_message* message, const char* key, std::string_view value) {
  int result = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (result < 0) return result;
  const std::string owned(value);
  if ((result = sd_bus_message_append(message, "s", key)) < 0) return result;
  if ((result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "s")) < 0) return result;
  if ((result = sd_bus_message_append(message, "s", owned.c_str())) < 0) return result;
  if ((result = sd_bus_message_close_container(message)) < 0) return result;
  return sd_bus_message_close_container(message);
}
int AddU64(sd_bus_message* message, const char* key, std::uint64_t value) {
  int result = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (result < 0) return result;
  if ((result = sd_bus_message_append(message, "s", key)) < 0) return result;
  if ((result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "t")) < 0) return result;
  if ((result = sd_bus_message_append(message, "t", value)) < 0) return result;
  if ((result = sd_bus_message_close_container(message)) < 0) return result;
  return sd_bus_message_close_container(message);
}
int AddU32(sd_bus_message* message, const char* key, std::uint32_t value) {
  int result = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (result < 0) return result;
  if ((result = sd_bus_message_append(message, "s", key)) < 0) return result;
  if ((result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "u")) < 0) return result;
  if ((result = sd_bus_message_append(message, "u", value)) < 0) return result;
  if ((result = sd_bus_message_close_container(message)) < 0) return result;
  return sd_bus_message_close_container(message);
}
int AddI64(sd_bus_message* message, const char* key, std::int64_t value) {
  int result = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (result < 0) return result;
  if ((result = sd_bus_message_append(message, "s", key)) < 0) return result;
  if ((result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "x")) < 0) return result;
  if ((result = sd_bus_message_append(message, "x", value)) < 0) return result;
  if ((result = sd_bus_message_close_container(message)) < 0) return result;
  return sd_bus_message_close_container(message);
}
int AddBytes(sd_bus_message* message, const char* key,
             const std::vector<std::uint8_t>& value) {
  int result = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (result < 0) return result;
  if ((result = sd_bus_message_append(message, "s", key)) < 0) return result;
  if ((result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "ay")) < 0) return result;
  if ((result = sd_bus_message_append_array(message, SD_BUS_TYPE_BYTE,
                                             value.data(), value.size())) < 0) return result;
  if ((result = sd_bus_message_close_container(message)) < 0) return result;
  return sd_bus_message_close_container(message);
}

std::string SessionName(SessionState value) {
  switch (value) {
    case SessionState::Discovered: return "discovered";
    case SessionState::Connecting: return "connecting";
    case SessionState::Connected: return "connected";
    case SessionState::Degraded: return "degraded";
    case SessionState::Disconnecting: return "disconnecting";
    case SessionState::Disconnected: return "disconnected";
  }
  return "disconnected";
}

std::string ResultName(OperationResult value) {
  switch (value) {
    case OperationResult::Success: return "success";
    case OperationResult::Unsupported: return "unsupported";
    case OperationResult::InvalidState: return "invalid_state";
    case OperationResult::InvalidConfiguration: return "invalid_configuration";
    case OperationResult::Rejected: return "rejected";
    case OperationResult::Timeout: return "timeout";
    case OperationResult::Disconnected: return "disconnected";
    case OperationResult::MalformedResponse: return "malformed_response";
    case OperationResult::PermissionDenied: return "permission_denied";
    case OperationResult::InternalError: return "internal_error";
  }
  return "internal_error";
}

int AppendConfiguration(sd_bus_message* message,
                        const VersionedRuntimeConfiguration& value) {
  int result = BeginMap(message);
  if (result < 0) return result;
  const auto& p = value.configuration.policy;
  if ((result = AddU64(message, "revision", value.revision)) < 0 ||
      (result = AddU32(message, "schema_version", 1)) < 0 ||
      (result = AddBool(message, "power_on_boot", p.power_on_boot)) < 0 ||
      (result = AddBool(message, "power_on_resume", p.power_on_resume)) < 0 ||
      (result = AddBool(message, "standby_on_suspend", p.standby_on_suspend)) < 0 ||
      (result = AddBool(message, "standby_on_shutdown", p.standby_on_shutdown)) < 0 ||
      (result = AddBool(message, "suspend_on_tv_standby", p.suspend_on_tv_standby)) < 0 ||
      (result = AddBool(message, "wake_host_on_tv_source_request", p.wake_host_on_tv_source_request)) < 0 ||
      (result = AddBool(message, "remote_input_enabled", value.configuration.remote.enabled)) < 0 ||
      (result = AddString(message, "remote_profile", remote_input_profile_name(value.configuration.remote.profile))) < 0) {
    return result;
  }
  return EndMap(message);
}

int AppendStatus(sd_bus_message* message,
                 const frontend::FrontendStatus& value) {
  int result = BeginMap(message);
  if (result < 0) return result;
  if ((result = AddU32(message, "schema_version", 1)) < 0 ||
      (result = AddBool(message, "service_available", value.service_available)) < 0 ||
      (result = AddBool(message, "injector_connected", value.injector_connected)) < 0 ||
      (result = AddString(message, "device_id", value.selected_device ? value.selected_device->diagnostic_value() : "")) < 0 ||
      (result = AddString(message, "device_name", value.device_display_name)) < 0 ||
      (result = AddString(message, "display_name", value.display_name)) < 0 ||
      (result = AddString(message, "session_state", SessionName(value.session_state))) < 0 ||
      (result = AddBool(message, "physical_address_available", value.physical_address.has_value())) < 0 ||
      (result = AddU32(message, "physical_address", value.physical_address.value_or(0xffff))) < 0 ||
      (result = AddBool(message, "logical_address_available", value.logical_address.has_value())) < 0 ||
      (result = AddU32(message, "logical_address", value.logical_address.value_or(0xff))) < 0 ||
      (result = AddBool(message, "wake_supported", value.wake.supported)) < 0 ||
      (result = AddBool(message, "wake_configured", value.wake.configured)) < 0 ||
      (result = AddBool(message, "wake_armed", value.wake.armed)) < 0 ||
      (result = AddBool(message, "wake_reason_available", value.wake.reason_available)) < 0 ||
      (result = AddBool(message, "edid_available", value.edid_available)) < 0 ||
      (result = AddU64(message, "edid_byte_count", value.edid_byte_count)) < 0) return result;
  return EndMap(message);
}

bool ReadConfiguration(sd_bus_message* message,
                       MateRuntimeConfiguration& configuration,
                       std::string& error) {
  configuration = production_runtime_defaults();
  bool seen[8]{};
  int result = sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}");
  if (result < 0) return error = "configuration map expected", false;
  while ((result = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
    const char* key = nullptr;
    if (sd_bus_message_read(message, "s", &key) < 0) return false;
    const char* signature = nullptr;
    if (sd_bus_message_peek_type(message, nullptr, &signature) < 0 ||
        sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, signature) < 0) return false;
    auto read_bool = [&](bool& target, int index) {
      int wire = 0;
      if (std::strcmp(signature, "b") != 0 || sd_bus_message_read(message, "b", &wire) < 0) return false;
      target = wire;
      seen[index] = true;
      return true;
    };
    bool valid = true;
    const std::string_view name(key);
    if (name == "power_on_boot") valid = read_bool(configuration.policy.power_on_boot, 0);
    else if (name == "power_on_resume") valid = read_bool(configuration.policy.power_on_resume, 1);
    else if (name == "standby_on_suspend") valid = read_bool(configuration.policy.standby_on_suspend, 2);
    else if (name == "standby_on_shutdown") valid = read_bool(configuration.policy.standby_on_shutdown, 3);
    else if (name == "suspend_on_tv_standby") valid = read_bool(configuration.policy.suspend_on_tv_standby, 4);
    else if (name == "wake_host_on_tv_source_request") valid = read_bool(configuration.policy.wake_host_on_tv_source_request, 5);
    else if (name == "remote_input_enabled") valid = read_bool(configuration.remote.enabled, 6);
    else if (name == "remote_profile") {
      const char* profile = nullptr;
      valid = std::strcmp(signature, "s") == 0 &&
              sd_bus_message_read(message, "s", &profile) >= 0 &&
              parse_remote_input_profile(profile, configuration.remote.profile);
      seen[7] = valid;
    } else {
      valid = sd_bus_message_skip(message, signature) >= 0;
    }
    if (!valid || sd_bus_message_exit_container(message) < 0 ||
        sd_bus_message_exit_container(message) < 0) {
      error = "malformed configuration field: " + std::string(name);
      return false;
    }
  }
  if (result < 0 || sd_bus_message_exit_container(message) < 0) return false;
  for (bool field : seen) if (!field) return error = "incomplete configuration", false;
  return true;
}

}  // namespace

struct LinuxDbusFrontendService::Impl {
  Impl(LinuxDbusFrontendService& owner_value, MateService& service_value,
       RuntimeConfigurationController& configuration_value, bool allow,
       std::string address)
      : owner(owner_value), service(service_value), configuration(configuration_value),
        test_allow_mutation(allow), bus_address(std::move(address)) {}

  static int GetStatus(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto& self = *static_cast<Impl*>(userdata);
    sd_bus_message* reply = nullptr;
    int result = sd_bus_message_new_method_return(message, &reply);
    if (result >= 0) result = AppendStatus(reply, frontend::FrontendStateProjector::status(self.service.frontend_snapshot()));
    if (result >= 0) result = sd_bus_send(self.bus, reply, nullptr);
    sd_bus_message_unref(reply);
    return result;
  }
  static int GetCapabilities(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto& self = *static_cast<Impl*>(userdata);
    sd_bus_message* reply = nullptr;
    int result = sd_bus_message_new_method_return(message, &reply);
    if (result >= 0) result = BeginMap(reply);
    for (const auto* key : {"configuration", "remote_input", "remote_wake", "tv_power_actions", "cec_monitor"})
      if (result >= 0) result = AddBool(reply, key, true);
    if (result >= 0) result = EndMap(reply);
    if (result >= 0) result = sd_bus_send(self.bus, reply, nullptr);
    sd_bus_message_unref(reply);
    return result;
  }
  static int GetConfiguration(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto& self = *static_cast<Impl*>(userdata);
    sd_bus_message* reply = nullptr;
    int result = sd_bus_message_new_method_return(message, &reply);
    if (result >= 0) result = AppendConfiguration(reply, self.configuration.current());
    if (result >= 0) result = sd_bus_send(self.bus, reply, nullptr);
    sd_bus_message_unref(reply);
    return result;
  }
  bool authorized(sd_bus_message* message) const {
    if (test_allow_mutation) return true;
    sd_bus_creds* creds = nullptr;
    if (sd_bus_query_sender_creds(message, SD_BUS_CREDS_EUID | SD_BUS_CREDS_SUPPLEMENTARY_GIDS, &creds) < 0) return false;
    uid_t uid = static_cast<uid_t>(-1);
    bool allowed = sd_bus_creds_get_euid(creds, &uid) >= 0 && uid == 0;
    if (!allowed) {
      if (const auto* wheel = getgrnam("wheel")) {
        const gid_t* groups = nullptr;
        const int count = sd_bus_creds_get_supplementary_gids(creds, &groups);
        for (int index = 0; index < count; ++index) allowed |= groups[index] == wheel->gr_gid;
      }
    }
    sd_bus_creds_unref(creds);
    return allowed;
  }
  static int SetConfiguration(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto& self = *static_cast<Impl*>(userdata);
    if (!self.authorized(message)) return sd_bus_error_set_const(error, SD_BUS_ERROR_ACCESS_DENIED, "mutation requires root or wheel membership");
    std::uint64_t revision = 0;
    if (sd_bus_message_read(message, "t", &revision) < 0) return sd_bus_error_set_const(error, SD_BUS_ERROR_INVALID_ARGS, "base revision expected");
    MateRuntimeConfiguration requested;
    std::string parse_error;
    if (!ReadConfiguration(message, requested, parse_error)) {
      return sd_bus_error_set(error, SD_BUS_ERROR_INVALID_ARGS,
                              parse_error.c_str());
    }
    const auto outcome = self.configuration.set(revision, requested);
    return sd_bus_reply_method_return(message, "sts", ResultName(outcome.result).c_str(), outcome.authoritative.revision, outcome.message.c_str());
  }
  static int RequestAction(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto& self = *static_cast<Impl*>(userdata);
    if (!self.authorized(message)) return sd_bus_error_set_const(error, SD_BUS_ERROR_ACCESS_DENIED, "mutation requires root or wheel membership");
    const char* name = nullptr;
    if (sd_bus_message_read(message, "s", &name) < 0) return -EINVAL;
    MateManualAction action;
    if (std::strcmp(name, "power_on_tv") == 0) action = MateManualAction::PowerOnTv;
    else if (std::strcmp(name, "make_active_source") == 0) action = MateManualAction::MakeActiveSource;
    else if (std::strcmp(name, "standby_tv") == 0) action = MateManualAction::StandbyTv;
    else return sd_bus_error_set_const(error, SD_BUS_ERROR_INVALID_ARGS, "unknown action");
    const auto request = self.service.request_manual_action(action);
    if (request == 0) return sd_bus_error_set_const(error, SD_BUS_ERROR_FAILED, "service unavailable");
    return sd_bus_reply_method_return(message, "x", static_cast<std::int64_t>(request));
  }

  static constexpr sd_bus_vtable kVtable[] = {
      SD_BUS_VTABLE_START(0),
      SD_BUS_METHOD("GetStatus", "", "a{sv}", GetStatus, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("GetCapabilities", "", "a{sv}", GetCapabilities, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("GetConfiguration", "", "a{sv}", GetConfiguration, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("SetConfiguration", "ta{sv}", "sts", SetConfiguration, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("RequestAction", "s", "x", RequestAction, SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_SIGNAL("StatusChanged", "a{sv}", 0),
      SD_BUS_SIGNAL("ConfigurationChanged", "a{sv}", 0),
      SD_BUS_SIGNAL("ActionCompleted", "a{sv}", 0),
      SD_BUS_SIGNAL("CecEvent", "a{sv}", 0),
      SD_BUS_VTABLE_END};

  int emit_map_signal(const char* member,
                      const std::function<int(sd_bus_message*)>& append) {
    if (!bus) return -ENOTCONN;
    sd_bus_message* signal = nullptr;
    int result = sd_bus_message_new_signal(bus, &signal, kMateObjectPath, kMateInterface, member);
    if (result >= 0) result = append(signal);
    if (result >= 0) result = sd_bus_send(bus, signal, nullptr);
    sd_bus_message_unref(signal);
    return result;
  }

  void enqueue(std::function<void()> operation) {
    {
      std::lock_guard lock(outbound_mutex);
      outbound.push_back(std::move(operation));
    }
    const std::uint64_t wake = 1;
    if (wake_fd >= 0) (void)::write(wake_fd, &wake, sizeof(wake));
  }

  void drain_outbound() {
    std::deque<std::function<void()>> pending;
    {
      std::lock_guard lock(outbound_mutex);
      pending.swap(outbound);
    }
    for (auto& operation : pending) operation();
  }

  LinuxDbusFrontendService& owner;
  MateService& service;
  RuntimeConfigurationController& configuration;
  bool test_allow_mutation{};
  std::string bus_address;
  sd_bus* bus{};
  sd_bus_slot* slot{};
  std::mutex outbound_mutex;
  std::deque<std::function<void()>> outbound;
  int wake_fd{-1};
  std::atomic<bool> stopping{};
  std::thread worker;
};

constexpr sd_bus_vtable LinuxDbusFrontendService::Impl::kVtable[];

LinuxDbusFrontendService::LinuxDbusFrontendService(
    MateService& service, RuntimeConfigurationController& configuration,
    bool test_allow_mutation, std::string bus_address)
    : impl_(std::make_unique<Impl>(*this, service, configuration,
                                  test_allow_mutation,
                                  std::move(bus_address))) {}
LinuxDbusFrontendService::~LinuxDbusFrontendService() { stop(); }

OperationResult LinuxDbusFrontendService::start() {
  int result = 0;
  if (impl_->bus_address.empty()) result = sd_bus_open_system(&impl_->bus);
  else {
    result = sd_bus_new(&impl_->bus);
    if (result >= 0) result = sd_bus_set_address(impl_->bus, impl_->bus_address.c_str());
    if (result >= 0) result = sd_bus_set_bus_client(impl_->bus, true);
    if (result >= 0) result = sd_bus_start(impl_->bus);
  }
  if (result < 0) return OperationResult::Disconnected;
  impl_->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (impl_->wake_fd < 0) { stop(); return OperationResult::InternalError; }
  result = sd_bus_add_object_vtable(impl_->bus, &impl_->slot, kMateObjectPath,
                                    kMateInterface, Impl::kVtable, impl_.get());
  if (result >= 0) result = sd_bus_request_name(impl_->bus, kMateBusName, 0);
  if (result < 0) { stop(); return OperationResult::InternalError; }
  impl_->service.set_frontend_observer(this);
  impl_->configuration.set_changed_sink(
      [this](const VersionedRuntimeConfiguration& value) {
        configuration_changed(value);
      });
  impl_->stopping = false;
  impl_->worker = std::thread([this] {
    while (!impl_->stopping.load()) {
      impl_->drain_outbound();
      while (sd_bus_process(impl_->bus, nullptr) > 0) {}
      pollfd descriptors[2]{{sd_bus_get_fd(impl_->bus),
                             static_cast<short>(sd_bus_get_events(impl_->bus)), 0},
                            {impl_->wake_fd, POLLIN, 0}};
      if (::poll(descriptors, 2, -1) < 0 && errno != EINTR) break;
      if (descriptors[1].revents & POLLIN) {
        std::uint64_t wake = 0;
        while (::read(impl_->wake_fd, &wake, sizeof(wake)) > 0) {}
      }
    }
    impl_->drain_outbound();
  });
  return OperationResult::Success;
}

void LinuxDbusFrontendService::stop() noexcept {
  if (!impl_) return;
  impl_->service.set_frontend_observer(nullptr);
  impl_->configuration.set_changed_sink({});
  impl_->stopping = true;
  const std::uint64_t wake = 1;
  if (impl_->wake_fd >= 0) (void)::write(impl_->wake_fd, &wake, sizeof(wake));
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->wake_fd >= 0) { ::close(impl_->wake_fd); impl_->wake_fd = -1; }
  if (impl_->bus) sd_bus_close(impl_->bus);
  impl_->slot = sd_bus_slot_unref(impl_->slot);
  impl_->bus = sd_bus_unref(impl_->bus);
}

void LinuxDbusFrontendService::on_service_snapshot(
    const MateServiceSnapshot& snapshot) {
  const auto projected = frontend::FrontendStateProjector::status(snapshot);
  impl_->enqueue([this, projected] {
    (void)impl_->emit_map_signal("StatusChanged", [&](sd_bus_message* message) {
      return AppendStatus(message, projected);
    });
  });
}

void LinuxDbusFrontendService::configuration_changed(
    const VersionedRuntimeConfiguration& value) {
  impl_->enqueue([this, value] {
    (void)impl_->emit_map_signal("ConfigurationChanged", [&](sd_bus_message* message) {
      return AppendConfiguration(message, value);
    });
  });
}

void LinuxDbusFrontendService::on_service_cec_event(
    const MateServiceCecEvent& event) {
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
      event.timestamp.time_since_epoch()).count();
  impl_->enqueue([this, event, micros] {
   (void)impl_->emit_map_signal("CecEvent", [&](sd_bus_message* message) {
    int result = BeginMap(message);
    if (result >= 0) result = AddU32(message, "schema_version", 1);
    if (result >= 0) result = AddI64(message, "timestamp_us", micros);
    if (result >= 0) result = AddString(message, "direction", event.received ? "rx" : "tx");
    if (result >= 0) result = AddU32(message, "source", event.source);
    if (result >= 0) result = AddU32(message, "destination", event.destination);
    if (result >= 0) result = AddU32(message, "opcode", event.opcode);
    if (result >= 0) result = AddBytes(message, "payload", event.parameters);
    if (result >= 0) result = AddString(message, "semantic", event.semantic.value_or(""));
    return result >= 0 ? EndMap(message) : result;
   });
  });
}

void LinuxDbusFrontendService::on_manual_action_completed(
    MateManualActionRequestId request, const MateManualActionResult& value) {
  const auto action = value.action == MateManualAction::PowerOnTv ? "power_on_tv" :
      value.action == MateManualAction::MakeActiveSource ? "make_active_source" : "standby_tv";
  impl_->enqueue([this, request, value, action = std::string(action)] {
   (void)impl_->emit_map_signal("ActionCompleted", [&](sd_bus_message* message) {
    int result = BeginMap(message);
    if (result >= 0) result = AddU32(message, "schema_version", 1);
    if (result >= 0) result = AddI64(message, "request_id", static_cast<std::int64_t>(request));
    if (result >= 0) result = AddString(message, "action", action);
    if (result >= 0) result = AddString(message, "result", ResultName(value.result));
    if (result >= 0) result = AddI64(message, "duration_ms", value.duration.count());
    if (result >= 0) result = AddString(message, "message", value.message);
    return result >= 0 ? EndMap(message) : result;
   });
  });
}


}  // namespace cec_injector::mate::frontend_linux

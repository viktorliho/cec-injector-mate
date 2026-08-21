#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/edid/physical_address.h"
#include "cec_injector/mate/macos/mac_device_discovery.h"
#include "cec_injector/mate/macos/mac_device_inventory.h"
#include "cec_injector/mate/macos/mac_device_session_factory.h"
#include "cec_injector/mate/macos/mac_physical_address.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::macos_platform;

namespace {

std::atomic<bool> g_stop{};

const char* operation_result(OperationResult value) {
  switch (value) {
    case OperationResult::Success:
      return "Success";
    case OperationResult::Unsupported:
      return "Unsupported";
    case OperationResult::InvalidState:
      return "InvalidState";
    case OperationResult::InvalidConfiguration:
      return "InvalidConfiguration";
    case OperationResult::Rejected:
      return "Rejected";
    case OperationResult::Timeout:
      return "Timeout";
    case OperationResult::Disconnected:
      return "Disconnected";
    case OperationResult::MalformedResponse:
      return "MalformedResponse";
    case OperationResult::PermissionDenied:
      return "PermissionDenied";
    case OperationResult::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

const char* status_name(PhysicalAddressStatus status) {
  switch (status) {
    case PhysicalAddressStatus::Available:
      return "Available";
    case PhysicalAddressStatus::Unavailable:
      return "Unavailable";
    case PhysicalAddressStatus::Ambiguous:
      return "Ambiguous";
  }
  return "Unavailable";
}

std::string format_pa(std::uint16_t value) {
  std::ostringstream out;
  out << std::hex << std::nouppercase
      << ((value >> 12) & 0xf) << '.'
      << ((value >> 8) & 0xf) << '.'
      << ((value >> 4) & 0xf) << '.'
      << (value & 0xf);
  return out.str();
}

void print_usage() {
  std::cout
      << "usage: cec-injector-mate-macos-lab <discover|pa|connect|monitor|power-on|active-source|standby|wake-state>\n";
}

struct Environment {
  MacDeviceDiscovery discovery;
  std::shared_ptr<MacDeviceInventory> inventory{std::make_shared<MacDeviceInventory>()};
  MacPhysicalAddressResolver physical_address;
  MacDeviceSessionFactory factory{inventory};
  std::vector<MacDeviceCandidate> discovery_candidates;
  MacDiscoverySnapshot discovery_snapshot;
  std::vector<MacDisplayCandidate> display_candidates;
  MacPhysicalAddressObservation pa_observation;
};

Environment enumerate_environment() {
  Environment env;
  env.discovery_candidates = env.discovery.enumerate_candidates();
  env.discovery_snapshot = make_discovery_snapshot(env.discovery_candidates);
  env.inventory->replace(env.discovery_snapshot, env.discovery_candidates);
  env.display_candidates = env.physical_address.enumerate_candidates();
  env.pa_observation = observe_physical_address(env.display_candidates);
  return env;
}

std::optional<DiscoveredDevice> require_single_device(
    const MacDiscoverySnapshot& snapshot) {
  if (snapshot.status != DiscoverySnapshotStatus::Complete ||
      snapshot.devices.size() != 1) {
    return std::nullopt;
  }
  return snapshot.devices.front();
}

void print_discovery(const Environment& env) {
  std::cout << "raw_candidates=" << env.discovery_candidates.size() << '\n';
  std::cout << "devices=" << env.discovery_snapshot.devices.size() << '\n';
  for (const auto& device : env.discovery_snapshot.devices) {
    const auto entry = env.inventory->resolve(device);
    std::cout << "DeviceId=" << device.id.diagnostic_value() << '\n';
    std::cout << "locator=opaque\n";
    std::cout << "endpoint_kind="
              << (entry && entry->endpoint_kind == MacEndpointKind::Callout
                      ? "callout"
                      : "unknown")
              << '\n';
    std::cout << "endpoint_present="
              << (entry && !entry->explicit_port.empty() ? "yes" : "no") << '\n';
  }
}

void print_pa(const Environment& env) {
  std::cout << "displays=" << env.display_candidates.size() << '\n';
  std::cout << "external_candidates="
            << env.pa_observation.plausible_external_candidates << '\n';
  std::cout << "pa_status=" << status_name(env.pa_observation.resolution.status)
            << '\n';
  for (const auto& candidate : env.display_candidates) {
    std::cout << "display registry=0x" << std::hex << candidate.registry_entry_id
              << std::dec
              << " active=" << (candidate.active ? "yes" : "no")
              << " external="
              << (candidate.plausible_external ? "yes" : "no")
              << " edid_bytes=" << (candidate.edid ? candidate.edid->size() : 0)
              << " product=" << candidate.product_name
              << " transport=" << candidate.transport_description;
    if (candidate.edid) {
      const auto parsed = edid::parse_edid_physical_address(*candidate.edid);
      std::cout << " parsed_status=";
      switch (parsed.status) {
        case edid::EdidPhysicalAddressStatus::Invalid:
          std::cout << "Invalid";
          break;
        case edid::EdidPhysicalAddressStatus::NoPhysicalAddress:
          std::cout << "NoPhysicalAddress";
          break;
        case edid::EdidPhysicalAddressStatus::PhysicalAddress:
          std::cout << "PhysicalAddress";
          break;
      }
      if (parsed.address) {
        std::cout << " pa=" << format_pa(*parsed.address)
                  << " pa_raw=0x" << std::hex << std::setw(4)
                  << std::setfill('0') << *parsed.address << std::dec;
      }
    }
    std::cout << '\n';
  }
  if (env.pa_observation.resolution.address) {
    std::cout << "pa=" << format_pa(*env.pa_observation.resolution.address) << '\n';
    std::cout << "pa_raw=0x" << std::hex << std::setw(4) << std::setfill('0')
              << *env.pa_observation.resolution.address << std::dec << '\n';
  }
}

class MonitorObserver final : public DeviceSessionObserver {
 public:
  void on_session_state_changed(const DeviceId&, SessionState state) override {
    std::lock_guard lock(mu_);
    std::cout << "session_state=" << static_cast<int>(state) << '\n';
  }

  void on_external_cec_frame(const DeviceId&,
                             std::span<const std::uint8_t> frame) override {
    std::lock_guard lock(mu_);
    std::cout << "rx bytes=";
    for (std::size_t i = 0; i < frame.size(); ++i) {
      if (i) std::cout << ' ';
      std::cout << std::hex << std::uppercase
                << static_cast<unsigned>(frame[i]) << std::dec;
    }
    std::cout << '\n';
  }

  void on_wake_state_changed(const DeviceId&, const WakeState&) override {}

 private:
  std::mutex mu_;
};

int run_connect_like(std::string_view command, bool monitor_mode = false,
                     int monitor_seconds = 10) {
  auto env = enumerate_environment();
  print_discovery(env);
  print_pa(env);

  const auto selected = require_single_device(env.discovery_snapshot);
  if (!selected) {
    std::cerr << "expected exactly one Injector device\n";
    return 1;
  }
  if (env.pa_observation.resolution.status != PhysicalAddressStatus::Available ||
      !env.pa_observation.resolution.address) {
    std::cerr << "physical address is not uniquely available\n";
    return 1;
  }

  const auto pa = *env.pa_observation.resolution.address;
  auto session =
      env.factory.create_session(*selected, DeviceSessionConfiguration{pa});
  if (!session) {
    std::cerr << "failed to create production session from current inventory and PA\n";
    return 1;
  }

  const auto connect_started = std::chrono::steady_clock::now();
  const auto connect_result = session->connect();
  const auto connect_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - connect_started);
  std::cout << "host_pa=known\n";
  std::cout << "connect_result=" << operation_result(connect_result) << '\n';
  std::cout << "connect_elapsed_ms=" << connect_elapsed.count() << '\n';
  if (connect_result != OperationResult::Success) return 1;

  const auto snapshot = session->snapshot();
  std::cout << "logical_address="
            << (snapshot.addresses.logical
                    ? std::to_string(static_cast<unsigned>(*snapshot.addresses.logical))
                    : std::string("unknown"))
            << '\n';

  if (command == "connect") {
    std::cout << "refresh_state_result="
              << operation_result(session->refresh_state()) << '\n';
    const auto wake = session->query_wake_state();
    std::cout << "wake_supported=" << (wake.supported ? "yes" : "no") << '\n';
    std::cout << "wake_configured=" << (wake.configured ? "yes" : "no") << '\n';
    std::cout << "wake_armed=" << (wake.armed ? "yes" : "no") << '\n';
    std::cout << "wake_reason_available=" << (wake.reason_available ? "yes" : "no")
              << '\n';
    std::cout << "disconnect_result=" << operation_result(session->disconnect())
              << '\n';
    return 0;
  }

  if (command == "power-on") {
    const auto started = std::chrono::steady_clock::now();
    const auto result = session->power_on_tv();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "power_on_result=" << operation_result(result) << '\n';
    std::cout << "power_on_elapsed_ms=" << elapsed.count() << '\n';
  } else if (command == "active-source") {
    const auto started = std::chrono::steady_clock::now();
    const auto result = session->make_active_source();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "active_source_result=" << operation_result(result) << '\n';
    std::cout << "active_source_elapsed_ms=" << elapsed.count() << '\n';
  } else if (command == "standby") {
    const auto started = std::chrono::steady_clock::now();
    const auto result = session->standby_tv();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "standby_result=" << operation_result(result) << '\n';
    std::cout << "standby_elapsed_ms=" << elapsed.count() << '\n';
  } else if (command == "wake-state") {
    const auto wake = session->query_wake_state();
    std::cout << "wake_supported=" << (wake.supported ? "yes" : "no") << '\n';
    std::cout << "wake_configured=" << (wake.configured ? "yes" : "no") << '\n';
    std::cout << "wake_armed=" << (wake.armed ? "yes" : "no") << '\n';
    std::cout << "wake_reason_available=" << (wake.reason_available ? "yes" : "no")
              << '\n';
  } else if (command == "monitor" && monitor_mode) {
    MonitorObserver observer;
    session->set_observer(&observer);
    std::signal(SIGINT, [](int) { g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop = true; });
    std::cout << "monitor_seconds=" << monitor_seconds << '\n';
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(monitor_seconds);
    while (!g_stop && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    session->set_observer(nullptr);
  }

  std::cout << "disconnect_result=" << operation_result(session->disconnect()) << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }
  const std::string command = argv[1];
  if (command == "discover") {
    print_discovery(enumerate_environment());
    return 0;
  }
  if (command == "pa") {
    print_pa(enumerate_environment());
    return 0;
  }
  if (command == "connect" || command == "power-on" ||
      command == "active-source" || command == "standby" ||
      command == "wake-state") {
    return run_connect_like(command);
  }
  if (command == "monitor") {
    const int seconds = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 10;
    return run_connect_like(command, true, seconds);
  }
  print_usage();
  return 1;
}

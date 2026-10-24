#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "cec_injector/mate/linux/linux_device_session_factory.h"
#include "cec_injector/mate/linux/linux_host_platform.h"
#include "cec_injector/mate/mate_service.h"
#include "cec_injector/mate/runtime_configuration.h"
#include "cec_injector/mate/runtime_configuration_io.h"
#include "cec_injector/mate/runtime_configuration_controller.h"
#include "cec_injector/mate/frontend_linux/linux_dbus_frontend_service.h"

namespace {

std::atomic<bool> stopping{};

extern "C" void Stop(int) { stopping = true; }

bool SameFileContents(const std::filesystem::path& first,
                      const std::filesystem::path& second) {
  std::ifstream left(first, std::ios::binary);
  std::ifstream right(second, std::ios::binary);
  if (!left || !right) return false;
  const std::string left_contents(std::istreambuf_iterator<char>{left}, {});
  const std::string right_contents(std::istreambuf_iterator<char>{right}, {});
  return !left.bad() && !right.bad() && left_contents == right_contents;
}

void Usage() {
  std::cerr
      << "usage: cec-injector-mate --pa-cache PATH [options]\n"
      << "  --uinput PATH\n  --device-id ID\n"
      << "  --remote-profile steamos-parity\n"
      << "  --runtime-config PATH\n"
      << "  --boot-once [--startup-timeout SECONDS]\n"
      << "  --power-on-boot --power-on-resume --standby-on-suspend\n"
      << "  --standby-on-shutdown --suspend-on-tv-standby\n"
      << "  --wake-host-on-tv-source-request\n";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace cec_injector::mate;
  using namespace cec_injector::mate::linux_platform;
  LinuxHostPlatformConfiguration host_configuration;
  MateServiceConfiguration service_configuration;
  bool boot_once = false;
  std::optional<std::filesystem::path> runtime_config_path;
  auto authoritative_configuration = MateRuntimeConfiguration{};
  std::uint64_t authoritative_revision = 1;
  std::chrono::seconds startup_timeout{30};
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    const auto value = [&]() -> const char* {
      return index + 1 < argc ? argv[++index] : nullptr;
    };
    if (option == "--pa-cache") {
      const auto* path = value();
      if (!path) return Usage(), 2;
      host_configuration.physical_address_cache = path;
    } else if (option == "--uinput") {
      const auto* path = value();
      if (!path) return Usage(), 2;
      host_configuration.uinput_endpoint = path;
    } else if (option == "--device-id") {
      const auto* id = value();
      if (!id) return Usage(), 2;
      service_configuration.selected_device = DeviceId(id);
    } else if (option == "--remote-profile") {
      const auto* profile = value();
      if (!profile || std::string_view(profile) != "steamos-parity") {
        return Usage(), 2;
      }
      host_configuration.input_profile = steamos_parity_remote_input_profile();
    } else if (option == "--runtime-config") {
      const auto* path = value();
      if (!path) return Usage(), 2;
      runtime_config_path = path;
    } else if (option == "--boot-once") {
      boot_once = true;
    } else if (option == "--startup-timeout") {
      const auto* seconds = value();
      if (!seconds) return Usage(), 2;
      try {
        std::size_t consumed = 0;
        const auto parsed = std::stoul(seconds, &consumed);
        if (consumed != std::string_view(seconds).size() || parsed == 0 ||
            parsed > 300) {
          return Usage(), 2;
        }
        startup_timeout = std::chrono::seconds(parsed);
      } catch (...) {
        return Usage(), 2;
      }
    } else if (option == "--power-on-boot") {
      service_configuration.policy.power_on_boot = true;
    } else if (option == "--power-on-resume") {
      service_configuration.policy.power_on_resume = true;
    } else if (option == "--standby-on-suspend") {
      service_configuration.policy.standby_on_suspend = true;
    } else if (option == "--standby-on-shutdown") {
      service_configuration.policy.standby_on_shutdown = true;
    } else if (option == "--suspend-on-tv-standby") {
      service_configuration.policy.suspend_on_tv_standby = true;
    } else if (option == "--wake-host-on-tv-source-request") {
      service_configuration.policy.wake_host_on_tv_source_request = true;
    } else if (option == "--help") {
      Usage();
      return 0;
    } else {
      Usage();
      return 2;
    }
  }
  if (host_configuration.physical_address_cache.empty()) {
    Usage();
    return 2;
  }
  if (runtime_config_path) {
    const auto loaded = load_runtime_configuration(*runtime_config_path);
    if (!loaded.ok()) {
      std::cerr << "cec-injector-mate: invalid runtime configuration: "
                << loaded.error << '\n';
      return 2;
    }
    if (!loaded.existed) {
      std::cerr << "cec-injector-mate: runtime configuration is missing; "
                   "installation repair is required\n";
      return 2;
    }
    if (*runtime_config_path ==
        std::filesystem::path{"/etc/cec-injector-mate/runtime/config.conf"}) {
      const auto backup_path = std::filesystem::path{
          "/var/lib/cec-injector-mate/runtime-config.backup"};
      if (std::filesystem::exists(backup_path)) {
        const auto backup = load_runtime_configuration(backup_path);
        if (!backup.ok() || backup.revision > loaded.revision ||
            (backup.revision == loaded.revision &&
             !SameFileContents(*runtime_config_path, backup_path))) {
          std::cerr << "cec-injector-mate: runtime configuration backup "
                       "conflicts with /etc; installation repair is required\n";
          return 2;
        }
      }
    }
    service_configuration.policy = loaded.configuration.policy;
    service_configuration.remote = loaded.configuration.remote;
    authoritative_configuration = loaded.configuration;
    authoritative_revision = loaded.revision;
  } else {
    authoritative_configuration = {service_configuration.policy,
                                   service_configuration.remote};
  }
  host_configuration.input_profile = steamos_parity_remote_input_profile();

  if (boot_once) {
    host_configuration.profile = LinuxHostPlatformProfile::Boot;
    service_configuration.maintain_session_wake = false;
  }

  try {
    LinuxHostPlatform host(std::move(host_configuration));
    const auto diagnostics = [](std::string_view message) {
      std::cerr << "cec-injector-mate: " << message << '\n';
    };
    LinuxDeviceSessionFactory factory(
        host.inventory(), std::chrono::milliseconds(2500), diagnostics);
    MateService service(host, factory, std::move(service_configuration),
                        diagnostics);
    if (service.start() != OperationResult::Success) return 1;
    if (boot_once) {
      const auto deadline = std::chrono::steady_clock::now() + startup_timeout;
      const auto started = std::chrono::steady_clock::now();
      bool waiting_logged = false;
      using namespace std::chrono_literals;
      while (std::chrono::steady_clock::now() < deadline) {
        service.wait_idle();
        if (service.has_active_session()) break;
        if (!waiting_logged) {
          std::cerr << "cec-injector-mate: readiness profile=boot waiting "
                       "resource=session\n";
          waiting_logged = true;
        }
        std::this_thread::sleep_for(500ms);
        service.refresh_discovery();
      }
      service.wait_idle();
      if (!service.has_active_session()) {
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        std::cerr << "cec-injector-mate: readiness timeout profile=boot "
                     "resource=session duration_ms=" << duration.count() << '\n';
        service.shutdown();
        return 1;
      }
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      std::cerr << "cec-injector-mate: readiness profile=boot ready duration_ms="
                << duration.count() << '\n';
      service.trigger_boot();
      service.wait_idle();
      service.shutdown();
      return 0;
    }
    if (!runtime_config_path) {
      std::cerr << "cec-injector-mate: main daemon requires --runtime-config\n";
      service.shutdown();
      return 2;
    }
    const auto backup_path = *runtime_config_path ==
        std::filesystem::path{"/etc/cec-injector-mate/runtime/config.conf"}
        ? std::filesystem::path{"/var/lib/cec-injector-mate/runtime-config.backup"}
        : std::filesystem::path{};
    RuntimeConfigurationController configuration_controller(
        service,
        std::make_unique<FileRuntimeConfigurationStore>(*runtime_config_path,
                                                        backup_path),
        authoritative_configuration, {}, authoritative_revision);
    frontend_linux::LinuxDbusFrontendService dbus_frontend(
        service, configuration_controller);
    if (dbus_frontend.start() != OperationResult::Success) {
      std::cerr << "cec-injector-mate: system D-Bus frontend failed to start\n";
      service.shutdown();
      return 1;
    }
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    using namespace std::chrono_literals;
    while (!stopping.load()) {
      std::this_thread::sleep_for(500ms);
      service.refresh_discovery();
    }
    dbus_frontend.stop();
    service.shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "cec-injector-mate: " << error.what() << '\n';
    return 1;
  }
}

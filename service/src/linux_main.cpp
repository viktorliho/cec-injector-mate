#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "cec_injector/mate/linux/linux_device_session_factory.h"
#include "cec_injector/mate/linux/linux_host_platform.h"
#include "cec_injector/mate/mate_service.h"

namespace {

std::atomic<bool> stopping{};

extern "C" void Stop(int) { stopping = true; }

void Usage() {
  std::cerr
      << "usage: cec-injector-mate --pa-cache PATH [options]\n"
      << "  --uinput PATH\n  --device-id ID\n"
      << "  --remote-profile steamos-parity\n"
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
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    using namespace std::chrono_literals;
    while (!stopping.load()) {
      std::this_thread::sleep_for(500ms);
      service.refresh_discovery();
    }
    service.shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "cec-injector-mate: " << error.what() << '\n';
    return 1;
  }
}

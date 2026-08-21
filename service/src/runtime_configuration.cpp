#include "cec_injector/mate/runtime_configuration_io.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace cec_injector::mate {
namespace {

bool ParseBoolean(std::string_view value, bool& output) {
  if (value == "true") return output = true, true;
  if (value == "false") return output = false, true;
  return false;
}

std::string ErrnoMessage(std::string_view operation) {
  return std::string(operation) + ": " + std::strerror(errno);
}

bool WriteAll(int descriptor, std::string_view value, std::string& error) {
  while (!value.empty()) {
    const auto count = ::write(descriptor, value.data(), value.size());
    if (count < 0) {
      if (errno == EINTR) continue;
      error = ErrnoMessage("write runtime configuration");
      return false;
    }
    value.remove_prefix(static_cast<std::size_t>(count));
  }
  return true;
}

}  // namespace

MateRuntimeConfiguration production_runtime_defaults() {
  return {{.power_on_boot = true,
           .power_on_resume = true,
           .standby_on_suspend = true,
           .standby_on_shutdown = true,
           .suspend_on_tv_standby = true,
           .wake_host_on_tv_source_request = true},
          {.enabled = true,
           .profile = RemoteInputProfile::SteamOsParity}};
}

std::string_view remote_input_profile_name(RemoteInputProfile profile) noexcept {
  switch (profile) {
    case RemoteInputProfile::SteamOsParity: return "steamos-parity";
  }
  return "";
}

bool parse_remote_input_profile(std::string_view value,
                                RemoteInputProfile& profile) {
  if (value != "steamos-parity") return false;
  profile = RemoteInputProfile::SteamOsParity;
  return true;
}

std::string serialize_runtime_configuration(
    const MateRuntimeConfiguration& configuration) {
  const auto boolean = [](bool value) { return value ? "true" : "false"; };
  std::ostringstream output;
  output << "version=1\n"
         << "power_on_boot=" << boolean(configuration.policy.power_on_boot) << '\n'
         << "power_on_resume=" << boolean(configuration.policy.power_on_resume) << '\n'
         << "standby_on_suspend=" << boolean(configuration.policy.standby_on_suspend) << '\n'
         << "standby_on_shutdown=" << boolean(configuration.policy.standby_on_shutdown) << '\n'
         << "suspend_on_tv_standby=" << boolean(configuration.policy.suspend_on_tv_standby) << '\n'
         << "wake_host_on_tv_source_request="
         << boolean(configuration.policy.wake_host_on_tv_source_request) << '\n'
         << "remote_input_enabled=" << boolean(configuration.remote.enabled) << '\n'
         << "remote_profile="
         << remote_input_profile_name(configuration.remote.profile) << '\n';
  return output.str();
}

RuntimeConfigurationLoadResult parse_runtime_configuration(
    std::string_view text) {
  RuntimeConfigurationLoadResult result;
  result.existed = true;
  std::map<std::string, std::string> fields;
  std::istringstream input{std::string(text)};
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    const auto separator = line.find('=');
    if (separator == std::string::npos || separator == 0) {
      result.error = "malformed line " + std::to_string(line_number);
      return result;
    }
    const auto key = line.substr(0, separator);
    if (!fields.emplace(key, line.substr(separator + 1)).second) {
      result.error = "duplicate field: " + key;
      return result;
    }
  }
  const auto version = fields.find("version");
  if (version == fields.end() || version->second != "1") {
    result.error = "unsupported or missing configuration version";
    return result;
  }
  auto configuration = production_runtime_defaults();
  const auto parse_bool = [&](const char* key, bool& destination) {
    const auto found = fields.find(key);
    if (found == fields.end()) {
      result.error = std::string("missing field: ") + key;
      return false;
    }
    if (!ParseBoolean(found->second, destination)) {
      result.error = std::string("invalid boolean: ") + key;
      return false;
    }
    return true;
  };
  if (!parse_bool("power_on_boot", configuration.policy.power_on_boot) ||
      !parse_bool("power_on_resume", configuration.policy.power_on_resume) ||
      !parse_bool("standby_on_suspend", configuration.policy.standby_on_suspend) ||
      !parse_bool("standby_on_shutdown", configuration.policy.standby_on_shutdown) ||
      !parse_bool("suspend_on_tv_standby", configuration.policy.suspend_on_tv_standby) ||
      !parse_bool("wake_host_on_tv_source_request",
                  configuration.policy.wake_host_on_tv_source_request) ||
      !parse_bool("remote_input_enabled", configuration.remote.enabled)) {
    return result;
  }
  const auto profile = fields.find("remote_profile");
  if (profile == fields.end() ||
      !parse_remote_input_profile(profile->second, configuration.remote.profile)) {
    result.error = "invalid or missing remote_profile";
    return result;
  }
  result.configuration = configuration;
  return result;
}

RuntimeConfigurationLoadResult load_runtime_configuration(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (!std::filesystem::exists(path)) {
      return {.configuration = production_runtime_defaults(),
              .existed = false,
              .error = {}};
    }
    return {.configuration = {},
            .existed = true,
            .error = "cannot open runtime configuration"};
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return {.configuration = {},
            .existed = true,
            .error = "cannot read runtime configuration"};
  }
  return parse_runtime_configuration(contents.str());
}

bool persist_runtime_configuration_atomic(
    const std::filesystem::path& path,
    const MateRuntimeConfiguration& configuration, std::string& error) {
  const auto directory = path.parent_path();
  std::string temporary_name{"."};
  temporary_name.append(path.filename().string());
  temporary_name.append(".tmp");
  const auto temporary = directory / temporary_name;
  const auto contents = serialize_runtime_configuration(configuration);
  const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC |
                                                     O_CLOEXEC | O_NOFOLLOW,
                                0600);
  if (descriptor < 0) return error = ErrnoMessage("open temporary configuration"), false;
  bool ok = WriteAll(descriptor, contents, error);
  if (ok && ::fsync(descriptor) != 0) {
    error = ErrnoMessage("fsync temporary configuration");
    ok = false;
  }
  if (::close(descriptor) != 0 && ok) {
    error = ErrnoMessage("close temporary configuration");
    ok = false;
  }
  if (!ok) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  if (::rename(temporary.c_str(), path.c_str()) != 0) {
    error = ErrnoMessage("rename runtime configuration");
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  const int directory_descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory_descriptor < 0) return error = ErrnoMessage("open configuration directory"), false;
  const bool synced = ::fsync(directory_descriptor) == 0;
  if (!synced) error = ErrnoMessage("fsync configuration directory");
  ::close(directory_descriptor);
  return synced;
}

}  // namespace cec_injector::mate

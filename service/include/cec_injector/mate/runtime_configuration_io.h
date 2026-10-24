#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>

#include "cec_injector/mate/runtime_configuration.h"

namespace cec_injector::mate {

struct RuntimeConfigurationLoadResult {
  MateRuntimeConfiguration configuration;
  std::uint64_t revision{1};
  bool existed{};
  std::string error;
  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] std::string serialize_runtime_configuration(
    const MateRuntimeConfiguration& configuration, std::uint64_t revision = 1);
[[nodiscard]] RuntimeConfigurationLoadResult parse_runtime_configuration(
    std::string_view text);
[[nodiscard]] RuntimeConfigurationLoadResult load_runtime_configuration(
    const std::filesystem::path& path);
[[nodiscard]] bool persist_runtime_configuration_atomic(
    const std::filesystem::path& path,
    const MateRuntimeConfiguration& configuration, std::string& error,
    std::uint64_t revision = 1);

}  // namespace cec_injector::mate

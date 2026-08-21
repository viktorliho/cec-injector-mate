#pragma once

#include <string_view>

#include "cec_injector/mate/policy.h"

namespace cec_injector::mate {

enum class RemoteInputProfile { SteamOsParity };

struct RemoteInputConfiguration {
  bool enabled{true};
  RemoteInputProfile profile{RemoteInputProfile::SteamOsParity};
  bool operator==(const RemoteInputConfiguration&) const = default;
};

struct MateRuntimeConfiguration {
  PolicyConfiguration policy;
  RemoteInputConfiguration remote;
  bool operator==(const MateRuntimeConfiguration&) const = default;
};

[[nodiscard]] MateRuntimeConfiguration production_runtime_defaults();
[[nodiscard]] std::string_view remote_input_profile_name(
    RemoteInputProfile profile) noexcept;
[[nodiscard]] bool parse_remote_input_profile(std::string_view value,
                                              RemoteInputProfile& profile);
}  // namespace cec_injector::mate

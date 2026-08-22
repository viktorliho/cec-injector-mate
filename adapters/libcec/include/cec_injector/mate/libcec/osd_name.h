#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cec_injector::mate::libcec {

inline constexpr std::size_t kCecMaxFrameOctets = 16;
inline constexpr std::size_t kSetOsdNameOverheadOctets = 2;
inline constexpr std::size_t kSetOsdNameMaxOperandOctets =
    kCecMaxFrameOctets - kSetOsdNameOverheadOctets;

[[nodiscard]] inline std::array<char, kSetOsdNameMaxOperandOctets + 1>
make_libcec_osd_name(std::string_view input) {
  std::array<char, kSetOsdNameMaxOperandOctets + 1> output{};
  const auto count = input.size() < kSetOsdNameMaxOperandOctets
                         ? input.size()
                         : kSetOsdNameMaxOperandOctets;
  for (std::size_t index = 0; index < count; ++index) {
    output[index] = input[index];
  }
  return output;
}

}  // namespace cec_injector::mate::libcec

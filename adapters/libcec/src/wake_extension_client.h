#pragma once
#include <chrono>
#include <functional>
#include <span>
#include <vector>
#include "cec_injector/mate/libcec/libcec_api.h"
#include "cec_injector/mate/types.h"
namespace cec_injector::mate::libcec::detail {
class WakeExtensionClient {
 public:
  using Transaction = std::function<BackendResult(std::uint8_t,
      std::span<const std::uint8_t>, std::vector<std::uint8_t>&,
      std::chrono::milliseconds)>;
  explicit WakeExtensionClient(Transaction);
  WakeState query(); OperationResult configure(std::uint16_t);
  OperationResult arm(std::uint16_t); OperationResult disarm();
  WakeReason read_reason(); OperationResult clear_reason();
  [[nodiscard]] const WakeState& state() const { return state_; }
 private:
  OperationResult call(std::uint8_t, std::span<const std::uint8_t>,
                       std::vector<std::uint8_t>&);
  OperationResult detect(); Transaction transaction_; WakeState state_;
};
}  // namespace cec_injector::mate::libcec::detail

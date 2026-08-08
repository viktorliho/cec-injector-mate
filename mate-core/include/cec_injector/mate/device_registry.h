#pragma once

#include <optional>
#include <set>
#include <vector>

#include "cec_injector/mate/types.h"

namespace cec_injector::mate {

enum class SelectionState { Waiting, Selected, Ambiguous, SelectionRequired };

enum class CoreIntentKind { ConnectDevice, DisconnectDevice };

struct CoreIntent {
  CoreIntentKind kind;
  DeviceId device;

  bool operator==(const CoreIntent&) const = default;
};

struct SelectionSnapshot {
  SelectionState state{SelectionState::Waiting};
  std::set<DeviceId> discovered;
  std::optional<DeviceId> explicit_selection;
  std::optional<DeviceId> selected_active;

  bool operator==(const SelectionSnapshot&) const = default;
};

class DeviceRegistry {
 public:
  [[nodiscard]] std::vector<CoreIntent> update_discovered(
      std::vector<DeviceId> devices);
  [[nodiscard]] std::vector<CoreIntent> set_explicit_selection(
      std::optional<DeviceId> device);
  [[nodiscard]] const SelectionSnapshot& snapshot() const noexcept {
    return snapshot_;
  }

 private:
  [[nodiscard]] std::vector<CoreIntent> reconcile(bool selected_was_lost);

  SelectionSnapshot snapshot_;
  bool automatic_reselection_suppressed_{};
};

}  // namespace cec_injector::mate

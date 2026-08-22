#include "cec_injector/mate/device_registry.h"

#include <algorithm>
#include <stdexcept>

namespace cec_injector::mate {

std::vector<CoreIntent> DeviceRegistry::update_discovered(
    std::vector<DeviceId> devices) {
  std::set<DeviceId> next;
  for (auto& device : devices) {
    if (!device.valid()) throw std::invalid_argument("discovered DeviceId is invalid");
    next.insert(std::move(device));
  }

  const bool selected_was_lost = snapshot_.selected_active &&
      !next.contains(*snapshot_.selected_active);
  snapshot_.discovered = std::move(next);
  if (snapshot_.discovered.empty()) automatic_reselection_suppressed_ = false;
  return reconcile(selected_was_lost);
}

std::vector<CoreIntent> DeviceRegistry::set_explicit_selection(
    std::optional<DeviceId> device) {
  if (device && !device->valid()) {
    throw std::invalid_argument("explicit DeviceId is invalid");
  }
  snapshot_.explicit_selection = std::move(device);
  automatic_reselection_suppressed_ = false;
  return reconcile(false);
}

std::vector<CoreIntent> DeviceRegistry::reconcile(bool selected_was_lost) {
  std::vector<CoreIntent> intents;
  const auto previous = snapshot_.selected_active;

  if (selected_was_lost) {
    intents.push_back({CoreIntentKind::DisconnectDevice, *previous});
    snapshot_.selected_active.reset();
    if (!snapshot_.explicit_selection) {
      automatic_reselection_suppressed_ = !snapshot_.discovered.empty();
    }
  }

  std::optional<DeviceId> desired;
  if (snapshot_.explicit_selection) {
    if (snapshot_.discovered.contains(*snapshot_.explicit_selection)) {
      desired = snapshot_.explicit_selection;
    }
  } else if (snapshot_.selected_active &&
             snapshot_.discovered.contains(*snapshot_.selected_active)) {
    desired = snapshot_.selected_active;
  } else if (!automatic_reselection_suppressed_ &&
             snapshot_.discovered.size() == 1) {
    desired = *snapshot_.discovered.begin();
  }

  if (snapshot_.selected_active && snapshot_.selected_active != desired) {
    intents.push_back({CoreIntentKind::DisconnectDevice,
                       *snapshot_.selected_active});
    snapshot_.selected_active.reset();
  }
  if (desired && snapshot_.selected_active != desired) {
    snapshot_.selected_active = desired;
    intents.push_back({CoreIntentKind::ConnectDevice, *desired});
  }

  if (snapshot_.selected_active) {
    snapshot_.state = SelectionState::Selected;
  } else if (snapshot_.explicit_selection || snapshot_.discovered.empty()) {
    snapshot_.state = SelectionState::Waiting;
  } else if (automatic_reselection_suppressed_) {
    snapshot_.state = SelectionState::SelectionRequired;
  } else if (snapshot_.discovered.size() > 1) {
    snapshot_.state = SelectionState::Ambiguous;
  } else {
    snapshot_.state = SelectionState::Waiting;
  }
  return intents;
}

}  // namespace cec_injector::mate

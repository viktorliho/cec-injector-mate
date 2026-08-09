#include "cec_injector/mate/linux/linux_remote_input.h"

#include <stdexcept>
#include <utility>

namespace cec_injector::mate::linux_platform {

LinuxSemanticMapping generic_linux_mapping(SemanticRemoteAction action) {
  using Class = LinuxMappingClass;
  using Key = LinuxInputKey;
  switch (action) {
    case SemanticRemoteAction::Up:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Up}}};
    case SemanticRemoteAction::Down:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Down}}};
    case SemanticRemoteAction::Left:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Left}}};
    case SemanticRemoteAction::Right:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Right}}};
    case SemanticRemoteAction::Select:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Enter}}};
    case SemanticRemoteAction::Back:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Escape}}};
    case SemanticRemoteAction::Info:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Info}}};
    case SemanticRemoteAction::Play:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Play}}};
    case SemanticRemoteAction::Pause:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Pause}}};
    case SemanticRemoteAction::PlayPause:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::PlayPause}}};
    case SemanticRemoteAction::Stop:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::Stop}}};
    case SemanticRemoteAction::Next:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::NextSong}}};
    case SemanticRemoteAction::Previous:
      return {Class::GenericLinuxMapping, LinuxInputBinding{{Key::PreviousSong}}};
    case SemanticRemoteAction::Guide:
      return {Class::ProfileRequired, std::nullopt};
    case SemanticRemoteAction::Home:
    case SemanticRemoteAction::Menu:
      return {Class::CurrentlyUnmapped, std::nullopt};
  }
  return {Class::CurrentlyUnmapped, std::nullopt};
}

LinuxRemoteInputProfile steamos_parity_remote_input_profile() {
  using Action = SemanticRemoteAction;
  using Key = LinuxInputKey;
  return {{{Action::Guide, {{Key::LeftShift, Key::Tab}}},
           {Action::Info,
            {{Key::LeftControl, Key::LeftShift, Key::Tab}}}}};
}

LinuxRemoteInputController::LinuxRemoteInputController(
    std::unique_ptr<LinuxInputSink> sink, LinuxRemoteInputProfile profile)
    : sink_(std::move(sink)), profile_(std::move(profile)) {
  if (!sink_) throw std::invalid_argument("Linux input sink is required");
  for (int value = static_cast<int>(SemanticRemoteAction::Up);
       value <= static_cast<int>(SemanticRemoteAction::Previous); ++value) {
    const auto action = static_cast<SemanticRemoteAction>(value);
    const auto binding = binding_for(action);
    if (!binding || binding->keys.empty()) continue;
    capabilities_.insert(binding->keys.begin(), binding->keys.end());
  }
}

LinuxRemoteInputController::~LinuxRemoteInputController() {
  (void)reset_remote_input();
  if (device_created_) sink_->destroy();
}

std::optional<LinuxInputBinding> LinuxRemoteInputController::binding_for(
    SemanticRemoteAction action) const {
  const auto profile = profile_.bindings.find(action);
  if (profile != profile_.bindings.end()) return profile->second;
  return generic_linux_mapping(action).binding;
}

OperationResult LinuxRemoteInputController::ensure_device() {
  if (device_created_) return OperationResult::Success;
  const auto result = sink_->create(capabilities_);
  device_created_ = result == OperationResult::Success;
  return result;
}

void LinuxRemoteInputController::fail_device() noexcept {
  sink_->destroy();
  device_created_ = false;
  active_action_.reset();
  active_binding_.reset();
}

OperationResult LinuxRemoteInputController::press(
    SemanticRemoteAction action, const LinuxInputBinding& binding,
    Clock::time_point now) {
  const auto ready = ensure_device();
  if (ready != OperationResult::Success) return ready;
  std::size_t emitted = 0;
  for (; emitted < binding.keys.size(); ++emitted) {
    const auto result = emit_key(binding.keys[emitted], true);
    if (result == OperationResult::Success) continue;
    while (emitted > 0) {
      --emitted;
      (void)emit_key(binding.keys[emitted], false);
    }
    fail_device();
    return result;
  }
  active_action_ = action;
  active_binding_ = binding;
  release_deadline_ = now + kSafetyReleaseTimeout;
  return OperationResult::Success;
}

OperationResult LinuxRemoteInputController::emit_key(LinuxInputKey key,
                                                     bool pressed) {
  const auto key_result =
      sink_->emit({LinuxInputEventKind::Key, key, pressed});
  if (key_result != OperationResult::Success) return key_result;
  return sink_->emit({LinuxInputEventKind::Synchronize});
}

OperationResult LinuxRemoteInputController::release_active() {
  if (!active_binding_) return OperationResult::Success;
  OperationResult result = OperationResult::Success;
  if (device_created_) {
    for (auto key = active_binding_->keys.rbegin();
         key != active_binding_->keys.rend(); ++key) {
      const auto emitted = emit_key(*key, false);
      if (emitted != OperationResult::Success) {
        result = emitted;
        break;
      }
    }
  }
  active_action_.reset();
  active_binding_.reset();
  if (result != OperationResult::Success) fail_device();
  return result;
}

OperationResult LinuxRemoteInputController::emit_remote_action(
    SemanticRemoteAction action, RemoteActionState state,
    Clock::time_point now) {
  const auto binding = binding_for(action);
  if (!binding || binding->keys.empty()) return OperationResult::Unsupported;
  if (state == RemoteActionState::Released) {
    if (active_action_ != action) return OperationResult::Success;
    return release_active();
  }
  if (active_action_ == action && active_binding_ == binding) {
    release_deadline_ = now + kSafetyReleaseTimeout;
    return OperationResult::Success;
  }
  const auto released = release_active();
  if (released != OperationResult::Success) return released;
  return press(action, *binding, now);
}

OperationResult LinuxRemoteInputController::reset_remote_input() {
  return release_active();
}

OperationResult LinuxRemoteInputController::tick(Clock::time_point now) {
  if (!active_binding_ || now < release_deadline_) {
    return OperationResult::InvalidState;
  }
  return release_active();
}

}  // namespace cec_injector::mate::linux_platform

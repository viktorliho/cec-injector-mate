#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "cec_injector/mate/types.h"

namespace cec_injector::mate::linux_platform {

enum class LinuxInputKey {
  Up,
  Down,
  Left,
  Right,
  Enter,
  Escape,
  Info,
  Play,
  Pause,
  PlayPause,
  Stop,
  NextSong,
  PreviousSong,
  LeftControl,
  LeftShift,
  Tab,
};

struct LinuxInputBinding {
  std::vector<LinuxInputKey> keys;
  bool operator==(const LinuxInputBinding&) const = default;
};

struct LinuxRemoteInputProfile {
  std::map<SemanticRemoteAction, LinuxInputBinding> bindings;
};

[[nodiscard]] LinuxRemoteInputProfile steamos_parity_remote_input_profile();

enum class LinuxInputEventKind { Key, Synchronize };

struct LinuxInputEvent {
  LinuxInputEventKind kind{LinuxInputEventKind::Synchronize};
  LinuxInputKey key{LinuxInputKey::Up};
  bool pressed{};
  bool operator==(const LinuxInputEvent&) const = default;
};

enum class LinuxMappingClass {
  GenericLinuxMapping,
  ProfileRequired,
  CurrentlyUnmapped,
};

struct LinuxSemanticMapping {
  LinuxMappingClass classification{LinuxMappingClass::CurrentlyUnmapped};
  std::optional<LinuxInputBinding> binding;
};

[[nodiscard]] LinuxSemanticMapping generic_linux_mapping(
    SemanticRemoteAction action);

class LinuxInputSink {
 public:
  virtual ~LinuxInputSink() = default;
  virtual OperationResult create(const std::set<LinuxInputKey>& capabilities) = 0;
  virtual OperationResult emit(const LinuxInputEvent& event) = 0;
  virtual void destroy() noexcept = 0;
};

class LinuxRemoteInputController {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kSafetyReleaseTimeout{900};

  explicit LinuxRemoteInputController(
      std::unique_ptr<LinuxInputSink> sink,
      LinuxRemoteInputProfile profile = {});
  ~LinuxRemoteInputController();

  LinuxRemoteInputController(const LinuxRemoteInputController&) = delete;
  LinuxRemoteInputController& operator=(const LinuxRemoteInputController&) = delete;

  OperationResult emit_remote_action(SemanticRemoteAction action,
                                     RemoteActionState state,
                                     Clock::time_point now = Clock::now());
  OperationResult reset_remote_input();
  OperationResult tick(Clock::time_point now = Clock::now());

  [[nodiscard]] std::optional<SemanticRemoteAction> active_action() const noexcept {
    return active_action_;
  }
  [[nodiscard]] bool device_created() const noexcept { return device_created_; }
  [[nodiscard]] const std::set<LinuxInputKey>& capabilities() const noexcept {
    return capabilities_;
  }

 private:
  [[nodiscard]] std::optional<LinuxInputBinding> binding_for(
      SemanticRemoteAction action) const;
  OperationResult ensure_device();
  OperationResult press(SemanticRemoteAction action,
                        const LinuxInputBinding& binding,
                        Clock::time_point now);
  OperationResult release_active();
  OperationResult emit_key(LinuxInputKey key, bool pressed);
  void fail_device() noexcept;

  std::unique_ptr<LinuxInputSink> sink_;
  LinuxRemoteInputProfile profile_;
  std::set<LinuxInputKey> capabilities_;
  bool device_created_{};
  std::optional<SemanticRemoteAction> active_action_;
  std::optional<LinuxInputBinding> active_binding_;
  Clock::time_point release_deadline_{};
};

[[nodiscard]] std::unique_ptr<LinuxInputSink> create_linux_uinput_sink(
    std::filesystem::path endpoint = "/dev/uinput");

}  // namespace cec_injector::mate::linux_platform

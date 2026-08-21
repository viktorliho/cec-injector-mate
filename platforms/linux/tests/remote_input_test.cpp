#include "cec_injector/mate/linux/linux_remote_input.h"

#include <memory>
#include <set>
#include <vector>

#include "test_support.h"

using namespace cec_injector::mate;
using namespace cec_injector::mate::linux_platform;

namespace {

class FakeSink final : public LinuxInputSink {
 public:
  OperationResult create(const std::set<LinuxInputKey>& requested) override {
    ++create_count;
    capabilities = requested;
    return create_result;
  }

  OperationResult emit(const LinuxInputEvent& event) override {
    ++emit_count;
    if (fail_emit_at != 0 && emit_count == fail_emit_at) {
      return OperationResult::InternalError;
    }
    events.push_back(event);
    return OperationResult::Success;
  }

  void destroy() noexcept override { ++destroy_count; }

  OperationResult create_result{OperationResult::Success};
  std::size_t fail_emit_at{};
  std::size_t emit_count{};
  int create_count{};
  int destroy_count{};
  std::set<LinuxInputKey> capabilities;
  std::vector<LinuxInputEvent> events;
};

LinuxInputEvent Key(LinuxInputKey key, bool pressed) {
  return {LinuxInputEventKind::Key, key, pressed};
}

LinuxInputEvent Sync() {
  return {LinuxInputEventKind::Synchronize, LinuxInputKey::Up, false};
}

}  // namespace

int main() {
  using Action = SemanticRemoteAction;
  using Class = LinuxMappingClass;
  using KeyCode = LinuxInputKey;
  using State = RemoteActionState;

  const std::vector<Action> direct_navigation = {
      Action::Up, Action::Down, Action::Left, Action::Right,
      Action::Select, Action::Back, Action::Info};
  for (const auto action : direct_navigation) {
    const auto mapping = generic_linux_mapping(action);
    CHECK(mapping.classification == Class::GenericLinuxMapping);
    CHECK(mapping.binding.has_value());
    CHECK(mapping.binding->keys.size() == 1);
  }
  for (const auto action : {Action::Play, Action::Pause, Action::PlayPause,
                            Action::Stop, Action::Next, Action::Previous}) {
    CHECK(generic_linux_mapping(action).classification ==
          Class::GenericLinuxMapping);
  }
  CHECK(generic_linux_mapping(Action::Guide).classification ==
        Class::ProfileRequired);
  CHECK(!generic_linux_mapping(Action::Guide).binding);
  CHECK(generic_linux_mapping(Action::Info).binding ==
        LinuxInputBinding{{KeyCode::Info}});
  CHECK(generic_linux_mapping(Action::Home).classification ==
        Class::CurrentlyUnmapped);
  CHECK(generic_linux_mapping(Action::Menu).classification ==
        Class::CurrentlyUnmapped);

  auto sink = std::make_unique<FakeSink>();
  auto* fake = sink.get();
  LinuxRemoteInputController input(std::move(sink));
  const auto start = LinuxRemoteInputController::Clock::time_point{};

  CHECK(input.initialize() == OperationResult::Success);
  CHECK(input.device_created());
  CHECK(fake->create_count == 1);
  CHECK(fake->events.empty());

  CHECK(input.emit_remote_action(Action::Up, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(fake->create_count == 1);
  CHECK(fake->events ==
        std::vector<LinuxInputEvent>({Key(KeyCode::Up, true), Sync()}));
  CHECK(input.active_action() == Action::Up);

  CHECK(input.emit_remote_action(Action::Up, State::Pressed,
                                 start + std::chrono::milliseconds(100)) ==
        OperationResult::Success);
  CHECK(fake->events.size() == 2);

  CHECK(input.emit_remote_action(Action::Down, State::Pressed,
                                 start + std::chrono::milliseconds(200)) ==
        OperationResult::Success);
  CHECK(fake->events == std::vector<LinuxInputEvent>(
                            {Key(KeyCode::Up, true), Sync(),
                             Key(KeyCode::Up, false), Sync(),
                             Key(KeyCode::Down, true), Sync()}));

  CHECK(input.emit_remote_action(Action::Up, State::Released) ==
        OperationResult::Success);
  CHECK(fake->events.size() == 6);
  CHECK(input.reset_remote_input() == OperationResult::Success);
  CHECK(fake->events[fake->events.size() - 2] == Key(KeyCode::Down, false));
  CHECK(fake->events.back().kind == LinuxInputEventKind::Synchronize);
  CHECK(!input.active_action());

  CHECK(input.emit_remote_action(Action::Right, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(input.tick(start + LinuxRemoteInputController::kSafetyReleaseTimeout -
                   std::chrono::milliseconds(1)) ==
        OperationResult::InvalidState);
  CHECK(input.active_action() == Action::Right);
  CHECK(input.tick(start + LinuxRemoteInputController::kSafetyReleaseTimeout) ==
        OperationResult::Success);
  CHECK(!input.active_action());

  const auto parity_profile = steamos_parity_remote_input_profile();
  CHECK(parity_profile.bindings.at(Action::Guide) ==
        LinuxInputBinding{{KeyCode::LeftShift, KeyCode::Tab}});
  CHECK(parity_profile.bindings.at(Action::Info) ==
        LinuxInputBinding{
            {KeyCode::LeftControl, KeyCode::LeftShift, KeyCode::Tab}});

  auto chord_sink = std::make_unique<FakeSink>();
  auto* chord_fake = chord_sink.get();
  LinuxRemoteInputController chord(std::move(chord_sink), parity_profile);
  CHECK(chord.emit_remote_action(Action::Guide, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(chord.reset_remote_input() == OperationResult::Success);
  CHECK(chord_fake->events == std::vector<LinuxInputEvent>(
                                   {Key(KeyCode::LeftShift, true), Sync(),
                                    Key(KeyCode::Tab, true), Sync(),
                                    Key(KeyCode::Tab, false), Sync(),
                                    Key(KeyCode::LeftShift, false), Sync()}));

  chord_fake->events.clear();
  CHECK(chord.emit_remote_action(Action::Guide, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(chord.emit_remote_action(Action::Info, State::Pressed,
                                 start + std::chrono::milliseconds(1)) ==
        OperationResult::Success);
  CHECK(chord_fake->events == std::vector<LinuxInputEvent>(
                                   {Key(KeyCode::LeftShift, true), Sync(),
                                    Key(KeyCode::Tab, true), Sync(),
                                    Key(KeyCode::Tab, false), Sync(),
                                    Key(KeyCode::LeftShift, false), Sync(),
                                    Key(KeyCode::LeftControl, true), Sync(),
                                    Key(KeyCode::LeftShift, true), Sync(),
                                    Key(KeyCode::Tab, true), Sync()}));
  CHECK(chord.reset_remote_input() == OperationResult::Success);
  CHECK(chord_fake->events == std::vector<LinuxInputEvent>(
                                   {Key(KeyCode::LeftShift, true), Sync(),
                                    Key(KeyCode::Tab, true), Sync(),
                                    Key(KeyCode::Tab, false), Sync(),
                                    Key(KeyCode::LeftShift, false), Sync(),
                                    Key(KeyCode::LeftControl, true), Sync(),
                                    Key(KeyCode::LeftShift, true), Sync(),
                                    Key(KeyCode::Tab, true), Sync(),
                                    Key(KeyCode::Tab, false), Sync(),
                                    Key(KeyCode::LeftShift, false), Sync(),
                                    Key(KeyCode::LeftControl, false), Sync()}));

  chord_fake->events.clear();
  CHECK(chord.emit_remote_action(Action::Info, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(chord.tick(start + LinuxRemoteInputController::kSafetyReleaseTimeout) ==
        OperationResult::Success);
  CHECK(chord_fake->events == std::vector<LinuxInputEvent>(
                                   {Key(KeyCode::LeftControl, true), Sync(),
                                    Key(KeyCode::LeftShift, true), Sync(),
                                    Key(KeyCode::Tab, true), Sync(),
                                    Key(KeyCode::Tab, false), Sync(),
                                    Key(KeyCode::LeftShift, false), Sync(),
                                    Key(KeyCode::LeftControl, false), Sync()}));

  auto failure_sink = std::make_unique<FakeSink>();
  auto* failure_fake = failure_sink.get();
  failure_fake->fail_emit_at = 2;
  LinuxRemoteInputController recovering(std::move(failure_sink));
  CHECK(recovering.emit_remote_action(Action::Left, State::Pressed, start) ==
        OperationResult::InternalError);
  CHECK(!recovering.active_action());
  CHECK(!recovering.device_created());
  CHECK(failure_fake->destroy_count == 1);
  failure_fake->fail_emit_at = 0;
  CHECK(recovering.emit_remote_action(Action::Left, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(failure_fake->create_count == 2);
  CHECK(recovering.reset_remote_input() == OperationResult::Success);

  auto live_sink = std::make_unique<FakeSink>();
  auto* live_fake = live_sink.get();
  LinuxRemoteInputController live(std::move(live_sink), parity_profile);
  CHECK(live.emit_remote_action(Action::Guide, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(live.reconfigure(false, parity_profile) == OperationResult::Success);
  CHECK(!live.active_action());
  const auto disabled_events = live_fake->events.size();
  CHECK(live.emit_remote_action(Action::Down, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(live_fake->events.size() == disabled_events);
  CHECK(live.reconfigure(true, parity_profile) == OperationResult::Success);
  CHECK(live.emit_remote_action(Action::Down, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(live.active_action() == Action::Down);
  auto alternate = parity_profile;
  alternate.bindings[Action::Guide] = {{KeyCode::LeftControl, KeyCode::Tab}};
  CHECK(live.reconfigure(true, alternate) == OperationResult::Success);
  CHECK(!live.active_action());
  CHECK(live.emit_remote_action(Action::Guide, State::Pressed, start) ==
        OperationResult::Success);
  CHECK(live.reset_remote_input() == OperationResult::Success);

  CHECK(input.emit_remote_action(Action::Guide, State::Pressed) ==
        OperationResult::Unsupported);
  CHECK(input.emit_remote_action(Action::Home, State::Pressed) ==
        OperationResult::Unsupported);
  return EXIT_SUCCESS;
}

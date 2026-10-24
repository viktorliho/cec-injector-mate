#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>
#include <unistd.h>

#include "cec_injector/mate/runtime_configuration.h"
#include "cec_injector/mate/runtime_configuration_io.h"
#include "cec_injector/mate/runtime_configuration_controller.h"

namespace {
void Check(bool value, std::string_view message) {
  if (value) return;
  std::cerr << "CHECK failed: " << message << '\n';
  std::exit(1);
}
}  // namespace

class Store final : public cec_injector::mate::RuntimeConfigurationStore {
 public:
  bool prepare(const cec_injector::mate::MateRuntimeConfiguration& value,
               std::uint64_t,
               std::string& error) override {
    if (fail_prepare) { fail_prepare = false; error = "injected prepare failure"; return false; }
    pending = value;
    return true;
  }
  bool commit(std::string& error) override {
    if (fail_commit) { fail_commit = false; error = "injected commit failure"; return false; }
    values.push_back(*pending);
    pending.reset();
    return true;
  }
  bool restore(const cec_injector::mate::MateRuntimeConfiguration& value,
               std::uint64_t,
               std::string&) override {
    restored = value;
    return !fail_restore;
  }
  void abort() noexcept override { pending.reset(); }
  bool fail_prepare{};
  bool fail_commit{};
  bool fail_restore{};
  std::optional<cec_injector::mate::MateRuntimeConfiguration> pending;
  std::optional<cec_injector::mate::MateRuntimeConfiguration> restored;
  std::vector<cec_injector::mate::MateRuntimeConfiguration> values;
};

int main() {
  using namespace cec_injector::mate;
  const auto defaults = production_runtime_defaults();
  Check(defaults.policy.power_on_boot && defaults.policy.power_on_resume &&
            defaults.policy.standby_on_suspend &&
            defaults.policy.standby_on_shutdown &&
            defaults.policy.suspend_on_tv_standby &&
            defaults.policy.wake_host_on_tv_source_request &&
            defaults.remote.enabled,
        "explicit production defaults");

  const auto text = serialize_runtime_configuration(defaults);
  const auto parsed = parse_runtime_configuration(text);
  Check(parsed.ok() && parsed.configuration == defaults && parsed.revision == 1,
        "full round trip");
  auto revised = parse_runtime_configuration(
      serialize_runtime_configuration(defaults, 47));
  Check(revised.ok() && revised.revision == 47,
        "configuration revision round trip");
  auto legacy = text;
  legacy.erase(legacy.find("revision=1\n"), sizeof("revision=1\n") - 1);
  Check(parse_runtime_configuration(legacy).revision == 1,
        "legacy configuration revision migration");
  Check(!parse_runtime_configuration("version=1\nrevision=0\n").ok(),
        "zero revision rejected");
  Check(!parse_runtime_configuration("version=2\n").ok(),
        "unsupported version rejected");
  auto malformed = text;
  malformed.replace(malformed.find("power_on_boot=true"), 18,
                    "power_on_boot=maybe");
  Check(!parse_runtime_configuration(malformed).ok(),
        "malformed known boolean rejected");
  auto invalid_profile = text;
  invalid_profile.replace(invalid_profile.find("steamos-parity"), 14, "unknown");
  Check(!parse_runtime_configuration(invalid_profile).ok(),
        "invalid profile rejected");
  Check(parse_runtime_configuration(text + "future_field=value\n").ok(),
        "unknown future field tolerated");

  const auto directory = std::filesystem::temp_directory_path() /
                         ("mate-runtime-config-" + std::to_string(::getpid()));
  std::filesystem::create_directory(directory);
  const auto path = directory / "config.conf";
  const auto missing = load_runtime_configuration(path);
  Check(missing.ok() && !missing.existed && missing.configuration == defaults,
        "missing file returns explicit defaults");
  std::string error;
  Check(persist_runtime_configuration_atomic(path, defaults, error),
        "atomic persist");
  const auto loaded = load_runtime_configuration(path);
  Check(loaded.ok() && loaded.existed && loaded.configuration == defaults,
        "restart reload");
  Check(!std::filesystem::exists(directory / ".config.conf.tmp"),
        "temporary file removed");
  const auto backup_directory = directory / "state";
  std::filesystem::create_directory(backup_directory);
  const auto backup = backup_directory / "runtime-config.backup";
  Check(persist_runtime_configuration_atomic(backup, defaults, error, 9) &&
            persist_runtime_configuration_atomic(path, defaults, error, 9),
        "seed revisioned primary and backup");
  RuntimeConfigurationController durable_controller(
      [](const MateRuntimeConfiguration&) { return OperationResult::Success; },
      std::make_unique<FileRuntimeConfigurationStore>(path, backup), defaults,
      {}, 9);
  auto durable_change = defaults;
  durable_change.remote.enabled = false;
  Check(durable_controller.set(9, durable_change).result ==
            OperationResult::Success &&
            load_runtime_configuration(path).revision == 10 &&
            load_runtime_configuration(backup).revision == 10 &&
            load_runtime_configuration(backup).configuration == durable_change,
        "GUI apply persists primary and backup at the same revision");
  Check(std::filesystem::equivalent(path, backup) == false &&
            std::filesystem::file_size(path) == std::filesystem::file_size(backup),
        "backup is independent durable file");
  const auto unavailable_backup = directory / "missing" / "backup";
  int durable_applies = 0;
  RuntimeConfigurationController backup_failure(
      [&](const MateRuntimeConfiguration&) {
        ++durable_applies;
        return OperationResult::Success;
      }, std::make_unique<FileRuntimeConfigurationStore>(path, unavailable_backup),
      durable_change, {}, 10);
  Check(backup_failure.set(10, defaults).result == OperationResult::InternalError &&
            durable_applies == 0 && load_runtime_configuration(path).revision == 10,
        "backup prepare failure leaves live and authoritative state unchanged");
  int rollback_applies = 0;
  RuntimeConfigurationController interrupted_backup(
      [&](const MateRuntimeConfiguration&) {
        if (++rollback_applies == 1)
          std::filesystem::remove(backup_directory / ".runtime-config.backup.pending");
        return OperationResult::Success;
      }, std::make_unique<FileRuntimeConfigurationStore>(path, backup),
      durable_change, {}, 10);
  Check(interrupted_backup.set(10, defaults).result ==
            OperationResult::InternalError && rollback_applies == 2 &&
            load_runtime_configuration(path).revision == 10 &&
            load_runtime_configuration(backup).revision == 10 &&
            load_runtime_configuration(path).configuration == durable_change &&
            load_runtime_configuration(backup).configuration == durable_change,
        "backup commit failure restores live, primary and backup revisions");
  Check(!persist_runtime_configuration_atomic(path, defaults, error, 0),
        "invalid revision is not persisted");
  std::filesystem::remove_all(directory);

  auto store = std::make_unique<Store>();
  auto* store_view = store.get();
  int applies = 0;
  RuntimeConfigurationController controller(
      [&](const MateRuntimeConfiguration&) { ++applies; return OperationResult::Success; },
      std::move(store), defaults);
  auto changed = defaults;
  changed.policy.power_on_resume = false;
  const auto success = controller.set(1, changed);
  Check(success.result == OperationResult::Success && success.authoritative.revision == 2 &&
            applies == 1 && store_view->values.size() == 1,
        "persist then live apply increments revision");
  Check(controller.set(1, defaults).result == OperationResult::Rejected,
        "stale revision rejected without mutation");
  store_view->fail_prepare = true;
  changed.policy.power_on_boot = false;
  Check(controller.set(2, changed).result == OperationResult::InternalError &&
            applies == 1 && controller.current().revision == 2,
        "prepare failure prevents live apply");

  auto rollback_store = std::make_unique<Store>();
  auto* rollback_view = rollback_store.get();
  RuntimeConfigurationController rollback(
      [](const MateRuntimeConfiguration&) { return OperationResult::InvalidState; },
      std::move(rollback_store), defaults);
  Check(rollback.set(1, changed).result == OperationResult::InvalidState &&
            rollback.current().configuration == defaults &&
            rollback_view->values.empty() && !rollback_view->pending,
        "live apply failure aborts prepared persistence");

  auto commit_store = std::make_unique<Store>();
  auto* commit_view = commit_store.get();
  commit_view->fail_commit = true;
  std::vector<MateRuntimeConfiguration> applied_values;
  RuntimeConfigurationController commit_failure(
      [&](const MateRuntimeConfiguration& value) {
        applied_values.push_back(value);
        return OperationResult::Success;
      }, std::move(commit_store), defaults);
  Check(commit_failure.set(1, changed).result == OperationResult::InternalError &&
            applied_values.size() == 2 && applied_values.front() == changed &&
            applied_values.back() == defaults &&
            commit_failure.current().configuration == defaults &&
            !commit_view->pending && commit_view->restored == defaults,
        "commit failure rolls live configuration back");
}

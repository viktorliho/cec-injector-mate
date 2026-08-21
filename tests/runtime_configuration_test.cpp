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
  Check(parsed.ok() && parsed.configuration == defaults, "full round trip");
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

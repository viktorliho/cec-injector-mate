#include "cec_injector/mate/runtime_configuration_controller.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>

namespace cec_injector::mate {

bool FileRuntimeConfigurationStore::prepare(
    const MateRuntimeConfiguration& configuration, std::string& error) {
  abort();
  const auto directory = path_.parent_path();
  std::string temporary_name{"."};
  temporary_name.append(path_.filename().string());
  temporary_name.append(".pending");
  temporary_ = directory / temporary_name;
  const auto contents = serialize_runtime_configuration(configuration);
  const int descriptor = ::open(temporary_.c_str(),
                                O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC |
                                    O_NOFOLLOW,
                                0600);
  if (descriptor < 0) {
    error = "open pending configuration: " + std::string(std::strerror(errno));
    return false;
  }
  std::string_view remaining = contents;
  bool ok = true;
  while (!remaining.empty()) {
    const auto count = ::write(descriptor, remaining.data(), remaining.size());
    if (count < 0) {
      if (errno == EINTR) continue;
      error = "write pending configuration: " +
              std::string(std::strerror(errno));
      ok = false;
      break;
    }
    remaining.remove_prefix(static_cast<std::size_t>(count));
  }
  if (ok && ::fsync(descriptor) != 0) {
    error = "fsync pending configuration: " +
            std::string(std::strerror(errno));
    ok = false;
  }
  if (::close(descriptor) != 0 && ok) {
    error = "close pending configuration: " +
            std::string(std::strerror(errno));
    ok = false;
  }
  if (!ok) {
    abort();
    return false;
  }
  prepared_ = true;
  return true;
}

bool FileRuntimeConfigurationStore::commit(std::string& error) {
  if (!prepared_) {
    error = "no prepared runtime configuration";
    return false;
  }
  if (::rename(temporary_.c_str(), path_.c_str()) != 0) {
    error = "rename runtime configuration: " +
            std::string(std::strerror(errno));
    abort();
    return false;
  }
  prepared_ = false;
  temporary_.clear();
  const int descriptor = ::open(path_.parent_path().c_str(),
                                O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) {
    error = "open configuration directory: " +
            std::string(std::strerror(errno));
    return false;
  }
  const bool ok = ::fsync(descriptor) == 0;
  if (!ok) {
    error = "fsync configuration directory: " +
            std::string(std::strerror(errno));
  }
  ::close(descriptor);
  return ok;
}

bool FileRuntimeConfigurationStore::restore(
    const MateRuntimeConfiguration& configuration, std::string& error) {
  abort();
  return persist_runtime_configuration_atomic(path_, configuration, error);
}

void FileRuntimeConfigurationStore::abort() noexcept {
  if (!temporary_.empty()) {
    std::error_code ignored;
    std::filesystem::remove(temporary_, ignored);
  }
  temporary_.clear();
  prepared_ = false;
}

RuntimeConfigurationController::RuntimeConfigurationController(
    MateService& service, std::unique_ptr<RuntimeConfigurationStore> store,
    MateRuntimeConfiguration initial, ChangedSink changed)
    : RuntimeConfigurationController(
          [&service](const MateRuntimeConfiguration& value) {
            return service.apply_runtime_configuration(value);
          }, std::move(store), std::move(initial), std::move(changed)) {}

RuntimeConfigurationController::RuntimeConfigurationController(
    LiveApply live_apply, std::unique_ptr<RuntimeConfigurationStore> store,
    MateRuntimeConfiguration initial, ChangedSink changed)
    : live_apply_(std::move(live_apply)), store_(std::move(store)),
      changed_(std::move(changed)),
      current_{1, std::move(initial)} {}

VersionedRuntimeConfiguration RuntimeConfigurationController::current() const {
  std::lock_guard lock(mutex_);
  return current_;
}

void RuntimeConfigurationController::set_changed_sink(ChangedSink changed) {
  std::lock_guard lock(mutex_);
  changed_ = std::move(changed);
}

RuntimeConfigurationUpdateResult RuntimeConfigurationController::set(
    std::uint64_t expected_revision,
    const MateRuntimeConfiguration& requested) {
  std::lock_guard lock(mutex_);
  if (expected_revision != current_.revision) {
    return {OperationResult::Rejected, current_,
            "configuration revision conflict"};
  }
  if (requested == current_.configuration) {
    return {OperationResult::Success, current_, {}};
  }
  std::string error;
  if (!store_->prepare(requested, error)) {
    return {OperationResult::InternalError, current_, std::move(error)};
  }
  const auto applied = live_apply_(requested);
  if (applied != OperationResult::Success) {
    store_->abort();
    return {applied, current_, "live configuration apply failed"};
  }
  if (!store_->commit(error)) {
    const auto rolled_back = live_apply_(current_.configuration);
    std::string restore_error;
    const bool restored = store_->restore(current_.configuration, restore_error);
    store_->abort();
    if (rolled_back != OperationResult::Success || !restored) {
      return {OperationResult::InternalError, current_,
              "persistent commit failed; rollback incomplete: " + error +
                  (restore_error.empty() ? std::string{} :
                                           std::string("; restore: ") +
                                               restore_error)};
    }
    return {OperationResult::InternalError, current_, std::move(error)};
  }
  ++current_.revision;
  current_.configuration = requested;
  if (changed_) changed_(current_);
  return {OperationResult::Success, current_, {}};
}

}  // namespace cec_injector::mate

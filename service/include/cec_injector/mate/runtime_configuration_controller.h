#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "cec_injector/mate/mate_service.h"
#include "cec_injector/mate/runtime_configuration_io.h"

namespace cec_injector::mate {

struct VersionedRuntimeConfiguration {
  std::uint64_t revision{1};
  MateRuntimeConfiguration configuration;
  bool operator==(const VersionedRuntimeConfiguration&) const = default;
};

struct RuntimeConfigurationUpdateResult {
  OperationResult result{OperationResult::InternalError};
  VersionedRuntimeConfiguration authoritative;
  std::string message;
};

class RuntimeConfigurationStore {
 public:
  virtual ~RuntimeConfigurationStore() = default;
  virtual bool prepare(const MateRuntimeConfiguration& configuration,
                       std::string& error) = 0;
  virtual bool commit(std::string& error) = 0;
  virtual bool restore(const MateRuntimeConfiguration& configuration,
                       std::string& error) = 0;
  virtual void abort() noexcept = 0;
};

class FileRuntimeConfigurationStore final : public RuntimeConfigurationStore {
 public:
  explicit FileRuntimeConfigurationStore(std::filesystem::path path)
      : path_(std::move(path)) {}
  bool prepare(const MateRuntimeConfiguration& configuration,
               std::string& error) override;
  bool commit(std::string& error) override;
  bool restore(const MateRuntimeConfiguration& configuration,
               std::string& error) override;
  void abort() noexcept override;

 private:
  std::filesystem::path path_;
  std::filesystem::path temporary_;
  bool prepared_{};
};

class RuntimeConfigurationController final {
 public:
  using ChangedSink = std::function<void(const VersionedRuntimeConfiguration&)>;
  using LiveApply = std::function<OperationResult(const MateRuntimeConfiguration&)>;

  RuntimeConfigurationController(
      MateService& service, std::unique_ptr<RuntimeConfigurationStore> store,
      MateRuntimeConfiguration initial, ChangedSink changed = {});
  RuntimeConfigurationController(
      LiveApply live_apply, std::unique_ptr<RuntimeConfigurationStore> store,
      MateRuntimeConfiguration initial, ChangedSink changed = {});

  [[nodiscard]] VersionedRuntimeConfiguration current() const;
  void set_changed_sink(ChangedSink changed);
  RuntimeConfigurationUpdateResult set(
      std::uint64_t expected_revision,
      const MateRuntimeConfiguration& requested);

 private:
  LiveApply live_apply_;
  std::unique_ptr<RuntimeConfigurationStore> store_;
  ChangedSink changed_;
  mutable std::mutex mutex_;
  VersionedRuntimeConfiguration current_;
};

}  // namespace cec_injector::mate

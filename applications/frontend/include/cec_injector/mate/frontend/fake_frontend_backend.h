#pragma once

#include <mutex>

#include "cec_injector/mate/frontend/frontend_backend.h"

namespace cec_injector::mate::frontend {

class FakeFrontendBackend final : public MateFrontendBackend {
 public:
  FakeFrontendBackend();

  void set_observer(MateFrontendObserver* observer) override;
  [[nodiscard]] FrontendStatus status() const override;
  [[nodiscard]] FrontendCapabilities capabilities() const override;
  [[nodiscard]] FrontendConfiguration configuration() const override;
  OperationResult apply_configuration(
      const FrontendConfiguration& configuration) override;
  [[nodiscard]] FrontendActionRequestId request_action(
      FrontendAction action) override;

 private:
  mutable std::mutex mutex_;
  MateFrontendObserver* observer_{};
  FrontendStatus status_;
  FrontendCapabilities capabilities_;
  FrontendConfiguration configuration_;
  std::vector<FrontendCecEvent> events_;
  FrontendActionRequestId next_request_{1};
};

}  // namespace cec_injector::mate::frontend

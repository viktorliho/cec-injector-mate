#pragma once

#include "cec_injector/mate/frontend/frontend_backend.h"
#include "cec_injector/mate/mate_service.h"
#include "cec_injector/mate/runtime_configuration_controller.h"

namespace cec_injector::mate::frontend {

class FrontendStateProjector final {
 public:
  [[nodiscard]] static FrontendStatus status(
      const MateServiceSnapshot& source);
  [[nodiscard]] static FrontendConfigurationSnapshot configuration(
      const VersionedRuntimeConfiguration& source);
  [[nodiscard]] static FrontendCecEvent cec_event(
      const MateServiceCecEvent& source);
  [[nodiscard]] static FrontendActionResult action_result(
      const MateManualActionResult& source);
};

}  // namespace cec_injector::mate::frontend

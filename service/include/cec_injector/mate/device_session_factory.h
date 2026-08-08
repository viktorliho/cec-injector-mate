#pragma once

#include <memory>

#include "cec_injector/mate/device_session.h"
#include "cec_injector/mate/host_platform.h"

namespace cec_injector::mate {

// Service composition passes the complete discovery record without
// interpreting its platform-private locator. A platform/adapter-specific
// implementation resolves the locator using the inventory it owns.
class DeviceSessionFactory {
 public:
  virtual ~DeviceSessionFactory() = default;

  [[nodiscard]] virtual std::unique_ptr<DeviceSession> create_session(
      const DiscoveredDevice& device) = 0;
};

}  // namespace cec_injector::mate

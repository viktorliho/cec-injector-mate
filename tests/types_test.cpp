#include "cec_injector/mate/types.h"

#include <set>

#include "test_support.h"

using namespace cec_injector::mate;

int main() {
  const DeviceId invalid;
  const DeviceId first("device-a");
  const DeviceId same("device-a");
  const DeviceId second("device-b");

  CHECK(!invalid.valid());
  CHECK(first.valid());
  CHECK(first == same);
  CHECK(first != second);
  CHECK(first.diagnostic_value() == "device-a");

  const std::set<DeviceId> keys{second, first, same};
  CHECK(keys.size() == 2);

  CHECK(SessionState::Discovered != SessionState::Connected);
  CHECK(SessionState::Degraded != SessionState::Disconnected);
  CHECK(OperationResult::Success != OperationResult::Timeout);
  CHECK(OperationResult::PermissionDenied != OperationResult::Rejected);
  CHECK(SemanticRemoteAction::Up != SemanticRemoteAction::Down);
  CHECK(SemanticRemoteAction::PlayPause != SemanticRemoteAction::Stop);
  return EXIT_SUCCESS;
}

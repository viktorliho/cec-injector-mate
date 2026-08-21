#include "cec_injector/mate/frontend/frontend_state_projector.h"

namespace cec_injector::mate::frontend {

FrontendStatus FrontendStateProjector::status(const MateServiceSnapshot& source) {
  FrontendStatus result;
  result.service_available = source.running;
  result.injector_connected = source.session_state == SessionState::Connected;
  result.selected_device = source.selected_device;
  result.device_display_name = source.device_display_name;
  result.display_name = source.display.name;
  result.edid_available = source.display.edid_available;
  result.edid_byte_count = source.display.edid_byte_count;
  result.physical_address_status = source.physical_address_status;
  result.physical_address = source.physical_address;
  result.logical_address = source.device_state.addresses.logical;
  result.session_state = source.session_state;
  result.wake = source.device_state.wake;
  result.host_power_state = source.host_power_state;
  return result;
}

FrontendConfigurationSnapshot FrontendStateProjector::configuration(
    const VersionedRuntimeConfiguration& source) {
  return {source.revision, source.configuration};
}

FrontendCecEvent FrontendStateProjector::cec_event(
    const MateServiceCecEvent& source) {
  return {source.timestamp,
          source.received ? CecDirection::Received : CecDirection::Transmitted,
          source.source, source.destination, source.opcode, source.parameters,
          source.semantic};
}

FrontendActionResult FrontendStateProjector::action_result(
    const MateManualActionResult& source) {
  FrontendAction action = FrontendAction::PowerOnTv;
  switch (source.action) {
    case MateManualAction::PowerOnTv: action = FrontendAction::PowerOnTv; break;
    case MateManualAction::MakeActiveSource:
      action = FrontendAction::MakeActiveSource;
      break;
    case MateManualAction::StandbyTv: action = FrontendAction::StandbyTv; break;
  }
  return {action, source.result, source.duration, source.message};
}

}  // namespace cec_injector::mate::frontend

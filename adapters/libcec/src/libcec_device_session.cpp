#include "cec_injector/mate/libcec/libcec_device_session.h"
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "libcec_session_engine.h"
#include "wake_extension_client.h"
namespace cec_injector::mate::libcec {
OperationResult map_backend_result(BackendResult r){switch(r){
 case BackendResult::Success:return OperationResult::Success;case BackendResult::Unsupported:return OperationResult::Unsupported;
 case BackendResult::InvalidArgument:return OperationResult::InvalidConfiguration;case BackendResult::NotOpen:return OperationResult::InvalidState;
 case BackendResult::Rejected:return OperationResult::Rejected;case BackendResult::Timeout:return OperationResult::Timeout;
 case BackendResult::Disconnected:return OperationResult::Disconnected;case BackendResult::MalformedResponse:case BackendResult::BufferTooSmall:return OperationResult::MalformedResponse;
 case BackendResult::InternalError:return OperationResult::InternalError;}return OperationResult::InternalError;}
#ifndef CEC_INJECTOR_MATE_HAVE_REAL_LIBCEC
std::unique_ptr<LibCecApi> create_real_libcec_api(){return {};}
#endif
class LibCecDeviceSession::Impl final : public detail::SessionEventObserver {
 public:
  Impl(DeviceId i,DeviceSessionConfiguration c,LibCecSessionOptions o,LibCecApiFactory f)
      : id(std::move(i)),configuration(c),engine(id,std::move(o),std::move(f),*this){}
  void on_state_changed(SessionState s) override {DeviceSessionObserver* o;{std::lock_guard l(observer_mu);o=observer;}if(o)o->on_session_state_changed(id,s);}
  void on_external_cec_frame(std::span<const std::uint8_t> f) override {DeviceSessionObserver* o;{std::lock_guard l(observer_mu);o=observer;}if(o)o->on_external_cec_frame(id,f);}
  mutable std::mutex observer_mu;DeviceSessionObserver* observer{};DeviceId id;DeviceSessionConfiguration configuration;detail::LibCecSessionEngine engine;
};
LibCecDeviceSession::LibCecDeviceSession(DeviceId i,DeviceSessionConfiguration c,LibCecSessionOptions o,LibCecApiFactory f):impl_(std::make_unique<Impl>(std::move(i),c,std::move(o),std::move(f))){}
LibCecDeviceSession::~LibCecDeviceSession()=default;const DeviceId& LibCecDeviceSession::device_id()const noexcept{return impl_->id;}
SessionState LibCecDeviceSession::state()const noexcept{return impl_->engine.state();}void LibCecDeviceSession::set_observer(DeviceSessionObserver* o){std::lock_guard l(impl_->observer_mu);impl_->observer=o;}
OperationResult LibCecDeviceSession::connect(){if(!impl_->id.valid()||!impl_->configuration.valid())return OperationResult::InvalidConfiguration;
  return impl_->engine.connect([this](LibCecApi& api){return map_backend_result(api.initialize(impl_->configuration.physical_address));},
    [this](DeviceStateSnapshot& snapshot,LibCecApi&){snapshot.addresses.physical=impl_->configuration.physical_address;},
    std::chrono::milliseconds(12000));}OperationResult LibCecDeviceSession::disconnect(){return impl_->engine.disconnect();}
OperationResult LibCecDeviceSession::refresh_state(){return connect();}DeviceStateSnapshot LibCecDeviceSession::snapshot()const{return impl_->engine.snapshot();}
OperationResult LibCecDeviceSession::power_on_tv(){return impl_->engine.operation([](auto& a){return a.power_on_tv();});}OperationResult LibCecDeviceSession::standby_tv(){return impl_->engine.operation([](auto& a){return a.standby_broadcast();});}
OperationResult LibCecDeviceSession::make_active_source(){return impl_->engine.operation([](auto& a){return a.make_active_source();});}
WakeState LibCecDeviceSession::query_wake_state(){(void)impl_->engine.invoke([this]{auto* wake=impl_->engine.wake();if(!wake)return OperationResult::Disconnected;impl_->engine.set_wake_state(wake->query());return OperationResult::Success;});return impl_->engine.snapshot().wake;}
OperationResult LibCecDeviceSession::configure_wake(std::uint16_t p){auto r=impl_->engine.invoke([this,p]{auto* wake=impl_->engine.wake();if(!wake)return OperationResult::Disconnected;const auto result=wake->configure(p);impl_->engine.set_wake_state(wake->state());return result;});if(r==OperationResult::Success)impl_->engine.set_wake_physical_address(p);return r;}
OperationResult LibCecDeviceSession::arm_wake(){return impl_->engine.invoke([this]{auto* wake=impl_->engine.wake();if(!wake)return OperationResult::Disconnected;const auto result=wake->arm(impl_->engine.wake_physical_address());impl_->engine.set_wake_state(wake->state());return result;});}
OperationResult LibCecDeviceSession::disarm_wake(){return impl_->engine.invoke([this]{auto* wake=impl_->engine.wake();if(!wake)return OperationResult::Disconnected;const auto result=wake->disarm();impl_->engine.set_wake_state(wake->state());return result;});}
WakeReason LibCecDeviceSession::read_last_wake_reason(){auto out=std::make_shared<WakeReason>();auto r=impl_->engine.invoke([this,out]{auto* wake=impl_->engine.wake();if(!wake)return OperationResult::Disconnected;*out=wake->read_reason();impl_->engine.set_wake_state(wake->state());return out->result;});if(r!=out->result)return {.result=r};return *out;}
OperationResult LibCecDeviceSession::clear_last_wake_reason(){return impl_->engine.invoke([this]{auto* wake=impl_->engine.wake();if(!wake)return OperationResult::Disconnected;const auto result=wake->clear_reason();impl_->engine.set_wake_state(wake->state());return result;});}
}  // namespace cec_injector::mate::libcec

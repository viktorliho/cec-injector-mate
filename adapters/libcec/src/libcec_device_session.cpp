#include "cec_injector/mate/libcec/libcec_device_session.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
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
class LibCecDeviceSession::Impl {
 public:
  Impl(DeviceId i,DeviceSessionConfiguration c,LibCecSessionOptions o,LibCecApiFactory f):id(std::move(i)),configuration(c),options(std::move(o)),factory(std::move(f)),worker(&Impl::run,this){}
  ~Impl(){{std::lock_guard l(state_mu);observer=nullptr;}disconnect();{std::lock_guard l(mu);stopping=true;}cv.notify_all();worker.join();}
  using Task=std::packaged_task<OperationResult()>;
  OperationResult invoke(std::function<OperationResult()> fn,std::chrono::milliseconds timeout={}){Task t(std::move(fn));auto future=t.get_future();{
    std::lock_guard l(mu);if(stopping||tasks.size()>=8)return OperationResult::Rejected;tasks.push_back(std::move(t));}cv.notify_one();
    if(timeout==std::chrono::milliseconds::zero())timeout=options.operation_timeout;
    if(future.wait_for(timeout)!=std::future_status::ready)return OperationResult::Timeout;try{return future.get();}catch(...){return OperationResult::InternalError;}}
  void run(){for(;;){Task t;std::optional<Event> e;{std::unique_lock l(mu);cv.wait(l,[&]{return stopping||!tasks.empty()||!events.empty();});
    if(stopping&&tasks.empty()&&events.empty())break;if(!events.empty()){e=std::move(events.front());events.pop_front();}else{t=std::move(tasks.front());tasks.pop_front();}}
    if(e){dispatch(*e);}else t();}}
  struct Event{bool lost{};PortableCecFrame frame;};
  void enqueue(Event e){std::lock_guard l(mu);if(events.size()<32||e.lost){if(e.lost&&events.size()>=32)events.pop_front();events.push_back(std::move(e));cv.notify_one();}}
  void dispatch(const Event& e){if(e.lost){set_state(SessionState::Disconnected);return;}std::vector<std::uint8_t> bytes;
    bytes.push_back(static_cast<std::uint8_t>(e.frame.initiator<<4|e.frame.destination));if(e.frame.opcode)bytes.push_back(*e.frame.opcode);
    bytes.insert(bytes.end(),e.frame.parameters.begin(),e.frame.parameters.end());DeviceSessionObserver* o;{std::lock_guard l(state_mu);o=observer;}if(o)o->on_external_cec_frame(id,bytes);}
  void set_state(SessionState s){state_value=s;DeviceSessionObserver* o;{std::lock_guard l(state_mu);o=observer;}if(o)o->on_session_state_changed(id,s);}
  OperationResult connect(){if(!id.valid()||!configuration.valid()||options.explicit_port.empty())return OperationResult::InvalidConfiguration;
    set_state(SessionState::Connecting);auto result=invoke([this]{if(api)api->close();api=factory?factory():nullptr;if(!api)return OperationResult::Unsupported;
      api->set_disconnect_callback([this]{enqueue({.lost=true});});api->set_frame_callback([this](PortableCecFrame f){enqueue({.frame=std::move(f)});});
      auto r=map_backend_result(api->initialize(configuration.physical_address));if(r!=OperationResult::Success){api.reset();return r;}
      r=map_backend_result(api->open(options.explicit_port,std::chrono::milliseconds(5000)));if(r!=OperationResult::Success){api.reset();return r;}
      {std::lock_guard l(state_mu);snapshot_value.capabilities.cec=true;snapshot_value.addresses.physical=configuration.physical_address;snapshot_value.addresses.logical=api->logical_address();}
      wake=std::make_unique<detail::WakeExtensionClient>([this](auto c,auto q,auto& p,auto timeout){return api->project_transaction(c,q,p,timeout);});return OperationResult::Success;},std::chrono::milliseconds(12000));
    set_state(result==OperationResult::Success?SessionState::Connected:SessionState::Disconnected);return result;}
  OperationResult disconnect(){if(!worker.joinable())return OperationResult::Success;set_state(SessionState::Disconnecting);auto r=invoke([this]{if(api)api->close();api.reset();wake.reset();return OperationResult::Success;});set_state(SessionState::Disconnected);return r;}
  OperationResult op(std::function<BackendResult(LibCecApi&)> f){return invoke([this,f=std::move(f)]{if(!api||state_value!=SessionState::Connected)return OperationResult::Disconnected;return map_backend_result(f(*api));});}
  DeviceId id;DeviceSessionConfiguration configuration;LibCecSessionOptions options;LibCecApiFactory factory;std::unique_ptr<LibCecApi> api;std::unique_ptr<detail::WakeExtensionClient> wake;std::uint16_t wake_physical_address{0xffff};
  std::atomic<SessionState> state_value{SessionState::Discovered};mutable std::mutex state_mu;DeviceSessionObserver* observer{};DeviceStateSnapshot snapshot_value{};
  std::mutex mu;std::condition_variable cv;std::deque<Task> tasks;std::deque<Event> events;bool stopping{};std::thread worker;
};
LibCecDeviceSession::LibCecDeviceSession(DeviceId i,DeviceSessionConfiguration c,LibCecSessionOptions o,LibCecApiFactory f):impl_(std::make_unique<Impl>(std::move(i),c,std::move(o),std::move(f))){}
LibCecDeviceSession::~LibCecDeviceSession()=default;const DeviceId& LibCecDeviceSession::device_id()const noexcept{return impl_->id;}
SessionState LibCecDeviceSession::state()const noexcept{return impl_->state_value.load();}void LibCecDeviceSession::set_observer(DeviceSessionObserver* o){std::lock_guard l(impl_->state_mu);impl_->observer=o;}
OperationResult LibCecDeviceSession::connect(){return impl_->connect();}OperationResult LibCecDeviceSession::disconnect(){return impl_->disconnect();}
OperationResult LibCecDeviceSession::refresh_state(){return impl_->connect();}DeviceStateSnapshot LibCecDeviceSession::snapshot()const{std::lock_guard l(impl_->state_mu);return impl_->snapshot_value;}
OperationResult LibCecDeviceSession::power_on_tv(){return impl_->op([](auto& a){return a.power_on_tv();});}OperationResult LibCecDeviceSession::standby_tv(){return impl_->op([](auto& a){return a.standby_broadcast();});}
OperationResult LibCecDeviceSession::make_active_source(){return impl_->op([](auto& a){return a.make_active_source();});}
WakeState LibCecDeviceSession::query_wake_state(){impl_->invoke([this]{if(!impl_->wake)return OperationResult::Disconnected;auto s=impl_->wake->query();{std::lock_guard l(impl_->state_mu);impl_->snapshot_value.wake=s;}return OperationResult::Success;});return snapshot().wake;}
OperationResult LibCecDeviceSession::configure_wake(std::uint16_t p){auto r=impl_->invoke([this,p]{return impl_->wake?impl_->wake->configure(p):OperationResult::Disconnected;});if(r==OperationResult::Success)impl_->wake_physical_address=p;return r;}
OperationResult LibCecDeviceSession::arm_wake(){return impl_->invoke([this]{return impl_->wake?impl_->wake->arm(impl_->wake_physical_address):OperationResult::Disconnected;});}
OperationResult LibCecDeviceSession::disarm_wake(){return impl_->invoke([this]{return impl_->wake?impl_->wake->disarm():OperationResult::Disconnected;});}
WakeReason LibCecDeviceSession::read_last_wake_reason(){auto out=std::make_shared<WakeReason>();auto r=impl_->invoke([this,out]{if(!impl_->wake)return OperationResult::Disconnected;*out=impl_->wake->read_reason();return out->result;});if(r!=out->result)return {.result=r};return *out;}
OperationResult LibCecDeviceSession::clear_last_wake_reason(){return impl_->invoke([this]{return impl_->wake?impl_->wake->clear_reason():OperationResult::Disconnected;});}
}  // namespace cec_injector::mate::libcec

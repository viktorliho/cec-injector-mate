#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include "cec_injector/mate/libcec/libcec_device_session.h"
using namespace cec_injector::mate;using namespace cec_injector::mate::libcec;
namespace {
struct Fake final:LibCecApi{DisconnectCallback lost;FrameCallback frame;int opens{},closes{},actions{};bool short_reason{};std::chrono::milliseconds open_delay{},transaction_delay{};std::vector<std::uint8_t> commands;std::vector<std::uint16_t> initialized_addresses;std::vector<std::uint16_t>* initialization_log{};
 void set_disconnect_callback(DisconnectCallback x)override{lost=std::move(x);}void set_frame_callback(FrameCallback x)override{frame=std::move(x);}
 BackendResult initialize(std::uint16_t pa)override{initialized_addresses.push_back(pa);if(initialization_log)initialization_log->push_back(pa);return BackendResult::Success;}BackendResult open(std::string_view p,std::chrono::milliseconds)override{if(p!="opaque-port")return BackendResult::InvalidArgument;std::this_thread::sleep_for(open_delay);++opens;return BackendResult::Success;}
 void close()override{++closes;}std::optional<std::uint8_t> logical_address()const override{return 4;}BackendResult power_on_tv()override{++actions;return BackendResult::Success;}
 BackendResult standby_broadcast()override{++actions;return BackendResult::Rejected;}BackendResult make_active_source()override{++actions;return BackendResult::Success;}
 BackendResult project_transaction(std::uint8_t c,std::span<const std::uint8_t> q,std::vector<std::uint8_t>&r,std::chrono::milliseconds)override{std::this_thread::sleep_for(transaction_delay);commands.push_back(c);switch(c){
  case 0xe0:r={0xe0,1,1,0};break;case 0xe1:r={0x08,0xe1};break;case 0xe2:r={0xe2,1,1,1,0x10,0,1};break;case 0xe3:r={0x08,0xe3};break;case 0xe4:r={0x08,0xe4};break;
  case 0xe5:r.assign(32,0);r[0]=0xe5;r[1]=1;r[3]=7;r[4]=0x10;r[6]=1;break;case 0xe6:r=short_reason?std::vector<std::uint8_t>{0xe6,1,1}:std::vector<std::uint8_t>{0xe6,1,1,0,0,0,7,4,0x0f,0x86,0x10,0};break;case 0xe7:r={0x08,0xe7};break;}return BackendResult::Success;}
};
struct Observer:DeviceSessionObserver{int frames{},losses{};std::vector<std::uint8_t> last;void on_session_state_changed(const DeviceId&,SessionState s)override{if(s==SessionState::Disconnected)++losses;}
 void on_external_cec_frame(const DeviceId&,std::span<const std::uint8_t> f)override{++frames;last.assign(f.begin(),f.end());}void on_wake_state_changed(const DeviceId&,const WakeState&)override{}};
void check(bool x,const char*m){if(!x)throw m;}
}
int main(){try{Fake* raw{};std::vector<std::uint16_t> initialization_log;std::vector<std::string> diagnostics;LibCecDeviceSession s(DeviceId("device-a"),{0x2100},{"opaque-port",std::chrono::milliseconds(1000),[&](std::string_view x){diagnostics.emplace_back(x);}},[&]{auto p=std::make_unique<Fake>();p->initialization_log=&initialization_log;raw=p.get();return p;});Observer o;s.set_observer(&o);
 check(s.state()==SessionState::Discovered,"construction remains disconnected");check(initialization_log.empty(),"construction performs no device operation");
 check(s.connect()==OperationResult::Success,"connect");check(s.state()==SessionState::Connected,"state");check(s.snapshot().addresses.logical==4,"logical");check(raw->opens==1,"single open");
 check(raw->initialized_addresses==std::vector<std::uint16_t>({0x2100}),"configured physical address");
 check(s.power_on_tv()==OperationResult::Success,"power");check(s.standby_tv()==OperationResult::Rejected,"reject map");raw->frame({0,4,0x44,{1}});std::this_thread::sleep_for(std::chrono::milliseconds(10));check(o.last==std::vector<std::uint8_t>({0x04,0x44,1}),"frame copy");
 raw->transaction_delay=std::chrono::milliseconds(100);std::thread occupied([&]{(void)s.read_last_wake_reason();});std::this_thread::sleep_for(std::chrono::milliseconds(5));
 for(int i=0;i<32;++i)raw->frame({0,4,0x44,{static_cast<std::uint8_t>(i)}});raw->frame({0,0x0f,0x36,{}});occupied.join();std::this_thread::sleep_for(std::chrono::milliseconds(20));
 check(o.last==std::vector<std::uint8_t>({0x0f,0x36}),"standby prioritized at queue capacity");check(std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& x){return x.find("evicted=ordinary_for_external_tv_standby")!=std::string::npos;}),"queue pressure diagnostic");raw->transaction_delay={};
 check(s.configure_wake(0x1000)==OperationResult::Success,"config");check(s.arm_wake()==OperationResult::Success,"arm");raw->short_reason=true;check(s.read_last_wake_reason().result==OperationResult::MalformedResponse,"short reason");raw->short_reason=false;auto reason=s.read_last_wake_reason();check(reason.result==OperationResult::Success&&reason.cec_frame.size()==4,"reason");check(s.clear_last_wake_reason()==OperationResult::Success,"clear");check(s.disarm_wake()==OperationResult::Success,"disarm");
 raw->lost();std::this_thread::sleep_for(std::chrono::milliseconds(10));check(s.state()==SessionState::Disconnected,"loss");check(s.connect()==OperationResult::Success,"reopen");check(initialization_log==std::vector<std::uint16_t>({0x2100,0x2100}),"same physical address on reconnect");check(raw->actions==0,"no replay on reopen");check(s.disconnect()==OperationResult::Success,"close");
 check(map_backend_result(BackendResult::Unsupported)==OperationResult::Unsupported,"unsupported");check(map_backend_result(BackendResult::InvalidArgument)==OperationResult::InvalidConfiguration,"invalid");
 check(map_backend_result(BackendResult::NotOpen)==OperationResult::InvalidState,"not-open");check(map_backend_result(BackendResult::Timeout)==OperationResult::Timeout,"timeout");
 check(map_backend_result(BackendResult::Disconnected)==OperationResult::Disconnected,"disconnected");check(map_backend_result(BackendResult::MalformedResponse)==OperationResult::MalformedResponse,"malformed");
 s.set_observer(nullptr);Fake* slow{};{LibCecDeviceSession bounded(DeviceId("bounded"),{0x1000},{"opaque-port",std::chrono::milliseconds(1)},[&]{auto p=std::make_unique<Fake>();p->open_delay=std::chrono::milliseconds(5);slow=p.get();return p;});
 check(bounded.connect()==OperationResult::Success,"connect timeout budget");slow->transaction_delay=std::chrono::milliseconds(20);check(bounded.read_last_wake_reason().result==OperationResult::Timeout,"safe timed reason");}
 bool invalid_factory_called=false;LibCecDeviceSession invalid(DeviceId("invalid"),{0xffff},{"opaque-port",std::chrono::milliseconds(1)},[&]{invalid_factory_called=true;return std::make_unique<Fake>();});
 check(invalid.state()==SessionState::Discovered,"invalid remains disconnected");check(invalid.connect()==OperationResult::InvalidConfiguration,"invalid physical address");check(!invalid_factory_called,"invalid performs no device operation");
 LibCecDeviceSession invalid_id(DeviceId(),{0x1000},{"opaque-port",std::chrono::milliseconds(1)},[&]{invalid_factory_called=true;return std::make_unique<Fake>();});
 check(invalid_id.connect()==OperationResult::InvalidConfiguration,"invalid device id");check(!invalid_factory_called,"invalid id performs no device operation");
 std::cout<<"libCEC adapter tests passed\n";return 0;}catch(const char*e){std::cerr<<e<<'\n';return 1;}}

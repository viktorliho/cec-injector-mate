#include "wake_extension_client.h"
#include <array>
#include "cec_injector/mate/libcec/libcec_device_session.h"
namespace cec_injector::mate::libcec::detail {
namespace {
// Implementation values are bound to authoritative Device Wake Extension v1.
constexpr std::uint8_t E0=0xe0,E1=0xe1,E2=0xe2,E3=0xe3,E4=0xe4,E5=0xe5,E6=0xe6,E7=0xe7;
constexpr std::uint8_t version=1, set_stream_path=1;
constexpr auto timeout=std::chrono::milliseconds(600);
}
WakeExtensionClient::WakeExtensionClient(Transaction t):transaction_(std::move(t)){}
OperationResult WakeExtensionClient::call(std::uint8_t c,std::span<const std::uint8_t> q,
    std::vector<std::uint8_t>& r){return map_backend_result(transaction_(c,q,r,timeout));}
OperationResult WakeExtensionClient::detect(){std::vector<std::uint8_t> r;auto x=call(E0,{},r);
  if(x==OperationResult::Rejected)x=OperationResult::Unsupported;
  if(x!=OperationResult::Success){state_={};return x;}
  if(r.size()!=4||r[0]!=E0||r[1]!=version||(r[2]&1u)==0)return OperationResult::MalformedResponse;
  state_.supported=true;return OperationResult::Success;}
WakeState WakeExtensionClient::query(){if(detect()!=OperationResult::Success)return state_;
  std::vector<std::uint8_t> r;if(call(E5,{},r)!=OperationResult::Success||r.size()!=32||r[0]!=E5||r[1]!=version)return state_;
  state_.configured=(r[3]&3u)==3u;state_.armed=(r[3]&4u)!=0;state_.reason_available=r[27]!=0;return state_;}
OperationResult WakeExtensionClient::configure(std::uint16_t pa){auto x=detect();if(x!=OperationResult::Success)return x;
  const std::array<std::uint8_t,5> q{version,1,static_cast<std::uint8_t>(pa>>8),static_cast<std::uint8_t>(pa),set_stream_path};
  std::vector<std::uint8_t> r;x=call(E1,q,r);if(x!=OperationResult::Success)return x;
  if(r!=std::vector<std::uint8_t>({0x08,E1}))return OperationResult::MalformedResponse;r.clear();x=call(E2,{},r);
  if(x!=OperationResult::Success)return x;if(r.size()!=7||r[0]!=E2||r[1]!=version||r[2]==0||r[3]==0||
    static_cast<std::uint16_t>(r[4]<<8|r[5])!=pa||r[6]!=set_stream_path)return OperationResult::MalformedResponse;
  state_.supported=state_.configured=true;return OperationResult::Success;}
OperationResult WakeExtensionClient::arm(std::uint16_t pa){if(!state_.supported||!state_.configured)return OperationResult::InvalidState;
  std::vector<std::uint8_t> r;auto x=call(E3,{},r);if(x!=OperationResult::Success)return x;
  if(r!=std::vector<std::uint8_t>({0x08,E3}))return OperationResult::MalformedResponse;r.clear();x=call(E5,{},r);
  if(x!=OperationResult::Success)return x;if(r.size()!=32||r[0]!=E5||r[1]!=version||(r[3]&7u)!=7u||
    static_cast<std::uint16_t>(r[4]<<8|r[5])!=pa||r[6]!=set_stream_path)return OperationResult::MalformedResponse;
  state_.armed=true;return OperationResult::Success;}
OperationResult WakeExtensionClient::disarm(){auto x=detect();if(x!=OperationResult::Success)return x;
  std::vector<std::uint8_t> r;x=call(E4,{},r);if(x==OperationResult::Success&&r!=std::vector<std::uint8_t>({0x08,E4}))x=OperationResult::MalformedResponse;
  if(x==OperationResult::Success)state_.armed=false;return x;}
WakeReason WakeExtensionClient::read_reason(){std::vector<std::uint8_t> r;auto x=call(E6,{},r);if(x!=OperationResult::Success)return {.result=x};
  if(r.size()<8||r[0]!=E6||r[1]!=version||r.size()!=8u+r[7])return {.result=OperationResult::MalformedResponse};
  state_.reason_available=r[2]!=0;return {.result=OperationResult::Success,.cec_frame={r.begin()+8,r.end()}};}
OperationResult WakeExtensionClient::clear_reason(){std::vector<std::uint8_t> r;auto x=call(E7,{},r);
  if(x==OperationResult::Success&&r!=std::vector<std::uint8_t>({0x08,E7}))x=OperationResult::MalformedResponse;
  if(x==OperationResult::Success)state_.reason_available=false;return x;}
}  // namespace cec_injector::mate::libcec::detail

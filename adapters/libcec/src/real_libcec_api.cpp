#include "cec_injector/mate/libcec/libcec_api.h"
#include <array>
#include <cstring>
#include <libcec/cec.h>
extern "C" std::int32_t cec_project_adapter_transaction_v1(
    void*, std::uint8_t, const std::uint8_t*, std::uint32_t, std::uint8_t*,
    std::uint32_t, std::uint32_t*, std::uint32_t);
namespace cec_injector::mate::libcec {
namespace {
class RealApi final : public LibCecApi {
 public:
  ~RealApi() override { close(); if(adapter_)CECDestroy(adapter_); }
  void set_disconnect_callback(DisconnectCallback c)override{disconnect_=std::move(c);}
  void set_frame_callback(FrameCallback c)override{frame_=std::move(c);}
  BackendResult initialize(std::uint16_t pa)override{CEC::libcec_configuration c;c.Clear();c.clientVersion=CEC::LIBCEC_VERSION_CURRENT;
    std::strncpy(c.strDeviceName,"CEC Injector Mate",sizeof(c.strDeviceName)-1);c.deviceTypes.Clear();c.deviceTypes.Add(CEC::CEC_DEVICE_TYPE_PLAYBACK_DEVICE);
    c.iPhysicalAddress=pa;c.bAutodetectAddress=0;c.bActivateSource=0;c.bPowerOffOnStandby=0;
#if CEC_LIB_VERSION_MAJOR >= 5
    c.bAutoPowerOn=0;
#endif
    callbacks_.Clear();callbacks_.alert=&alert;callbacks_.commandReceived=&command;c.callbacks=&callbacks_;c.callbackParam=this;adapter_=CECInitialise(&c);return adapter_?BackendResult::Success:BackendResult::InternalError;}
  BackendResult open(std::string_view p,std::chrono::milliseconds t)override{std::string path(p);if(!adapter_)return BackendResult::NotOpen;
    if(!adapter_->Open(path.c_str(),static_cast<std::uint32_t>(t.count())))return BackendResult::Disconnected;logical_=static_cast<std::uint8_t>(adapter_->GetLogicalAddresses().primary);return BackendResult::Success;}
  void close()override{logical_.reset();if(adapter_)adapter_->Close();}
  std::optional<std::uint8_t> logical_address()const override{return logical_;}
  BackendResult power_on_tv()override{return boolean(adapter_&&adapter_->PowerOnDevices(CEC::CECDEVICE_TV));}
  BackendResult standby_broadcast()override{return boolean(adapter_&&adapter_->StandbyDevices(CEC::CECDEVICE_BROADCAST));}
  BackendResult make_active_source()override{return boolean(adapter_&&adapter_->SetActiveSource(CEC::CEC_DEVICE_TYPE_PLAYBACK_DEVICE));}
  BackendResult project_transaction(std::uint8_t c,std::span<const std::uint8_t> q,std::vector<std::uint8_t>& r,std::chrono::milliseconds t)override{
    if(!adapter_)return BackendResult::NotOpen;std::array<std::uint8_t,72>b{};std::uint32_t n{};auto x=cec_project_adapter_transaction_v1(adapter_,c,q.data(),q.size(),b.data(),b.size(),&n,t.count());
    if(n>b.size())return BackendResult::BufferTooSmall;r.assign(b.begin(),b.begin()+n);
    switch(x){case 0:return BackendResult::Success;case 1:return BackendResult::InvalidArgument;
      case 2:return BackendResult::Unsupported;case 3:return BackendResult::NotOpen;
      case 4:return BackendResult::Timeout;case 5:return BackendResult::Rejected;
      case 6:return BackendResult::MalformedResponse;case 7:return BackendResult::Disconnected;
      case 8:return BackendResult::BufferTooSmall;default:return BackendResult::InternalError;}}
 private:
  static BackendResult boolean(bool ok){return ok?BackendResult::Success:BackendResult::Rejected;}
  static void CEC_CDECL alert(void* p,CEC::libcec_alert a,const CEC::libcec_parameter){auto*self=static_cast<RealApi*>(p);if(self&&a==CEC::CEC_ALERT_CONNECTION_LOST&&self->disconnect_)self->disconnect_();}
  static void CEC_CDECL command(void* p,const CEC::cec_command* c){auto*self=static_cast<RealApi*>(p);if(!self||!c||!self->frame_)return;PortableCecFrame f{static_cast<std::uint8_t>(c->initiator),static_cast<std::uint8_t>(c->destination)};
    if(c->opcode_set)f.opcode=static_cast<std::uint8_t>(c->opcode);for(std::uint8_t i=0;i<c->parameters.size;++i)f.parameters.push_back(c->parameters[i]);self->frame_(std::move(f));}
  CEC::ICECAdapter* adapter_{};CEC::ICECCallbacks callbacks_;DisconnectCallback disconnect_;FrameCallback frame_;std::optional<std::uint8_t> logical_;
};}
std::unique_ptr<LibCecApi> create_real_libcec_api(){return std::make_unique<RealApi>();}
}  // namespace cec_injector::mate::libcec

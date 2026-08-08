#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cec_injector::mate::libcec {
enum class BackendResult { Success, Unsupported, InvalidArgument, NotOpen,
  Rejected, Timeout, Disconnected, MalformedResponse, BufferTooSmall, InternalError };
struct PortableCecFrame { std::uint8_t initiator{}, destination{};
  std::optional<std::uint8_t> opcode; std::vector<std::uint8_t> parameters; };
class LibCecApi {
 public:
  using DisconnectCallback = std::function<void()>;
  using FrameCallback = std::function<void(PortableCecFrame)>;
  virtual ~LibCecApi() = default;
  virtual void set_disconnect_callback(DisconnectCallback) = 0;
  virtual void set_frame_callback(FrameCallback) = 0;
  virtual BackendResult initialize(std::uint16_t physical_address) = 0;
  virtual BackendResult open(std::string_view explicit_port,
                             std::chrono::milliseconds timeout) = 0;
  virtual void close() = 0;
  [[nodiscard]] virtual std::optional<std::uint8_t> logical_address() const = 0;
  virtual BackendResult power_on_tv() = 0;
  virtual BackendResult standby_broadcast() = 0;
  virtual BackendResult make_active_source() = 0;
  virtual BackendResult project_transaction(std::uint8_t command,
      std::span<const std::uint8_t> request, std::vector<std::uint8_t>& response,
      std::chrono::milliseconds timeout) = 0;
};
using LibCecApiFactory = std::function<std::unique_ptr<LibCecApi>()>;
[[nodiscard]] std::unique_ptr<LibCecApi> create_real_libcec_api();
}  // namespace cec_injector::mate::libcec

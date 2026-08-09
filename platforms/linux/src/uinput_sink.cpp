#include "cec_injector/mate/linux/linux_remote_input.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace cec_injector::mate::linux_platform {
namespace {

std::uint16_t LinuxCode(LinuxInputKey key) {
  switch (key) {
    case LinuxInputKey::Up: return KEY_UP;
    case LinuxInputKey::Down: return KEY_DOWN;
    case LinuxInputKey::Left: return KEY_LEFT;
    case LinuxInputKey::Right: return KEY_RIGHT;
    case LinuxInputKey::Enter: return KEY_ENTER;
    case LinuxInputKey::Escape: return KEY_ESC;
    case LinuxInputKey::Info: return KEY_INFO;
    case LinuxInputKey::Play: return KEY_PLAY;
    case LinuxInputKey::Pause: return KEY_PAUSE;
    case LinuxInputKey::PlayPause: return KEY_PLAYPAUSE;
    case LinuxInputKey::Stop: return KEY_STOP;
    case LinuxInputKey::NextSong: return KEY_NEXTSONG;
    case LinuxInputKey::PreviousSong: return KEY_PREVIOUSSONG;
    case LinuxInputKey::LeftControl: return KEY_LEFTCTRL;
    case LinuxInputKey::LeftShift: return KEY_LEFTSHIFT;
    case LinuxInputKey::Tab: return KEY_TAB;
  }
  return KEY_RESERVED;
}

OperationResult ErrorResult() {
  return errno == EACCES || errno == EPERM ? OperationResult::PermissionDenied
                                           : OperationResult::InternalError;
}

class UinputSink final : public LinuxInputSink {
 public:
  explicit UinputSink(std::filesystem::path endpoint)
      : endpoint_(std::move(endpoint)) {}
  ~UinputSink() override { destroy(); }

  OperationResult create(
      const std::set<LinuxInputKey>& capabilities) override {
    if (fd_ >= 0) return OperationResult::Success;
    if (capabilities.empty()) return OperationResult::Unsupported;
    fd_ = open(endpoint_.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) return ErrorResult();
    if (ioctl(fd_, UI_SET_EVBIT, EV_KEY) < 0) return fail_create();
    for (const auto key : capabilities) {
      if (ioctl(fd_, UI_SET_KEYBIT, LinuxCode(key)) < 0) return fail_create();
    }
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.version = 1;
    std::strncpy(setup.name, "CEC Injector Mate Remote",
                 UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd_, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(fd_, UI_DEV_CREATE) < 0) {
      return fail_create();
    }
    created_ = true;
    return OperationResult::Success;
  }

  OperationResult emit(const LinuxInputEvent& input) override {
    if (fd_ < 0 || !created_) return OperationResult::InvalidState;
    input_event event{};
    if (input.kind == LinuxInputEventKind::Key) {
      event.type = EV_KEY;
      event.code = LinuxCode(input.key);
      event.value = input.pressed ? 1 : 0;
    } else {
      event.type = EV_SYN;
      event.code = SYN_REPORT;
    }
    if (!write_event(event)) return ErrorResult();
    return OperationResult::Success;
  }

  void destroy() noexcept override {
    if (fd_ < 0) return;
    if (created_) (void)ioctl(fd_, UI_DEV_DESTROY);
    close(fd_);
    fd_ = -1;
    created_ = false;
  }

 private:
  OperationResult fail_create() {
    const auto result = ErrorResult();
    destroy();
    return result;
  }

  bool write_event(const input_event& event) {
    return write(fd_, &event, sizeof(event)) ==
           static_cast<ssize_t>(sizeof(event));
  }

  std::filesystem::path endpoint_;
  int fd_{-1};
  bool created_{};
};

}  // namespace

std::unique_ptr<LinuxInputSink> create_linux_uinput_sink(
    std::filesystem::path endpoint) {
  return std::make_unique<UinputSink>(std::move(endpoint));
}

}  // namespace cec_injector::mate::linux_platform

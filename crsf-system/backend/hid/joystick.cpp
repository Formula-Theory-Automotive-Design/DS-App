#include "joystick.h"

#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace backend {

Joystick::~Joystick() {
    close();
}

bool Joystick::open(const std::string& device) {
    close();

    fd_ = ::open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        error_ = std::string("open(") + device + "): " + std::strerror(errno);
        return false;
    }

    // A real joystick answers JSIOCGAXES; anything else (a regular file,
    // /dev/null, ...) is not a controller, so reject it.
    __u8 axes = 0, buttons = 0;
    if (ioctl(fd_, JSIOCGAXES, &axes) < 0) {
        error_ = std::string("not a joystick device: ") + device;
        close();
        return false;
    }
    ioctl(fd_, JSIOCGBUTTONS, &buttons);

    char name[128] = {0};
    name_ = (ioctl(fd_, JSIOCGNAME(sizeof(name)), name) >= 0) ? name : "unknown";

    state_ = JoystickState{};
    state_.numAxes    = axes;
    state_.numButtons = buttons;
    state_.connected  = true;
    error_.clear();
    return true;
}

void Joystick::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    state_.connected = false;
}

bool Joystick::poll() {
    if (fd_ < 0) return false;

    struct js_event ev;
    for (;;) {
        ssize_t n = ::read(fd_, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            // The JS_EVENT_INIT flag marks synthetic startup events; we treat
            // them the same as real ones (they prime the initial state).
            switch (ev.type & ~JS_EVENT_INIT) {
                case JS_EVENT_AXIS:
                    if (ev.number < JoystickState::kMaxAxes) {
                        state_.axes[ev.number] =
                            static_cast<float>(ev.value) / 32767.0f;
                    }
                    break;
                case JS_EVENT_BUTTON:
                    if (ev.number < JoystickState::kMaxButtons) {
                        state_.buttons[ev.number] = (ev.value != 0);
                    }
                    break;
                default:
                    break;
            }
            continue;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;  // drained, all good
            }
            if (errno == EINTR) {
                continue;
            }
            // ENODEV etc.: device went away.
            error_ = std::string("read: ") + std::strerror(errno);
            close();
            return false;
        }
        // Short read (shouldn't happen for js_event): stop draining.
        return true;
    }
}

}  // namespace backend

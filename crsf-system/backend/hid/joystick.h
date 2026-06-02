#pragma once

#include <cstddef>
#include <string>

namespace backend {

// Snapshot of a joystick/gamepad's inputs.
//
// Axes are normalized to [-1, 1]; buttons are pressed/released. Sized to the
// plan's contract (8 axes, 32 buttons); the actual device may expose fewer,
// reported via numAxes/numButtons.
struct JoystickState {
    static constexpr std::size_t kMaxAxes    = 8;
    static constexpr std::size_t kMaxButtons = 32;

    float axes[kMaxAxes]      = {0.0f};
    bool  buttons[kMaxButtons] = {false};
    int   numAxes    = 0;
    int   numButtons = 0;
    bool  connected  = false;
};

// Reads a Linux joystick (evdev js) device, e.g. /dev/input/js0.
//
// Implementation note: the plan named hidapi, but on Linux the kernel joystick
// API already delivers de-bounced, normalized axis/button *events* — hidapi
// would hand us raw HID reports to parse per-device. This keeps the backend
// dependency-free (no hidapi-devel) while exposing the same JoystickState the
// plan specified. The class is a drop-in to swap for a hidapi backend later.
class Joystick {
public:
    Joystick() = default;
    ~Joystick();

    Joystick(const Joystick&) = delete;
    Joystick& operator=(const Joystick&) = delete;

    // Open the device (non-blocking). Returns false if it can't be opened
    // (e.g. nothing plugged in); lastError() holds the reason.
    bool open(const std::string& device);
    void close();
    bool isOpen() const { return fd_ >= 0; }

    // Drain all pending events and fold them into the internal state.
    // Non-blocking; returns false if the device disappeared (unplugged).
    bool poll();

    const JoystickState& state() const { return state_; }
    const std::string&   name() const { return name_; }
    const std::string&   lastError() const { return error_; }

private:
    int          fd_ = -1;
    JoystickState state_;
    std::string  name_;
    std::string  error_;
};

}  // namespace backend

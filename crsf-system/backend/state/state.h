#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

#include "crsf_telemetry.h"
#include "hid/joystick.h"

namespace backend {

// The single source of truth shared between the serial RX thread, the HID
// poll thread, and the WebSocket push thread.
struct SystemState {
    crsf::LinkStats   link;
    crsf::BatteryData battery;
    crsf::GPSData     gps;
    JoystickState     joystick;

    // "have we ever received one of these?" flags, so the frontend can tell
    // real zeros from missing telemetry.
    bool hasLink    = false;
    bool hasBattery = false;
    bool hasGps     = false;

    std::uint32_t packetCount = 0;
    std::uint32_t crcErrors   = 0;
};

// Thread-safe wrapper. Mutate under update(), read a consistent copy via
// snapshot(). A single mutex is plenty for the handful of writers here.
class StateManager {
public:
    template <class Fn>
    void update(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::forward<Fn>(fn)(state_);
    }

    SystemState snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

private:
    mutable std::mutex mutex_;
    SystemState        state_;
};

}  // namespace backend

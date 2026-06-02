#pragma once

#include <cstdint>
#include <vector>

#include "crsf_encoder.h"
#include "crsf_packets.h"
#include "crsf_telemetry.h"

namespace app {

// Control logic for the ESP32 node.
//
// Deliberately free of ESP-IDF / FreeRTOS dependencies: it consumes decoded
// CRSF packets and produces channel outputs + telemetry frames, taking the
// current time as a parameter rather than reading a clock. That keeps it pure
// (host-testable) and mirrors the project rule — crsf-core is the product,
// everything around it is an adapter.
class Control {
public:
    // Fold a freshly-parsed packet into the control state. `now_ms` is a
    // monotonic millisecond timestamp supplied by the caller.
    void onPacket(const crsf::CRSFPacket& pkt, std::uint32_t now_ms);

    // Latest received stick/switch positions (11-bit CRSF values).
    const crsf::RCChannels& channels() const { return channels_; }

    // True until the first RC frame arrives, and again whenever no RC frame has
    // been seen for `timeout_ms`. Drive outputs to a safe state when set.
    bool failsafe(std::uint32_t now_ms, std::uint32_t timeout_ms = 500) const;

    bool                   hasLink() const { return has_link_; }
    const crsf::LinkStats& link() const { return link_; }
    std::uint32_t          rcFrameCount() const { return rc_count_; }

    // Encode an upstream telemetry frame (example: battery) to write back out
    // the UART using the shared encoder.
    std::vector<std::uint8_t> buildBatteryTelemetry(const crsf::BatteryData& b) const {
        return packer_.encodeBattery(b);
    }

private:
    crsf::RCChannels channels_;
    crsf::LinkStats  link_{};
    bool             has_link_   = false;
    bool             got_rc_     = false;
    std::uint32_t    last_rc_ms_ = 0;
    std::uint32_t    rc_count_   = 0;

    // Telemetry we originate is addressed to the flight controller / host.
    crsf::CRSFPacker packer_{
        static_cast<std::uint8_t>(crsf::CRSFAddress::FlightController)};
};

}  // namespace app

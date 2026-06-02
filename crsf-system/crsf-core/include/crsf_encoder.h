#pragma once

#include "crsf_packets.h"
#include "crsf_telemetry.h"

namespace crsf {

// ---------------------------------------------------------------------------
// RC channel bit packing (the part where most implementations get bugs)
// ---------------------------------------------------------------------------
//
// 16 channels of 11 bits each are packed LSB-first into 22 bytes. packChannels
// and unpackChannels are exact inverses.

void packChannels(const RCChannels& in, std::uint8_t out[CRSF_RC_CHANNELS_PAYLOAD]);
void unpackChannels(const std::uint8_t in[CRSF_RC_CHANNELS_PAYLOAD], RCChannels& out);

// Convenience conversions between microseconds and 11-bit CRSF ticks.
std::uint16_t usToTicks(std::uint16_t microseconds);
std::uint16_t ticksToUs(std::uint16_t ticks);

// ---------------------------------------------------------------------------
// Frame encoder
// ---------------------------------------------------------------------------
//
// Builds complete on-the-wire frames (address + len + type + payload + crc).
class CRSFPacker {
public:
    explicit CRSFPacker(std::uint8_t address =
                            static_cast<std::uint8_t>(CRSFAddress::FlightController));

    void setAddress(std::uint8_t address) { address_ = address; }
    std::uint8_t address() const { return address_; }

    // Generic frame builder.
    std::vector<std::uint8_t> encodeFrame(CRSFType type,
                                          const std::uint8_t* payload,
                                          std::size_t len) const;

    // Typed encoders.
    std::vector<std::uint8_t> encodeRCChannels(const RCChannels& channels) const;
    std::vector<std::uint8_t> encodeBattery(const BatteryData& battery) const;
    std::vector<std::uint8_t> encodeGPS(const GPSData& gps) const;
    std::vector<std::uint8_t> encodeLinkStats(const LinkStats& stats) const;

private:
    std::uint8_t address_;
};

}  // namespace crsf

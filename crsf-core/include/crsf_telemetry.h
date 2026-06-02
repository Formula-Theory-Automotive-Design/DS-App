#pragma once

#include "crsf_packets.h"

namespace crsf {

// Decoded telemetry structures (engineering units, host byte order).
// On the wire CRSF telemetry is big-endian and fixed-point; the decode/encode
// helpers below do the conversion.

struct BatteryData {
    float         voltage    = 0.0f;  // volts
    float         current    = 0.0f;  // amperes
    std::uint32_t capacity   = 0;     // mAh consumed
    std::uint8_t  remaining  = 0;     // percent 0..100
};

struct GPSData {
    double        latitude    = 0.0;  // degrees
    double        longitude   = 0.0;  // degrees
    float         groundSpeed = 0.0f; // km/h
    float         heading     = 0.0f; // degrees
    float         altitude    = 0.0f; // meters
    std::uint8_t  satellites  = 0;
};

struct LinkStats {
    std::uint8_t uplinkRSSI1   = 0;  // dBm * -1
    std::uint8_t uplinkRSSI2   = 0;  // dBm * -1
    std::uint8_t uplinkLQ      = 0;  // percent
    std::int8_t  uplinkSNR     = 0;  // dB
    std::uint8_t activeAntenna = 0;
    std::uint8_t rfMode        = 0;
    std::uint8_t uplinkTxPower = 0;  // enum index
    std::uint8_t downlinkRSSI  = 0;  // dBm * -1
    std::uint8_t downlinkLQ    = 0;  // percent
    std::int8_t  downlinkSNR   = 0;  // dB
};

// Decoders: return false if the packet type/length does not match.
bool decodeBattery(const CRSFPacket& pkt, BatteryData& out);
bool decodeGPS(const CRSFPacket& pkt, GPSData& out);
bool decodeLinkStats(const CRSFPacket& pkt, LinkStats& out);

}  // namespace crsf

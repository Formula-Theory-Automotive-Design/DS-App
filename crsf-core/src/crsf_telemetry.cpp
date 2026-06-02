#include "crsf_telemetry.h"

namespace crsf {

namespace {
std::uint16_t get_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
std::int32_t get_i32(const std::uint8_t* p) {
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(p[0]) << 24) |
        (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) |
        (static_cast<std::uint32_t>(p[3])));
}
}  // namespace

bool decodeBattery(const CRSFPacket& pkt, BatteryData& out) {
    if (pkt.type != CRSFType::BatterySensor || pkt.payload.size() < 8) {
        return false;
    }
    const std::uint8_t* p = pkt.payload.data();
    out.voltage   = get_u16(p) / 10.0f;
    out.current   = get_u16(p + 2) / 10.0f;
    out.capacity  = (static_cast<std::uint32_t>(p[4]) << 16) |
                    (static_cast<std::uint32_t>(p[5]) << 8) |
                    static_cast<std::uint32_t>(p[6]);
    out.remaining = p[7];
    return true;
}

bool decodeGPS(const CRSFPacket& pkt, GPSData& out) {
    if (pkt.type != CRSFType::GPS || pkt.payload.size() < 15) {
        return false;
    }
    const std::uint8_t* p = pkt.payload.data();
    out.latitude    = get_i32(p) / 1e7;
    out.longitude   = get_i32(p + 4) / 1e7;
    out.groundSpeed = get_u16(p + 8) / 10.0f;
    out.heading     = get_u16(p + 10) / 100.0f;
    out.altitude    = static_cast<float>(get_u16(p + 12)) - 1000.0f;
    out.satellites  = p[14];
    return true;
}

bool decodeLinkStats(const CRSFPacket& pkt, LinkStats& out) {
    if (pkt.type != CRSFType::LinkStatistics || pkt.payload.size() < 10) {
        return false;
    }
    const std::uint8_t* p = pkt.payload.data();
    out.uplinkRSSI1   = p[0];
    out.uplinkRSSI2   = p[1];
    out.uplinkLQ      = p[2];
    out.uplinkSNR     = static_cast<std::int8_t>(p[3]);
    out.activeAntenna = p[4];
    out.rfMode        = p[5];
    out.uplinkTxPower = p[6];
    out.downlinkRSSI  = p[7];
    out.downlinkLQ    = p[8];
    out.downlinkSNR   = static_cast<std::int8_t>(p[9]);
    return true;
}

}  // namespace crsf

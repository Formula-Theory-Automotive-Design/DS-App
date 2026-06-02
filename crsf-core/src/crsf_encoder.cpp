#include "crsf_encoder.h"

#include "crsf_crc.h"

namespace crsf {

// ---------------------------------------------------------------------------
// RC channel packing
// ---------------------------------------------------------------------------

void packChannels(const RCChannels& in, std::uint8_t out[CRSF_RC_CHANNELS_PAYLOAD]) {
    std::uint32_t bits      = 0;
    int           bit_count = 0;
    std::size_t   o         = 0;

    for (std::size_t ch = 0; ch < CRSF_NUM_CHANNELS; ++ch) {
        bits |= static_cast<std::uint32_t>(in.channels[ch] & CRSF_CHANNEL_VALUE_MASK)
                << bit_count;
        bit_count += 11;
        while (bit_count >= 8) {
            out[o++] = static_cast<std::uint8_t>(bits & 0xFF);
            bits >>= 8;
            bit_count -= 8;
        }
    }
    // 16 * 11 = 176 bits is a whole number of bytes, so nothing is left over.
}

void unpackChannels(const std::uint8_t in[CRSF_RC_CHANNELS_PAYLOAD], RCChannels& out) {
    std::uint32_t bits      = 0;
    int           bit_count = 0;
    std::size_t   i         = 0;

    for (std::size_t ch = 0; ch < CRSF_NUM_CHANNELS; ++ch) {
        while (bit_count < 11) {
            bits |= static_cast<std::uint32_t>(in[i++]) << bit_count;
            bit_count += 8;
        }
        out.channels[ch] = static_cast<std::uint16_t>(bits & CRSF_CHANNEL_VALUE_MASK);
        bits >>= 11;
        bit_count -= 11;
    }
}

// CRSF ticks <-> microseconds. 992 ticks == 1500 us; 5 us per 8 ticks.
std::uint16_t usToTicks(std::uint16_t microseconds) {
    int ticks = (static_cast<int>(microseconds) - 1500) * 8 / 5 + CRSF_CHANNEL_VALUE_MID;
    if (ticks < 0) ticks = 0;
    if (ticks > CRSF_CHANNEL_VALUE_MASK) ticks = CRSF_CHANNEL_VALUE_MASK;
    return static_cast<std::uint16_t>(ticks);
}

std::uint16_t ticksToUs(std::uint16_t ticks) {
    return static_cast<std::uint16_t>(
        (static_cast<int>(ticks) - CRSF_CHANNEL_VALUE_MID) * 5 / 8 + 1500);
}

// ---------------------------------------------------------------------------
// Big-endian write helpers
// ---------------------------------------------------------------------------
namespace {
void put_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}
void put_u24(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}
void put_i32(std::vector<std::uint8_t>& v, std::int32_t x) {
    const std::uint32_t u = static_cast<std::uint32_t>(x);
    v.push_back(static_cast<std::uint8_t>((u >> 24) & 0xFF));
    v.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFF));
    v.push_back(static_cast<std::uint8_t>((u >> 8) & 0xFF));
    v.push_back(static_cast<std::uint8_t>(u & 0xFF));
}
}  // namespace

// ---------------------------------------------------------------------------
// CRSFPacker
// ---------------------------------------------------------------------------

CRSFPacker::CRSFPacker(std::uint8_t address) : address_(address) {}

std::vector<std::uint8_t> CRSFPacker::encodeFrame(CRSFType type,
                                                  const std::uint8_t* payload,
                                                  std::size_t len) const {
    std::vector<std::uint8_t> frame;
    frame.reserve(len + 4);

    const std::uint8_t len_field = static_cast<std::uint8_t>(len + 2);  // type + crc
    frame.push_back(address_);
    frame.push_back(len_field);
    frame.push_back(static_cast<std::uint8_t>(type));
    for (std::size_t i = 0; i < len; ++i) {
        frame.push_back(payload[i]);
    }

    // CRC over [type .. payload].
    std::uint8_t crc = 0;
    for (std::size_t i = 2; i < frame.size(); ++i) {
        crc = crc8_update(crc, frame[i]);
    }
    frame.push_back(crc);
    return frame;
}

std::vector<std::uint8_t> CRSFPacker::encodeRCChannels(const RCChannels& channels) const {
    std::uint8_t payload[CRSF_RC_CHANNELS_PAYLOAD];
    packChannels(channels, payload);
    return encodeFrame(CRSFType::RCChannelsPacked, payload, sizeof(payload));
}

std::vector<std::uint8_t> CRSFPacker::encodeBattery(const BatteryData& battery) const {
    std::vector<std::uint8_t> p;
    p.reserve(8);
    put_u16(p, static_cast<std::uint16_t>(battery.voltage * 10.0f + 0.5f));   // dV
    put_u16(p, static_cast<std::uint16_t>(battery.current * 10.0f + 0.5f));   // dA
    put_u24(p, battery.capacity);                                            // mAh
    p.push_back(battery.remaining);
    return encodeFrame(CRSFType::BatterySensor, p.data(), p.size());
}

std::vector<std::uint8_t> CRSFPacker::encodeGPS(const GPSData& gps) const {
    std::vector<std::uint8_t> p;
    p.reserve(15);
    put_i32(p, static_cast<std::int32_t>(gps.latitude * 1e7));
    put_i32(p, static_cast<std::int32_t>(gps.longitude * 1e7));
    put_u16(p, static_cast<std::uint16_t>(gps.groundSpeed * 10.0f + 0.5f));   // km/h*10
    put_u16(p, static_cast<std::uint16_t>(gps.heading * 100.0f + 0.5f));      // deg*100
    put_u16(p, static_cast<std::uint16_t>(gps.altitude + 1000.0f + 0.5f));    // m + 1000
    p.push_back(gps.satellites);
    return encodeFrame(CRSFType::GPS, p.data(), p.size());
}

std::vector<std::uint8_t> CRSFPacker::encodeLinkStats(const LinkStats& s) const {
    std::uint8_t p[10] = {
        s.uplinkRSSI1, s.uplinkRSSI2, s.uplinkLQ,
        static_cast<std::uint8_t>(s.uplinkSNR),
        s.activeAntenna, s.rfMode, s.uplinkTxPower,
        s.downlinkRSSI, s.downlinkLQ,
        static_cast<std::uint8_t>(s.downlinkSNR),
    };
    return encodeFrame(CRSFType::LinkStatistics, p, sizeof(p));
}

}  // namespace crsf

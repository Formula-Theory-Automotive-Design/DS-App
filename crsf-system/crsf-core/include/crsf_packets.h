#pragma once

// crsf-core — pure CRSF protocol definitions.
//
// DESIGN RULE: this header (and everything in crsf-core) must NOT touch
// UART, HID, threads, sockets, or any OS API. It only describes bytes and
// packets. The same files compile unchanged on desktop Linux and on ESP32.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace crsf {

// ---------------------------------------------------------------------------
// Frame geometry
// ---------------------------------------------------------------------------
//
// Wire format of a CRSF frame:
//
//   +---------+--------+--------+------------------+--------+
//   | address |  len   |  type  |     payload      |  crc8  |
//   +---------+--------+--------+------------------+--------+
//        0        1        2        3 .. (len)       len+1
//
//   - address : destination device address (sync byte).
//   - len     : number of bytes that FOLLOW this byte == sizeof(type) +
//               sizeof(payload) + sizeof(crc). Range 2..62.
//   - type    : packet type id.
//   - payload : 0..60 bytes.
//   - crc8    : CRC8/DVB-S2 (poly 0xD5) computed over [type .. payload],
//               i.e. it does NOT include address or len.

constexpr std::size_t CRSF_MAX_FRAME_SIZE   = 64;  // address + len + 62
constexpr std::size_t CRSF_MAX_PAYLOAD_SIZE = 60;
constexpr std::size_t CRSF_MIN_LEN_VALUE    = 2;   // type + crc, empty payload
constexpr std::size_t CRSF_MAX_LEN_VALUE    = CRSF_MAX_FRAME_SIZE - 2;

// RC channels.
constexpr std::size_t CRSF_NUM_CHANNELS         = 16;
constexpr std::size_t CRSF_RC_CHANNELS_PAYLOAD  = 22;  // 16 * 11 bits = 176 bits

// 11-bit channel value envelope (these match the ELRS/CRSF convention).
constexpr std::uint16_t CRSF_CHANNEL_VALUE_MIN  = 172;   // ~988 us
constexpr std::uint16_t CRSF_CHANNEL_VALUE_MID  = 992;   // ~1500 us
constexpr std::uint16_t CRSF_CHANNEL_VALUE_MAX  = 1811;  // ~2012 us
constexpr std::uint16_t CRSF_CHANNEL_VALUE_MASK = 0x07FF;

// ---------------------------------------------------------------------------
// Device addresses (the frame's first byte / "sync" byte)
// ---------------------------------------------------------------------------
enum class CRSFAddress : std::uint8_t {
    Broadcast        = 0x00,
    FlightController = 0xC8,
    Radio            = 0xEA,  // RadioMaster/EdgeTX handset
    Receiver         = 0xEC,
    Transmitter      = 0xEE,  // CRSF TX module (e.g. internal ELRS)
};

// ---------------------------------------------------------------------------
// Packet types
// ---------------------------------------------------------------------------
//
// NOTE on the plan's enum: the plan listed TELEMETRY = 0x1C, but 0x1C is the
// receiver link-statistics frame; there is no single generic "telemetry"
// type on the wire — telemetry is a family of typed frames. The names below
// use the real CRSF type ids. Plan equivalents are noted in comments.
enum class CRSFType : std::uint8_t {
    GPS               = 0x02,  // plan: GPS
    Vario             = 0x07,
    BatterySensor     = 0x08,  // plan: BATTERY
    Baro              = 0x09,
    LinkStatistics    = 0x14,
    RCChannelsPacked  = 0x16,  // plan: RC_CHANNELS
    LinkStatisticsRx  = 0x1C,  // plan: TELEMETRY (actually RX link stats)
    LinkStatisticsTx  = 0x1D,
    Attitude          = 0x1E,
    FlightMode        = 0x21,
    DevicePing        = 0x28,
    DeviceInfo        = 0x29,
    ParameterEntry    = 0x2B,
    ParameterRead     = 0x2C,
    ParameterWrite    = 0x2D,
    Command           = 0x32,
};

// ---------------------------------------------------------------------------
// A decoded frame
// ---------------------------------------------------------------------------
struct CRSFPacket {
    std::uint8_t          address = 0;             // sync byte the frame arrived on
    CRSFType              type    = CRSFType::RCChannelsPacked;
    std::vector<std::uint8_t> payload;             // bytes between type and crc
};

// ---------------------------------------------------------------------------
// RC channels (decoded form)
// ---------------------------------------------------------------------------
struct RCChannels {
    std::uint16_t channels[CRSF_NUM_CHANNELS] = {
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
        CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MID,  // throttle defaults low
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
        CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MID,
    };
};

// Returns true if `address` is a known CRSF device address.
bool isKnownAddress(std::uint8_t address);

}  // namespace crsf

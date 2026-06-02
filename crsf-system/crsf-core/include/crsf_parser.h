#pragma once

#include "crsf_packets.h"

namespace crsf {

// Byte-stream CRSF frame parser.
//
// Feed it one byte at a time from whatever transport you have (UART on ESP32,
// FTDI serial on Linux). It runs a small state machine, validates length and
// CRC, and hands back fully-formed packets. It allocates nothing on the hot
// path beyond the packet payload vector and keeps no OS dependencies.
//
// Usage:
//     CRSFParser parser;
//     if (parser.feed(byte)) {
//         CRSFPacket pkt = parser.getPacket();
//         ...
//     }
class CRSFParser {
public:
    CRSFParser();

    // Feed a single byte. Returns true exactly on the byte that completes a
    // valid frame (CRC ok). A completed packet is then retrievable via
    // getPacket() until the next one completes.
    bool feed(std::uint8_t byte);

    // True if a completed packet is waiting to be read.
    bool hasPacket() const { return has_packet_; }

    // Retrieve the completed packet and clear the ready flag.
    CRSFPacket getPacket();

    // Drop any partial frame and return to hunting for a sync byte.
    void reset();

    // Diagnostics.
    std::uint32_t packetCount() const   { return packet_count_; }
    std::uint32_t crcErrorCount() const { return crc_error_count_; }

private:
    enum class State : std::uint8_t {
        Sync,     // hunting for a valid address byte
        Length,   // expecting the length byte
        Payload,  // collecting type + payload + crc
    };

    State          state_;
    std::uint8_t   address_;
    std::size_t    expected_;  // value of the length byte (bytes after it)
    std::size_t    index_;     // bytes collected into buf_ so far
    std::uint8_t   buf_[CRSF_MAX_FRAME_SIZE];  // holds [type .. crc]

    bool           has_packet_;
    CRSFPacket     packet_;

    std::uint32_t  packet_count_;
    std::uint32_t  crc_error_count_;
};

}  // namespace crsf

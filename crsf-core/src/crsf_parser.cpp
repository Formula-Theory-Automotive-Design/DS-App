#include "crsf_parser.h"

#include "crsf_crc.h"

namespace crsf {

bool isKnownAddress(std::uint8_t address) {
    switch (address) {
        case static_cast<std::uint8_t>(CRSFAddress::Broadcast):
        case static_cast<std::uint8_t>(CRSFAddress::FlightController):
        case static_cast<std::uint8_t>(CRSFAddress::Radio):
        case static_cast<std::uint8_t>(CRSFAddress::Receiver):
        case static_cast<std::uint8_t>(CRSFAddress::Transmitter):
            return true;
        default:
            return false;
    }
}

CRSFParser::CRSFParser() {
    reset();
    packet_count_    = 0;
    crc_error_count_ = 0;
}

void CRSFParser::reset() {
    state_      = State::Sync;
    address_    = 0;
    expected_   = 0;
    index_      = 0;
    has_packet_ = false;
}

bool CRSFParser::feed(std::uint8_t byte) {
    switch (state_) {
        case State::Sync:
            // Hunt for a recognized device address. Unknown bytes are skipped,
            // which is how the parser re-syncs after a corrupt frame.
            if (isKnownAddress(byte)) {
                address_ = byte;
                state_   = State::Length;
            }
            return false;

        case State::Length:
            // The length byte must describe a frame that fits the buffer.
            if (byte < CRSF_MIN_LEN_VALUE || byte > CRSF_MAX_LEN_VALUE) {
                // Not a plausible length. The byte we just saw might itself be
                // a fresh sync byte, so re-evaluate it from the Sync state
                // rather than blindly dropping it.
                state_ = State::Sync;
                return feed(byte);
            }
            expected_ = byte;
            index_    = 0;
            state_    = State::Payload;
            return false;

        case State::Payload:
            buf_[index_++] = byte;
            if (index_ < expected_) {
                return false;
            }

            // Full frame collected: buf_ holds [type, payload..., crc].
            {
                const std::size_t crc_index = expected_ - 1;
                const std::uint8_t computed  = crc8(buf_, crc_index);
                const std::uint8_t received  = buf_[crc_index];

                state_ = State::Sync;  // ready for the next frame regardless

                if (computed != received) {
                    ++crc_error_count_;
                    return false;
                }

                packet_.address = address_;
                packet_.type    = static_cast<CRSFType>(buf_[0]);
                packet_.payload.assign(buf_ + 1, buf_ + crc_index);
                has_packet_ = true;
                ++packet_count_;
                return true;
            }
    }
    return false;
}

CRSFPacket CRSFParser::getPacket() {
    has_packet_ = false;
    return packet_;
}

}  // namespace crsf

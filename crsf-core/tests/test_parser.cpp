#include "crsf_encoder.h"
#include "crsf_parser.h"
#include "test_framework.h"

void test_parser() {
    using namespace crsf;

    CRSFPacker packer(static_cast<std::uint8_t>(CRSFAddress::FlightController));
    CRSFParser parser;

    // Encode an RC frame, feed it byte by byte, expect one packet back.
    RCChannels ch;
    for (std::size_t i = 0; i < CRSF_NUM_CHANNELS; ++i) {
        ch.channels[i] = static_cast<std::uint16_t>(172 + i * 100);
    }
    std::vector<std::uint8_t> frame = packer.encodeRCChannels(ch);

    bool completed = false;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const bool done = parser.feed(frame[i]);
        if (i + 1 == frame.size()) {
            completed = done;  // should complete exactly on the last byte
        } else {
            CHECK(!done);
        }
    }
    CHECK(completed);
    CHECK(parser.hasPacket());

    CRSFPacket pkt = parser.getPacket();
    CHECK(!parser.hasPacket());
    CHECK_EQ(pkt.type, CRSFType::RCChannelsPacked);
    CHECK_EQ(pkt.payload.size(), CRSF_RC_CHANNELS_PAYLOAD);

    RCChannels decoded;
    unpackChannels(pkt.payload.data(), decoded);
    for (std::size_t i = 0; i < CRSF_NUM_CHANNELS; ++i) {
        CHECK_EQ(decoded.channels[i], ch.channels[i]);
    }

    // Leading garbage before a valid frame must be skipped (re-sync).
    parser.reset();
    std::vector<std::uint8_t> noisy = {0x01, 0x99, 0x7F};  // junk, no valid addr
    for (std::uint8_t b : noisy) CHECK(!parser.feed(b));
    completed = false;
    for (std::uint8_t b : frame) completed = parser.feed(b) || completed;
    CHECK(completed);

    // A corrupted CRC must be rejected and counted.
    parser.reset();
    std::vector<std::uint8_t> bad = frame;
    bad.back() ^= 0xFF;  // break the CRC
    bool any = false;
    for (std::uint8_t b : bad) any = parser.feed(b) || any;
    CHECK(!any);
    CHECK_EQ(parser.crcErrorCount(), 1u);
    CHECK(!parser.hasPacket());
}

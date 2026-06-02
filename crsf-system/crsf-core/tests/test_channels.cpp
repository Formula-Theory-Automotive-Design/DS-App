#include "crsf_encoder.h"
#include "test_framework.h"

void test_channels() {
    using namespace crsf;

    // Round-trip: pack then unpack must reproduce every channel exactly.
    RCChannels in;
    for (std::size_t i = 0; i < CRSF_NUM_CHANNELS; ++i) {
        // Spread distinct 11-bit values across the channels.
        in.channels[i] = static_cast<std::uint16_t>((i * 113 + 172) & 0x7FF);
    }

    std::uint8_t packed[CRSF_RC_CHANNELS_PAYLOAD];
    packChannels(in, packed);

    RCChannels out;
    unpackChannels(packed, out);

    for (std::size_t i = 0; i < CRSF_NUM_CHANNELS; ++i) {
        CHECK_EQ(in.channels[i], out.channels[i]);
    }

    // Boundary values survive the round trip.
    RCChannels edge;
    edge.channels[0] = 0;
    edge.channels[1] = CRSF_CHANNEL_VALUE_MASK;  // 2047
    edge.channels[2] = CRSF_CHANNEL_VALUE_MIN;
    edge.channels[3] = CRSF_CHANNEL_VALUE_MID;
    edge.channels[4] = CRSF_CHANNEL_VALUE_MAX;
    std::uint8_t epacked[CRSF_RC_CHANNELS_PAYLOAD];
    packChannels(edge, epacked);
    RCChannels eout;
    unpackChannels(epacked, eout);
    CHECK_EQ(eout.channels[0], 0);
    CHECK_EQ(eout.channels[1], CRSF_CHANNEL_VALUE_MASK);
    CHECK_EQ(eout.channels[4], CRSF_CHANNEL_VALUE_MAX);

    // us<->ticks midpoint identity.
    CHECK_EQ(ticksToUs(CRSF_CHANNEL_VALUE_MID), 1500);
    CHECK_EQ(usToTicks(1500), CRSF_CHANNEL_VALUE_MID);
}

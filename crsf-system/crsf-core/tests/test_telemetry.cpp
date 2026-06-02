#include <cmath>

#include "crsf_encoder.h"
#include "crsf_parser.h"
#include "crsf_telemetry.h"
#include "test_framework.h"

namespace {
bool near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

// Encode -> parse -> return the decoded packet for a telemetry round trip.
crsf::CRSFPacket roundTrip(const std::vector<std::uint8_t>& frame) {
    crsf::CRSFParser parser;
    for (std::uint8_t b : frame) parser.feed(b);
    return parser.getPacket();
}
}  // namespace

void test_telemetry() {
    using namespace crsf;
    CRSFPacker packer;

    // Battery.
    BatteryData bat;
    bat.voltage   = 16.8f;
    bat.current   = 12.3f;
    bat.capacity  = 1450;
    bat.remaining = 87;
    CRSFPacket bpkt = roundTrip(packer.encodeBattery(bat));
    BatteryData bout;
    CHECK(decodeBattery(bpkt, bout));
    CHECK(near(bout.voltage, 16.8f));
    CHECK(near(bout.current, 12.3f));
    CHECK_EQ(bout.capacity, 1450u);
    CHECK_EQ(bout.remaining, 87);

    // GPS.
    GPSData gps;
    gps.latitude    = 37.7749;
    gps.longitude   = -122.4194;
    gps.groundSpeed = 42.5f;
    gps.heading     = 180.25f;
    gps.altitude    = 125.0f;
    gps.satellites  = 11;
    CRSFPacket gpkt = roundTrip(packer.encodeGPS(gps));
    GPSData gout;
    CHECK(decodeGPS(gpkt, gout));
    CHECK(std::fabs(gout.latitude - 37.7749) < 1e-4);
    CHECK(std::fabs(gout.longitude - (-122.4194)) < 1e-4);
    CHECK(near(gout.groundSpeed, 42.5f));
    CHECK(near(gout.heading, 180.25f, 0.02f));
    CHECK(near(gout.altitude, 125.0f, 1.0f));
    CHECK_EQ(gout.satellites, 11);

    // Link statistics.
    LinkStats ls;
    ls.uplinkRSSI1   = 95;
    ls.uplinkLQ      = 100;
    ls.uplinkSNR     = -7;
    ls.downlinkSNR   = 9;
    ls.uplinkTxPower = 3;
    CRSFPacket lpkt = roundTrip(packer.encodeLinkStats(ls));
    LinkStats lout;
    CHECK(decodeLinkStats(lpkt, lout));
    CHECK_EQ(lout.uplinkRSSI1, 95);
    CHECK_EQ(lout.uplinkLQ, 100);
    CHECK_EQ(lout.uplinkSNR, -7);
    CHECK_EQ(lout.downlinkSNR, 9);
}

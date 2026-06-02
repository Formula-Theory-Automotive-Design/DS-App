// backend — minimal serial + CRSF read loop.
//
// Opens the FTDI serial link to the RadioMaster Nano, feeds the byte stream
// into the (OS-free) crsf-core parser, and prints every decoded frame. This is
// the first piece of the backend that talks to real hardware. HID, the state
// manager, and the WebSocket server come in later phases.
//
// Usage:
//     backend [device] [baud]
//   defaults: /dev/ttyUSB0 420000

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "crsf_encoder.h"
#include "crsf_parser.h"
#include "crsf_telemetry.h"
#include "serial/serial_port.h"

namespace {

std::atomic<bool> g_running{true};

void onSignal(int) {
    g_running = false;
}

const char* typeName(crsf::CRSFType t) {
    using T = crsf::CRSFType;
    switch (t) {
        case T::GPS:              return "GPS";
        case T::Vario:            return "VARIO";
        case T::BatterySensor:    return "BATTERY";
        case T::Baro:             return "BARO";
        case T::LinkStatistics:   return "LINK_STATS";
        case T::RCChannelsPacked: return "RC_CHANNELS";
        case T::LinkStatisticsRx: return "LINK_STATS_RX";
        case T::LinkStatisticsTx: return "LINK_STATS_TX";
        case T::Attitude:         return "ATTITUDE";
        case T::FlightMode:       return "FLIGHT_MODE";
        case T::DevicePing:       return "DEVICE_PING";
        case T::DeviceInfo:       return "DEVICE_INFO";
        case T::ParameterEntry:   return "PARAM_ENTRY";
        case T::ParameterRead:    return "PARAM_READ";
        case T::ParameterWrite:   return "PARAM_WRITE";
        case T::Command:          return "COMMAND";
        default:                  return "UNKNOWN";
    }
}

void printPacket(const crsf::CRSFPacket& pkt) {
    std::printf("[0x%02X] %-14s len=%2zu  ",
                static_cast<unsigned>(pkt.type), typeName(pkt.type),
                pkt.payload.size());

    switch (pkt.type) {
        case crsf::CRSFType::BatterySensor: {
            crsf::BatteryData b;
            if (crsf::decodeBattery(pkt, b)) {
                std::printf("%.1fV  %.1fA  %u mAh  %u%%",
                            b.voltage, b.current, b.capacity, b.remaining);
            }
            break;
        }
        case crsf::CRSFType::GPS: {
            crsf::GPSData g;
            if (crsf::decodeGPS(pkt, g)) {
                std::printf("%.6f,%.6f  %.1f km/h  %.0f deg  %.0f m  %u sats",
                            g.latitude, g.longitude, g.groundSpeed,
                            g.heading, g.altitude, g.satellites);
            }
            break;
        }
        case crsf::CRSFType::LinkStatistics: {
            crsf::LinkStats s;
            if (crsf::decodeLinkStats(pkt, s)) {
                std::printf("up: -%udBm LQ=%u%% SNR=%d  |  dn: -%udBm LQ=%u%%",
                            s.uplinkRSSI1, s.uplinkLQ, s.uplinkSNR,
                            s.downlinkRSSI, s.downlinkLQ);
            }
            break;
        }
        case crsf::CRSFType::RCChannelsPacked: {
            crsf::RCChannels ch;
            crsf::unpackChannels(pkt.payload.data(), ch);
            std::printf("ch1-4: %4u %4u %4u %4u  ...",
                        ch.channels[0], ch.channels[1],
                        ch.channels[2], ch.channels[3]);
            break;
        }
        default:
            break;
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string device = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    const int         baud   = (argc > 2) ? std::atoi(argv[2]) : 420000;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    backend::SerialPort port;
    if (!port.open(device, baud)) {
        std::fprintf(stderr, "error: %s\n", port.lastError().c_str());
        return 1;
    }
    std::printf("Opened %s @ %d baud. Listening for CRSF frames (Ctrl-C to stop)...\n",
                device.c_str(), baud);

    crsf::CRSFParser parser;
    std::uint8_t     buf[256];
    std::uint32_t    lastReport = 0;

    while (g_running) {
        long n = port.read(buf, sizeof(buf));
        if (n < 0) {
            std::fprintf(stderr, "error: %s\n", port.lastError().c_str());
            break;
        }
        for (long i = 0; i < n; ++i) {
            if (parser.feed(buf[i])) {
                printPacket(parser.getPacket());
            }
        }

        // Periodic stats line every ~256 packets so CRC health is visible.
        if (parser.packetCount() - lastReport >= 256) {
            lastReport = parser.packetCount();
            std::printf("--- %u packets, %u CRC errors ---\n",
                        parser.packetCount(), parser.crcErrorCount());
        }
    }

    std::printf("\nStopping. Total: %u packets, %u CRC errors.\n",
                parser.packetCount(), parser.crcErrorCount());
    port.close();
    return 0;
}

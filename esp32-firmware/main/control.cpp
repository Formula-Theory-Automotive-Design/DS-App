#include "control.h"

namespace app {

void Control::onPacket(const crsf::CRSFPacket& pkt, std::uint32_t now_ms) {
    switch (pkt.type) {
        case crsf::CRSFType::RCChannelsPacked:
            if (pkt.payload.size() == crsf::CRSF_RC_CHANNELS_PAYLOAD) {
                crsf::unpackChannels(pkt.payload.data(), channels_);
                last_rc_ms_ = now_ms;
                got_rc_     = true;
                ++rc_count_;
            }
            break;

        case crsf::CRSFType::LinkStatistics: {
            crsf::LinkStats l;
            if (crsf::decodeLinkStats(pkt, l)) {
                link_     = l;
                has_link_ = true;
            }
            break;
        }

        default:
            // Other telemetry types can be handled here as the node grows.
            break;
    }
}

bool Control::failsafe(std::uint32_t now_ms, std::uint32_t timeout_ms) const {
    if (!got_rc_) return true;
    return (now_ms - last_rc_ms_) > timeout_ms;
}

}  // namespace app

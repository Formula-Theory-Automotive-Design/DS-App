#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "crsf_packets.h"
#include "crsf_params.h"
#include "radio/link_config.h"

namespace backend {

// Drives the CRSF parameter-device protocol against an ELRS TX module to push
// the settings from radio.conf. It owns no transport: the caller supplies a
// `send` callback (serialized writes to the serial port) and feeds incoming
// DeviceInfo / ParameterEntry packets in via onPacket() from the RX thread.
//
// apply() runs the whole exchange synchronously (ping -> enumerate the menu ->
// match each desired setting by field name -> write the option -> confirm) and
// returns a per-setting report. It blocks while waiting for replies, so call it
// from a dedicated thread, not the RX or RC-TX hot paths.
class ElrsLink {
public:
    // Thread-safe: enqueue a packet seen by the RX thread. Non-parameter
    // packets are ignored cheaply.
    void onPacket(const crsf::CRSFPacket& pkt);

    struct ApplyResult {
        bool        connected = false;   // module answered the ping
        std::string deviceName;
        int         changed   = 0;       // settings actually written
        std::vector<std::string> log;    // one human-readable line per setting
    };

    using SendFn    = std::function<bool(const std::vector<std::uint8_t>&)>;
    using RunningFn = std::function<bool()>;

    ApplyResult apply(const std::vector<LinkConfig::Desired>& desired,
                      const SendFn& send, const RunningFn& running);

private:
    bool nextPacket(crsf::CRSFPacket& out, int timeoutMs, const RunningFn& running);
    void drainInbox();

    bool discover(const SendFn& send, const RunningFn& running, crsf::DeviceInfo& info);
    bool readField(std::uint8_t id, const SendFn& send, const RunningFn& running,
                   crsf::ParamEntry& out);

    std::mutex                   mtx_;
    std::condition_variable      cv_;
    std::deque<crsf::CRSFPacket> inbox_;
};

}  // namespace backend

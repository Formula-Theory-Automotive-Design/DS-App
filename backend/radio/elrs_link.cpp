#include "radio/elrs_link.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

namespace backend {

namespace {

using Clock = std::chrono::steady_clock;

// Timeouts/retries for the request/response exchange.
constexpr int  kPingTimeoutMs  = 300;
constexpr int  kPingAttempts   = 6;
constexpr int  kFieldTimeoutMs = 600;
constexpr int  kPacketWaitMs   = 120;

// lower-case + drop spaces, for forgiving comparisons.
std::string norm(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return o;
}

bool nameMatches(const std::string& fieldName, const std::string& candidate) {
    return norm(fieldName).find(norm(candidate)) != std::string::npos;
}

// Drop an ELRS-style "(...)" annotation and surrounding space, so
// "250Hz(-108dBm)" -> "250Hz" and "Hybrid" -> "Hybrid".
std::string cleanLabel(const std::string& s) {
    std::string t = s.substr(0, s.find('('));
    const std::size_t a = t.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const std::size_t b = t.find_last_not_of(" \t");
    return t.substr(a, b - a + 1);
}

// The leading run of digits, e.g. "250Hz" -> "250", "D250" -> "", "" -> "".
std::string leadingDigits(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        o.push_back(c);
    }
    return o;
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Find the option index in `options` that the user's `value` refers to, or -1.
// ELRS option labels often carry a sensitivity annotation, e.g.
// "250Hz(-108dBm)" or "100Hz Full(-112dBm)", so we match against the label with
// that annotation stripped — and for a plain numeric value against the label's
// *leading* number, so "250" picks "250Hz", never "D250" or the dBm figure.
int matchOption(const std::string& value, const std::vector<std::string>& options) {
    const std::string nv = norm(value);
    for (std::size_t i = 0; i < options.size(); ++i)            // exact (normalized)
        if (norm(cleanLabel(options[i])) == nv) return static_cast<int>(i);

    if (allDigits(value)) {                                     // numeric -> leading number
        for (std::size_t i = 0; i < options.size(); ++i)
            if (leadingDigits(cleanLabel(options[i])) == value) return static_cast<int>(i);
    }
    return -1;
}

std::string joinOptions(const std::vector<std::string>& options) {
    std::string s;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i) s += ", ";
        s += options[i];
    }
    return s;
}

}  // namespace

void ElrsLink::onPacket(const crsf::CRSFPacket& pkt) {
    if (pkt.type != crsf::CRSFType::DeviceInfo &&
        pkt.type != crsf::CRSFType::ParameterEntry) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        inbox_.push_back(pkt);
    }
    cv_.notify_one();
}

void ElrsLink::drainInbox() {
    std::lock_guard<std::mutex> lk(mtx_);
    inbox_.clear();
}

bool ElrsLink::nextPacket(crsf::CRSFPacket& out, int timeoutMs, const RunningFn& running) {
    std::unique_lock<std::mutex> lk(mtx_);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (inbox_.empty()) {
        if (!running()) return false;
        const auto now = Clock::now();
        if (now >= deadline) return false;
        // Wake periodically so a shutdown (running()==false) is noticed promptly.
        auto slice = std::min(deadline - now, std::chrono::steady_clock::duration(
                                                  std::chrono::milliseconds(50)));
        cv_.wait_for(lk, slice);
    }
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

bool ElrsLink::discover(const SendFn& send, const RunningFn& running, crsf::DeviceInfo& info) {
    for (int attempt = 0; attempt < kPingAttempts && running(); ++attempt) {
        send(crsf::encodeDevicePing());
        const auto deadline = Clock::now() + std::chrono::milliseconds(kPingTimeoutMs);
        while (Clock::now() < deadline && running()) {
            crsf::CRSFPacket pkt;
            if (!nextPacket(pkt, kPacketWaitMs, running)) continue;
            if (crsf::parseDeviceInfo(pkt, info)) return true;  // ignore stray entries
        }
    }
    return false;
}

bool ElrsLink::readField(std::uint8_t id, const SendFn& send, const RunningFn& running,
                         crsf::ParamEntry& out) {
    crsf::ParamReassembler ra;
    std::uint8_t chunk = 0;
    send(crsf::encodeParamRead(id, chunk));

    auto deadline = Clock::now() + std::chrono::milliseconds(kFieldTimeoutMs);
    while (Clock::now() < deadline && running()) {
        crsf::CRSFPacket pkt;
        if (!nextPacket(pkt, kPacketWaitMs, running)) continue;
        switch (ra.feed(pkt, out)) {
            case crsf::ParamReassembler::Status::Complete:
                if (out.id == id) return true;
                ra.reset();  // a different field completed; keep waiting
                break;
            case crsf::ParamReassembler::Status::NeedMore:
                send(crsf::encodeParamRead(id, ++chunk));
                deadline = Clock::now() + std::chrono::milliseconds(kFieldTimeoutMs);
                break;
            case crsf::ParamReassembler::Status::Ignored:
                break;
        }
    }
    return false;
}

ElrsLink::ApplyResult ElrsLink::apply(const std::vector<LinkConfig::Desired>& desired,
                                      const SendFn& send, const RunningFn& running) {
    ApplyResult result;
    drainInbox();

    crsf::DeviceInfo info;
    if (!discover(send, running, info)) {
        result.connected = false;
        result.log.push_back("no ELRS module responded to the parameter ping");
        return result;
    }
    result.connected  = true;
    result.deviceName = info.name;

    // Enumerate the whole menu so we can match settings by field name.
    std::vector<crsf::ParamEntry> fields;
    for (std::uint8_t id = 1; id <= info.fieldCount && running(); ++id) {
        crsf::ParamEntry e;
        if (readField(id, send, running, e)) fields.push_back(e);
    }
    if (fields.empty()) {
        result.log.push_back("module '" + info.name + "' returned no readable fields");
        return result;
    }

    for (const LinkConfig::Desired& d : desired) {
        const std::vector<std::string>* candidates = candidateFieldNames(d.key);
        if (!candidates) continue;  // validated at parse time; defensive

        // First TextSelection field whose name matches a candidate.
        const crsf::ParamEntry* field = nullptr;
        for (const std::string& cand : *candidates) {
            for (const crsf::ParamEntry& e : fields) {
                if (e.isSelection() && nameMatches(e.name, cand)) { field = &e; break; }
            }
            if (field) break;
        }
        if (!field) {
            result.log.push_back(d.key + ": module has no matching setting (skipped)");
            continue;
        }

        const int idx = matchOption(d.value, field->options);
        if (idx < 0) {
            result.log.push_back(d.key + " (" + field->name + "): '" + d.value +
                                 "' is not offered; options are [" +
                                 joinOptions(field->options) + "]");
            continue;
        }
        const std::string& target = field->options[static_cast<std::size_t>(idx)];
        if (field->value == idx) {
            result.log.push_back(d.key + " (" + field->name + "): already " + target);
            continue;
        }

        const std::string from = (field->value < field->options.size())
                                     ? field->options[field->value] : "?";
        send(crsf::encodeParamWrite(field->id, static_cast<std::uint8_t>(idx)));

        // Let the module apply, then re-read to confirm.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        crsf::ParamEntry after;
        if (readField(field->id, send, running, after) && after.value == idx) {
            result.changed++;
            result.log.push_back(d.key + " (" + field->name + "): " + from + " -> " + target);
        } else {
            result.log.push_back(d.key + " (" + field->name + "): wrote " + target +
                                 " but could not confirm");
        }
    }
    return result;
}

}  // namespace backend

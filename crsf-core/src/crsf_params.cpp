#include "crsf_params.h"

#include "crsf_encoder.h"

namespace crsf {

namespace {

// Read a null-terminated string starting at `i`; advances `i` past the null.
// Returns false if no null terminator is found within the buffer.
bool readCStr(const std::vector<std::uint8_t>& b, std::size_t& i, std::string& out) {
    out.clear();
    while (i < b.size()) {
        const std::uint8_t c = b[i++];
        if (c == 0) return true;
        out.push_back(static_cast<char>(c));
    }
    return false;  // ran off the end without a terminator
}

// Split a ';'-separated options string ("Hybrid;Wide") into a list.
std::vector<std::string> splitOptions(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ';') { out.push_back(cur); cur.clear(); }
        else          { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

// Parse a reassembled parameter-entry body (parent, type, name, [type data]).
bool parseEntryBody(const std::vector<std::uint8_t>& b, ParamEntry& out) {
    if (b.size() < 2) return false;
    std::size_t i = 0;
    out.parent = b[i++];
    const std::uint8_t typeByte = b[i++];
    out.hidden = (typeByte & 0x80) != 0;
    out.type   = static_cast<std::uint8_t>(typeByte & 0x7F);
    if (!readCStr(b, i, out.name)) return false;

    if (out.isSelection()) {
        std::string opts;
        if (!readCStr(b, i, opts)) return false;
        out.options = splitOptions(opts);
        if (i + 4 > b.size()) return false;
        out.value = b[i++];
        out.min   = b[i++];
        out.max   = b[i++];
        out.def   = b[i++];
        readCStr(b, i, out.unit);  // unit is optional / may be empty
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Frame builders
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> encodeDevicePing(std::uint8_t dest, std::uint8_t origin) {
    CRSFPacker packer(CRSF_SYNC_MODULE);
    const std::uint8_t payload[2] = {dest, origin};
    return packer.encodeFrame(CRSFType::DevicePing, payload, sizeof(payload));
}

std::vector<std::uint8_t> encodeParamRead(std::uint8_t fieldId, std::uint8_t chunk,
                                          std::uint8_t dest, std::uint8_t origin) {
    CRSFPacker packer(CRSF_SYNC_MODULE);
    const std::uint8_t payload[4] = {dest, origin, fieldId, chunk};
    return packer.encodeFrame(CRSFType::ParameterRead, payload, sizeof(payload));
}

std::vector<std::uint8_t> encodeParamWrite(std::uint8_t fieldId, std::uint8_t value,
                                           std::uint8_t dest, std::uint8_t origin) {
    CRSFPacker packer(CRSF_SYNC_MODULE);
    const std::uint8_t payload[4] = {dest, origin, fieldId, value};
    return packer.encodeFrame(CRSFType::ParameterWrite, payload, sizeof(payload));
}

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------

bool parseDeviceInfo(const CRSFPacket& pkt, DeviceInfo& out) {
    if (pkt.type != CRSFType::DeviceInfo) return false;
    const std::vector<std::uint8_t>& p = pkt.payload;
    if (p.size() < 2) return false;

    std::size_t i = 2;  // skip ext-header [dest, origin]
    out.origin = p[1];
    if (!readCStr(p, i, out.name)) return false;

    // serial(4) + hwVer(4) + swVer(4) + fieldCount(1) + paramVersion(1) = 14
    if (i + 14 > p.size()) return false;
    i += 12;  // skip serial + hardware + software versions
    out.fieldCount   = p[i++];
    out.paramVersion = p[i++];
    return true;
}

// ---------------------------------------------------------------------------
// Parameter-entry reassembly
// ---------------------------------------------------------------------------

void ParamReassembler::reset() {
    active_    = false;
    field_     = 0;
    remaining_ = 0;
    buf_.clear();
}

ParamReassembler::Status ParamReassembler::feed(const CRSFPacket& pkt, ParamEntry& out) {
    if (pkt.type != CRSFType::ParameterEntry) return Status::Ignored;
    const std::vector<std::uint8_t>& p = pkt.payload;
    // ext-header [dest, origin] + fieldId + chunksRemaining
    if (p.size() < 4) return Status::Ignored;

    const std::uint8_t fieldId   = p[2];
    const std::uint8_t remaining = p[3];

    // A new field id (or the first chunk) starts a fresh accumulation.
    if (!active_ || field_ != fieldId) {
        active_ = true;
        field_  = fieldId;
        buf_.clear();
    }
    buf_.insert(buf_.end(), p.begin() + 4, p.end());
    remaining_ = remaining;

    if (remaining != 0) return Status::NeedMore;

    active_ = false;
    out = ParamEntry{};
    out.id = fieldId;
    if (!parseEntryBody(buf_, out)) return Status::Ignored;
    return Status::Complete;
}

}  // namespace crsf

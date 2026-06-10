#include "crsf_encoder.h"
#include "crsf_params.h"
#include "crsf_parser.h"
#include "test_framework.h"

namespace {

using namespace crsf;

// Run a built frame through the byte-stream parser and return the packet.
CRSFPacket roundTrip(const std::vector<std::uint8_t>& frame, bool& ok) {
    CRSFParser parser;
    ok = false;
    for (std::uint8_t b : frame) ok = parser.feed(b);
    return parser.getPacket();
}

// Build a ParameterEntry (0x2B) frame carrying `body`, split so that `firstLen`
// body bytes go in the first chunk and the rest in following chunks.
std::vector<std::vector<std::uint8_t>> buildEntryFrames(
    std::uint8_t fieldId, const std::vector<std::uint8_t>& body, std::size_t chunkSize) {
    std::vector<std::vector<std::uint8_t>> frames;
    CRSFPacker packer(CRSF_SYNC_MODULE);
    std::size_t off = 0;
    do {
        const std::size_t take = (body.size() - off < chunkSize) ? body.size() - off : chunkSize;
        const std::uint8_t remaining =
            static_cast<std::uint8_t>((body.size() - off - take + chunkSize - 1) / chunkSize);
        std::vector<std::uint8_t> payload = {CRSF_ADDR_HANDSET_LUA, CRSF_ADDR_MODULE,
                                             fieldId, remaining};
        payload.insert(payload.end(), body.begin() + off, body.begin() + off + take);
        frames.push_back(packer.encodeFrame(CRSFType::ParameterEntry, payload.data(),
                                            payload.size()));
        off += take;
    } while (off < body.size());
    return frames;
}

void appendCStr(std::vector<std::uint8_t>& v, const char* s) {
    for (; *s; ++s) v.push_back(static_cast<std::uint8_t>(*s));
    v.push_back(0);
}

}  // namespace

void test_params() {
    using namespace crsf;

    // --- ping / read / write frames are well-formed and re-parse -------------
    {
        bool ok;
        CRSFPacket ping = roundTrip(encodeDevicePing(), ok);
        CHECK(ok);
        CHECK_EQ(ping.type, CRSFType::DevicePing);
        CHECK_EQ(ping.payload.size(), (std::size_t)2);
        CHECK_EQ(ping.payload[0], CRSF_ADDR_BROADCAST);
        CHECK_EQ(ping.payload[1], CRSF_ADDR_HANDSET_LUA);

        CRSFPacket rd = roundTrip(encodeParamRead(7, 1), ok);
        CHECK(ok);
        CHECK_EQ(rd.type, CRSFType::ParameterRead);
        CHECK_EQ(rd.payload[2], (std::uint8_t)7);   // fieldId
        CHECK_EQ(rd.payload[3], (std::uint8_t)1);   // chunk

        CRSFPacket wr = roundTrip(encodeParamWrite(7, 3), ok);
        CHECK(ok);
        CHECK_EQ(wr.type, CRSFType::ParameterWrite);
        CHECK_EQ(wr.payload[2], (std::uint8_t)7);   // fieldId
        CHECK_EQ(wr.payload[3], (std::uint8_t)3);   // value
    }

    // --- DeviceInfo decode ---------------------------------------------------
    {
        CRSFPacker packer(static_cast<std::uint8_t>(CRSFAddress::Radio));
        std::vector<std::uint8_t> p = {CRSF_ADDR_HANDSET_LUA, CRSF_ADDR_MODULE};
        appendCStr(p, "ELRS Nano TX");
        for (int k = 0; k < 12; ++k) p.push_back(0);  // serial + hw + sw versions
        p.push_back(19);  // fieldCount
        p.push_back(0);   // paramVersion
        std::vector<std::uint8_t> frame =
            packer.encodeFrame(CRSFType::DeviceInfo, p.data(), p.size());

        bool ok;
        CRSFPacket pkt = roundTrip(frame, ok);
        CHECK(ok);
        DeviceInfo info;
        CHECK(parseDeviceInfo(pkt, info));
        CHECK_EQ(info.name, std::string("ELRS Nano TX"));
        CHECK_EQ(info.fieldCount, (std::uint8_t)19);
        CHECK_EQ(info.origin, CRSF_ADDR_MODULE);
    }

    // --- TextSelection entry, reassembled from multiple chunks ---------------
    {
        // body = parent, type, name\0, options\0, value, min, max, def, unit\0
        std::vector<std::uint8_t> body;
        body.push_back(0);  // parent (top level)
        body.push_back(static_cast<std::uint8_t>(ParamType::TextSelection));  // type 9
        appendCStr(body, "Max Power");
        appendCStr(body, "10;25;50;100;250");
        body.push_back(1);  // value -> "25"
        body.push_back(0);  // min
        body.push_back(4);  // max
        body.push_back(0);  // default
        appendCStr(body, "mW");

        // Force chunking with a small chunk size.
        auto frames = buildEntryFrames(8, body, 10);
        CHECK(frames.size() > 1);  // confirm we actually exercised reassembly

        ParamReassembler ra;
        ParamEntry entry;
        ParamReassembler::Status status = ParamReassembler::Status::NeedMore;
        for (auto& f : frames) {
            bool ok;
            CRSFPacket pkt = roundTrip(f, ok);
            CHECK(ok);
            status = ra.feed(pkt, entry);
        }
        CHECK_EQ((int)status, (int)ParamReassembler::Status::Complete);
        CHECK_EQ(entry.id, (std::uint8_t)8);
        CHECK_EQ(entry.parent, (std::uint8_t)0);
        CHECK(entry.isSelection());
        CHECK_EQ(entry.name, std::string("Max Power"));
        CHECK_EQ(entry.options.size(), (std::size_t)5);
        CHECK_EQ(entry.options[1], std::string("25"));
        CHECK_EQ(entry.options[4], std::string("250"));
        CHECK_EQ(entry.value, (std::uint8_t)1);
        CHECK_EQ(entry.unit, std::string("mW"));
    }

    // --- single-frame entry (no chunking) ------------------------------------
    {
        std::vector<std::uint8_t> body;
        body.push_back(0);
        body.push_back(static_cast<std::uint8_t>(ParamType::TextSelection));
        appendCStr(body, "Switch Mode");
        appendCStr(body, "Hybrid;Wide");
        body.push_back(1);  // Wide
        body.push_back(0);
        body.push_back(1);
        body.push_back(0);
        appendCStr(body, "");

        auto frames = buildEntryFrames(3, body, 60);
        CHECK_EQ(frames.size(), (std::size_t)1);
        ParamReassembler ra;
        ParamEntry entry;
        bool ok;
        CRSFPacket pkt = roundTrip(frames[0], ok);
        CHECK(ok);
        CHECK_EQ((int)ra.feed(pkt, entry), (int)ParamReassembler::Status::Complete);
        CHECK_EQ(entry.name, std::string("Switch Mode"));
        CHECK_EQ(entry.options.size(), (std::size_t)2);
        CHECK_EQ(entry.value, (std::uint8_t)1);
    }
}

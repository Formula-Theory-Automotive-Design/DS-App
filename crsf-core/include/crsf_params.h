#pragma once

// crsf-core — CRSF "parameter device" protocol (a.k.a. the ELRS settings menu).
//
// This is the same request/response protocol an EdgeTX handset's Lua script
// speaks to configure an ExpressLRS TX module: discover the device, walk its
// menu of parameter fields, and write new values. Like the rest of crsf-core
// this file is pure: it only builds and parses bytes — no UART, threads, or OS.
//
// Flow (the caller drives the transport and timing):
//   1. send encodeDevicePing(...)              -> module replies DeviceInfo(0x29)
//   2. for each field 1..fieldCount:
//        send encodeParamRead(id, chunk)       -> module replies ParameterEntry(0x2B)
//        feed each reply to ParamReassembler    (entries may span several frames)
//   3. send encodeParamWrite(id, value)        -> changes a TextSelection field
//
// Field ids are 1-based and version-specific, so callers should match fields by
// NAME (and options by their text), never by a hard-coded index.

#include <cstdint>
#include <string>
#include <vector>

#include "crsf_packets.h"

namespace crsf {

// Frame sync byte and extended-header addresses used when impersonating the
// handset toward a TX module. These mirror the ELRS Lua script's constants.
constexpr std::uint8_t CRSF_SYNC_MODULE      = 0xEE;  // sync byte for frames -> module
constexpr std::uint8_t CRSF_ADDR_MODULE      = 0xEE;  // ext-header dest: the TX module
constexpr std::uint8_t CRSF_ADDR_HANDSET_LUA = 0xEF;  // ext-header origin: us (the "Lua")
constexpr std::uint8_t CRSF_ADDR_BROADCAST   = 0x00;

// Parameter field types (the low 7 bits of the type byte). The high bit (0x80)
// is an ELRS "hidden" flag, stripped out into ParamEntry::hidden.
enum class ParamType : std::uint8_t {
    Uint8         = 0,
    Int8          = 1,
    Uint16        = 2,
    Int16         = 3,
    Uint32        = 4,
    Int32         = 5,
    Float         = 8,
    TextSelection = 9,   // an enumerated choice — the kind we write
    String        = 10,
    Folder        = 11,
    Info          = 12,
    Command       = 13,
    OutOfRange    = 127,
};

// Decoded DeviceInfo (0x29) reply.
struct DeviceInfo {
    std::uint8_t origin       = 0;  // ext-header origin (the module's address)
    std::string  name;
    std::uint8_t fieldCount   = 0;  // number of parameter fields (1-based ids)
    std::uint8_t paramVersion = 0;
};

// A fully reassembled parameter field.
struct ParamEntry {
    std::uint8_t id     = 0;
    std::uint8_t parent = 0;       // id of the containing Folder (0 = top level)
    std::uint8_t type   = 0;       // low 7 bits of the type byte (see ParamType)
    bool         hidden = false;
    std::string  name;

    // TextSelection (and only that type) populates these.
    std::vector<std::string> options;
    std::uint8_t value = 0;        // current option index
    std::uint8_t min   = 0;
    std::uint8_t max   = 0;
    std::uint8_t def   = 0;
    std::string  unit;

    bool isSelection() const {
        return (type & 0x7F) == static_cast<std::uint8_t>(ParamType::TextSelection);
    }
};

// ---------------------------------------------------------------------------
// Frame builders (all sent toward the module: sync byte CRSF_SYNC_MODULE).
// ---------------------------------------------------------------------------

// Discover devices. `dest` defaults to broadcast so any module answers.
std::vector<std::uint8_t> encodeDevicePing(std::uint8_t dest = CRSF_ADDR_BROADCAST,
                                           std::uint8_t origin = CRSF_ADDR_HANDSET_LUA);

// Request field `fieldId`, chunk `chunk` (start at 0; re-request with the next
// index while a reply still reports chunks remaining).
std::vector<std::uint8_t> encodeParamRead(std::uint8_t fieldId, std::uint8_t chunk = 0,
                                          std::uint8_t dest = CRSF_ADDR_MODULE,
                                          std::uint8_t origin = CRSF_ADDR_HANDSET_LUA);

// Write a single-byte value (e.g. a TextSelection option index) to `fieldId`.
std::vector<std::uint8_t> encodeParamWrite(std::uint8_t fieldId, std::uint8_t value,
                                           std::uint8_t dest = CRSF_ADDR_MODULE,
                                           std::uint8_t origin = CRSF_ADDR_HANDSET_LUA);

// Parse a DeviceInfo (0x29) packet. Returns false if it isn't one / is malformed.
bool parseDeviceInfo(const CRSFPacket& pkt, DeviceInfo& out);

// Reassembles ParameterEntry (0x2B) frames, which the module may split into
// several chunks, into a single ParamEntry. Feed every 0x2B packet to feed();
// when it returns Complete the entry is ready in `out`.
class ParamReassembler {
public:
    enum class Status { Ignored, NeedMore, Complete };

    Status feed(const CRSFPacket& pkt, ParamEntry& out);
    void reset();

    std::uint8_t pendingField() const   { return field_; }
    std::uint8_t chunksRemaining() const { return remaining_; }

private:
    bool                      active_    = false;
    std::uint8_t              field_     = 0;
    std::uint8_t              remaining_ = 0;
    std::vector<std::uint8_t> buf_;
};

}  // namespace crsf

#pragma once

#include "crsf_packets.h"

namespace backend {

// Half-duplex single-wire echo protection.
//
// The driver-station's serial link to the ELRS module is wired single-wire: the
// CP2102N TX is joined to its own RX through a ~1K series resistor and tied to
// the module's S.Port pad. The resistor lets the module win the line when it
// talks back; the cost is that every frame WE transmit is echoed straight into
// our own receiver.
//
// Those echoes can be discarded reliably *by frame type*: the CRSF types this
// app transmits are disjoint from the types the module sends us, so any received
// frame whose type is one we transmit is, by definition, our own echo. This is
// more robust than byte-count muting (which desyncs on any dropped or collided
// byte) because it acts only on frames that already passed the CRC check.
//
//   transmitted by us        received from the module
//   -----------------        ------------------------
//   RCChannelsPacked 0x16    LinkStatistics  0x14
//   DevicePing       0x28    BatterySensor   0x08
//   ParameterRead    0x2C    DeviceInfo      0x29
//   ParameterWrite   0x2D    ParameterEntry  0x2B
inline bool isEchoFrame(crsf::CRSFType type) {
    switch (type) {
        case crsf::CRSFType::RCChannelsPacked:  // our ~100 Hz RC stream
        case crsf::CRSFType::DevicePing:        // ELRS parameter discovery
        case crsf::CRSFType::ParameterRead:
        case crsf::CRSFType::ParameterWrite:
            return true;
        default:
            return false;
    }
}

}  // namespace backend

# crsf-core

Pure, reusable CRSF (Crossfire) protocol library. **The core is the product;
everything else is an IO adapter.**

## Design rule

If it touches UART, HID, threads, sockets, or any OS API → it does **not**
belong in crsf-core. This library only turns bytes into packets and packets
into bytes. The exact same files compile on desktop Linux (backend) and on
ESP32 (ESP-IDF component).

## Contents

| File | Purpose |
|------|---------|
| `crsf_packets.h`   | Frame geometry, addresses, type ids, `CRSFPacket`, `RCChannels` |
| `crsf_crc.*`       | CRC8/DVB-S2 (poly `0xD5`), one-shot and incremental |
| `crsf_parser.*`    | Byte-stream state machine → validated packets |
| `crsf_encoder.*`   | 11-bit RC channel pack/unpack, frame builder, typed encoders |
| `crsf_telemetry.*` | Decoded battery / GPS / link-stats structs + decoders |

## Wire format

```
+---------+--------+--------+------------------+--------+
| address |  len   |  type  |     payload      |  crc8  |
+---------+--------+--------+------------------+--------+
     0        1        2        3 .. (len)       len+1
```

- `len` = bytes that follow it = `sizeof(type) + sizeof(payload) + sizeof(crc)`.
- `crc8` covers `[type .. payload]` only (not address or len).

## Quick use

```cpp
crsf::CRSFParser parser;
while (haveByte) {
    if (parser.feed(byte)) {
        crsf::CRSFPacket pkt = parser.getPacket();
        crsf::BatteryData bat;
        if (crsf::decodeBattery(pkt, bat)) { /* ... */ }
    }
}

crsf::CRSFPacker packer;            // builds full frames
crsf::RCChannels ch;               // 16 x 11-bit values
auto frame = packer.encodeRCChannels(ch);  // -> std::vector<uint8_t> to send
```

## Build & test

Requires CMake ≥ 3.16 and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

> On Fedora you need the C++ compiler: `sudo dnf install gcc-c++`.

The tests use a tiny built-in framework (no external dependencies) and need no
hardware — they round-trip encode → parse → decode for RC channels and
telemetry, and exercise CRC, re-sync, and CRC-rejection paths.

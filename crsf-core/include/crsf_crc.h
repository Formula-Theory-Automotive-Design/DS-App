#pragma once

#include <cstddef>
#include <cstdint>

namespace crsf {

// CRC8/DVB-S2, polynomial 0xD5, initial value 0x00, no reflection.
// This is the CRC used by CRSF, computed over [type .. payload].
std::uint8_t crc8(const std::uint8_t* data, std::size_t len);

// Incremental variant: fold one more byte into a running CRC.
std::uint8_t crc8_update(std::uint8_t crc, std::uint8_t byte);

}  // namespace crsf

#include "crsf_crc.h"

namespace crsf {

std::uint8_t crc8_update(std::uint8_t crc, std::uint8_t byte) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        if (crc & 0x80) {
            crc = static_cast<std::uint8_t>((crc << 1) ^ 0xD5);
        } else {
            crc = static_cast<std::uint8_t>(crc << 1);
        }
    }
    return crc;
}

std::uint8_t crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < len; ++i) {
        crc = crc8_update(crc, data[i]);
    }
    return crc;
}

}  // namespace crsf

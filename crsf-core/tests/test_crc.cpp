#include "crsf_crc.h"
#include "test_framework.h"

void test_crc() {
    using namespace crsf;

    // Empty input -> initial value 0.
    CHECK_EQ(crc8(nullptr, 0), 0);

    // Single byte equal to 0x01 folds to the polynomial 0xD5.
    const std::uint8_t one = 0x01;
    CHECK_EQ(crc8(&one, 1), 0xD5);

    // Link-statistics frame body: [type 0x14, 10-byte payload].
    const std::uint8_t data[] = {
        0x14, 0x10, 0x19, 0x63, 0x00, 0x03,
        0x08, 0x00, 0x16, 0x00, 0x00,
    };
    CHECK_EQ(crc8(data, sizeof(data)), 0x8D);

    // Incremental folding matches the one-shot computation.
    std::uint8_t inc = 0;
    for (std::size_t i = 0; i < sizeof(data); ++i) {
        inc = crc8_update(inc, data[i]);
    }
    CHECK_EQ(inc, crc8(data, sizeof(data)));
}

#include <cstdio>

#include "test_framework.h"

void test_crc();
void test_channels();
void test_parser();
void test_telemetry();

int main() {
    test_crc();
    test_channels();
    test_parser();
    test_telemetry();

    std::printf("----\n%d checks, %d failures\n",
                crsftest::checks(), crsftest::failures());
    return crsftest::failures() == 0 ? 0 : 1;
}

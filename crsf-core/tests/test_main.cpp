#include <cstdio>

#include "test_framework.h"

void test_crc();
void test_channels();
void test_parser();
void test_telemetry();
void test_params();

int main() {
    test_crc();
    test_channels();
    test_parser();
    test_telemetry();
    test_params();

    std::printf("----\n%d checks, %d failures\n",
                crsftest::checks(), crsftest::failures());
    return crsftest::failures() == 0 ? 0 : 1;
}

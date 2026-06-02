# crsf-core — pure, OS-free CRSF protocol library.
#
# Builds as a static library that the Linux backend and the ESP32 firmware
# both link against. No threads, no serial, no sockets, no OS calls here.

add_library(crsf-core STATIC
    src/crsf_crc.cpp
    src/crsf_parser.cpp
    src/crsf_encoder.cpp
    src/crsf_telemetry.cpp
)

target_include_directories(crsf-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(crsf-core PUBLIC cxx_std_17)

if(CRSF_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(crsf_core_tests
        tests/test_main.cpp
        tests/test_crc.cpp
        tests/test_channels.cpp
        tests/test_parser.cpp
        tests/test_telemetry.cpp
    )
    target_link_libraries(crsf_core_tests PRIVATE crsf-core)
    target_include_directories(crsf_core_tests PRIVATE tests)
    add_test(NAME crsf_core_tests COMMAND crsf_core_tests)
endif()

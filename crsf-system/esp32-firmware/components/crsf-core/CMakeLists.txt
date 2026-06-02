# crsf-core as an ESP-IDF component.
#
# CRITICAL REUSE: this does NOT copy or fork the library. It registers the very
# same source and header files that the desktop backend builds, referenced by
# relative path. One implementation, two targets — edit crsf-core once and both
# the Linux backend and this firmware pick it up.
#
#   esp32-firmware/components/crsf-core  ->  ../../../crsf-core   (shared source)
#
# (A git submodule would be the alternative if the firmware lived in its own
# repository; within this monorepo a relative path is simpler and avoids drift.)

set(CRSF_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../crsf-core")

idf_component_register(
    SRCS
        "${CRSF_CORE_DIR}/src/crsf_crc.cpp"
        "${CRSF_CORE_DIR}/src/crsf_parser.cpp"
        "${CRSF_CORE_DIR}/src/crsf_encoder.cpp"
        "${CRSF_CORE_DIR}/src/crsf_telemetry.cpp"
    INCLUDE_DIRS
        "${CRSF_CORE_DIR}/include"
)

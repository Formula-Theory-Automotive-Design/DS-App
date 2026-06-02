cmake_minimum_required(VERSION 3.16)
project(crsf_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(CRSF_CORE_BUILD_TESTS "Build crsf-core unit tests" ON)

enable_testing()

add_subdirectory(crsf-core)
add_subdirectory(backend)

# esp32-firmware/ is built with the ESP-IDF toolchain, not this tree.

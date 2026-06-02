# Top-level ESP-IDF project.
#
# Build with the ESP-IDF toolchain (not the desktop CMake tree):
#   idf.py set-target esp32
#   idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(crsf_esp32)

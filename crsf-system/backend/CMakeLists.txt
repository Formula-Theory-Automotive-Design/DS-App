# backend — desktop Linux app: serial IO + CRSF processing.
#
# This phase links the serial layer and the read loop against crsf-core. HID,
# state manager, and WebSocket server are added as their own sources later.

add_executable(backend
    main.cpp
    serial/serial_port.cpp
)

target_include_directories(backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend PRIVATE crsf-core)

target_compile_features(backend PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(backend PRIVATE -Wall -Wextra)
endif()

# backend — desktop Linux app: serial IO + HID + state + WebSocket.

find_package(Threads REQUIRED)

add_executable(backend
    main.cpp
    serial/serial_port.cpp
    hid/joystick.cpp
    mapping/channel_map.cpp
    radio/link_config.cpp
    radio/elrs_link.cpp
    state/state.h
    websocket/sha1.cpp
    websocket/ws_server.cpp
)

target_include_directories(backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend PRIVATE crsf-core Threads::Threads)

target_compile_features(backend PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(backend PRIVATE -Wall -Wextra)
endif()

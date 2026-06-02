#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace backend {

// Minimal SHA-1 (RFC 3174). Only used for the WebSocket opening handshake
// (Sec-WebSocket-Accept), so it does not need to be fast or constant-time.
// Returns the 20-byte digest as a std::string of raw bytes.
std::string sha1(const std::uint8_t* data, std::size_t len);
std::string sha1(const std::string& s);

}  // namespace backend

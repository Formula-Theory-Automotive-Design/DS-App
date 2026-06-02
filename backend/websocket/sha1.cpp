#include "sha1.h"

#include <cstring>

namespace backend {

namespace {
inline std::uint32_t rol(std::uint32_t v, int b) {
    return (v << b) | (v >> (32 - b));
}
}  // namespace

std::string sha1(const std::uint8_t* data, std::size_t len) {
    std::uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
                  h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // Pre-processing: append 0x80, pad with zeros, append 64-bit bit length.
    const std::uint64_t bitLen = static_cast<std::uint64_t>(len) * 8;
    std::string msg(reinterpret_cast<const char*>(data), len);
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[80];
        const auto* p = reinterpret_cast<const std::uint8_t*>(msg.data() + chunk);
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(p[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20)       { f = (b & c) | (~b & d);            k = 0x5A827999; }
            else if (i < 40)  { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
            else if (i < 60)  { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
            else              { f = b ^ c ^ d;                     k = 0xCA62C1D6; }

            std::uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::uint8_t digest[20];
    const std::uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4]     = (hs[i] >> 24) & 0xFF;
        digest[i * 4 + 1] = (hs[i] >> 16) & 0xFF;
        digest[i * 4 + 2] = (hs[i] >> 8) & 0xFF;
        digest[i * 4 + 3] = hs[i] & 0xFF;
    }
    return std::string(reinterpret_cast<char*>(digest), 20);
}

std::string sha1(const std::string& s) {
    return sha1(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

}  // namespace backend

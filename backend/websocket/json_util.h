#pragma once

#include <cstdio>
#include <string>

namespace backend {

// Minimal JSON helpers for the small request/response shapes the backend speaks
// over the WebSocket. Not a general parser -- just enough to read string fields
// out of our own frontend's messages and to emit string values safely.

// Escape a string for inclusion between double quotes in a JSON value.
inline std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o;
}

// Extract the string value of "key" from a JSON object text, decoding the
// standard escape sequences. Returns false if the key (with a string value) is
// not found. Finds the first "key": "..." occurrence; safe for our messages
// because every value that could contain the key text has its quotes escaped.
inline bool jsonGetString(const std::string& src, const std::string& key, std::string& out) {
    const std::string pat = "\"" + key + "\"";
    std::size_t p = src.find(pat);
    if (p == std::string::npos) return false;
    p = src.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < src.size() && (src[p] == ' ' || src[p] == '\t' ||
                              src[p] == '\n' || src[p] == '\r')) ++p;
    if (p >= src.size() || src[p] != '"') return false;
    ++p;

    auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return 0;
    };

    std::string r;
    while (p < src.size()) {
        const char ch = src[p++];
        if (ch == '"') { out.swap(r); return true; }
        if (ch == '\\' && p < src.size()) {
            const char e = src[p++];
            switch (e) {
                case '"':  r += '"';  break;
                case '\\': r += '\\'; break;
                case '/':  r += '/';  break;
                case 'n':  r += '\n'; break;
                case 'r':  r += '\r'; break;
                case 't':  r += '\t'; break;
                case 'b':  r += '\b'; break;
                case 'f':  r += '\f'; break;
                case 'u':
                    if (p + 4 <= src.size()) {
                        const int cp = (hex(src[p]) << 12) | (hex(src[p + 1]) << 8) |
                                       (hex(src[p + 2]) << 4) | hex(src[p + 3]);
                        p += 4;
                        if (cp < 0x80) {
                            r += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            r += static_cast<char>(0xC0 | (cp >> 6));
                            r += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            r += static_cast<char>(0xE0 | (cp >> 12));
                            r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            r += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                default: r += e; break;
            }
        } else {
            r += ch;
        }
    }
    return false;  // unterminated string
}

}  // namespace backend

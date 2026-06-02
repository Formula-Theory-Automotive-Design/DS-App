#pragma once

#include <cstdio>

namespace crsftest {
inline int& checks()   { static int c = 0; return c; }
inline int& failures() { static int f = 0; return f; }
}  // namespace crsftest

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++crsftest::checks();                                             \
        if (!(cond)) {                                                    \
            ++crsftest::failures();                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        ++crsftest::checks();                                             \
        if (!((a) == (b))) {                                              \
            ++crsftest::failures();                                       \
            std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__,     \
                        #a, #b);                                          \
        }                                                                 \
    } while (0)

// Diagnostics for framelink.
//
// A library has no business writing to an application's stdout, so this is off
// unless FRAMELINK_DEBUG is set in the environment. It stays because the two
// bugs that cost the most during bring-up - a producer refused for a reason it
// could not see, and a fence handle that was silently NULL - were both invisible
// failures where the only symptom was "no frames".
#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace fl {

inline bool logEnabled() {
    static const bool on = getenv("FRAMELINK_DEBUG") != nullptr;
    return on;
}

inline void logf(const char* level, const char* fmt, ...) {
    if (!logEnabled()) return;
    fprintf(stderr, "[framelink %s] ", level);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

} // namespace fl

#define FL_LOG(...) ::fl::logf("info", __VA_ARGS__)
// Errors are always reported: a refused version or a failed import is something
// the integrator must be able to see without recompiling.
#define FL_LOG_ERR(...)                                                                       \
    do {                                                                                      \
        fprintf(stderr, "[framelink error] ");                                                \
        fprintf(stderr, __VA_ARGS__);                                                         \
        fputc('\n', stderr);                                                                  \
    } while (0)

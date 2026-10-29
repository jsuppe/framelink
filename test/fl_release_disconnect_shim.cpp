// Test-only interposition: the real backend completes ownership bookkeeping,
// then we inject the result of its peer-notification send failing. No production
// hook, ring alteration or fence bypass is introduced.
#include "framelink.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
extern "C" flResult flRelease(flChannel* channel, const flFrame* frame) {
    using Release = flResult (*)(flChannel*, const flFrame*);
    static auto realRelease = reinterpret_cast<Release>(dlsym(RTLD_NEXT, "flRelease"));
    if (!realRelease) std::abort();
    static bool injected = false;
    const flResult result = realRelease(channel, frame);
    if (result == FL_OK && !injected) {
        injected = true;
        std::fprintf(stderr, "injected released-peer disconnect\n");
        return FL_DISCONNECTED;
    }
    return result;
}

extern "C" flResult flSignalConsumedSyncFd(flChannel* channel, uint64_t value, int fd) {
    using Signal = flResult (*)(flChannel*, uint64_t, int);
    static auto realSignal = reinterpret_cast<Signal>(dlsym(RTLD_NEXT, "flSignalConsumedSyncFd"));
    if (!realSignal) std::abort();
    static bool injected = false;
    const flResult result = realSignal(channel, value, fd);
    if (result == FL_OK && !injected) {
        injected = true;
        std::fprintf(stderr, "injected consumed-peer disconnect\n");
        return FL_DISCONNECTED;
    }
    return result;
}

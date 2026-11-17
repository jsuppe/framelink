#pragma once
#include <cstdint>
#include <cstring>
#include <utility>

namespace fl {
// Plane must expose data, rowStride and pixelStride (as AHB planes do).
// Only the contiguous VU layout is eligible; other YUV layouts use the
// caller's generic conversion. Geometry/allocation limits remain caller-owned.
template<class Plane>
bool isNv21(const Plane* p, unsigned w, unsigned h) {
    return w && h && !(w & 1) && !(h & 1) &&
        p[0].data && p[1].data && p[2].data &&
        p[0].pixelStride == 1 && p[1].pixelStride == 2 && p[2].pixelStride == 2 &&
        p[0].rowStride >= w && p[1].rowStride >= w &&
        p[1].rowStride == p[2].rowStride &&
        reinterpret_cast<uintptr_t>(p[1].data) > reinterpret_cast<uintptr_t>(p[2].data) &&
        reinterpret_cast<uintptr_t>(p[1].data) - reinterpret_cast<uintptr_t>(p[2].data) == 1;
}

template<class Plane>
bool readNv21(const Plane* p, unsigned w, unsigned h, uint8_t* nv12) {
    if (!isNv21(p, w, h)) return false;
    for (unsigned y = 0; y < h; ++y)
        std::memcpy(nv12 + size_t(y) * w,
                    static_cast<const uint8_t*>(p[0].data) + size_t(y) * p[0].rowStride, w);
    auto* uv = nv12 + size_t(w) * h;
    for (unsigned y = 0; y < h / 2; ++y) {
        auto* row = uv + size_t(y) * w;
        // Bulk read uncached native memory, then swizzle only cached scratch.
        std::memcpy(row, static_cast<const uint8_t*>(p[2].data) + size_t(y) * p[2].rowStride, w);
        for (unsigned x = 0; x < w; x += 2) std::swap(row[x], row[x + 1]);
    }
    return true;
}

// Consumes the normalized scratch: its UV bytes become VU. Call only at
// unmap, when public mapped pointers cease to be valid. The next map replaces
// every logical byte. AHB unlock is still required after writeback.
template<class Plane>
bool writeNv21(const Plane* p, unsigned w, unsigned h, uint8_t* nv12) {
    if (!isNv21(p, w, h)) return false;
    for (unsigned y = 0; y < h; ++y)
        std::memcpy(static_cast<uint8_t*>(p[0].data) + size_t(y) * p[0].rowStride,
                    nv12 + size_t(y) * w, w);
    auto* uv = nv12 + size_t(w) * h;
    for (unsigned y = 0; y < h / 2; ++y) {
        auto* row = uv + size_t(y) * w;
        for (unsigned x = 0; x < w; x += 2) std::swap(row[x], row[x + 1]);
        std::memcpy(static_cast<uint8_t*>(p[2].data) + size_t(y) * p[2].rowStride, row, w);
    }
    return true;
}
} // namespace fl

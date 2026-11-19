#pragma once
// The DRM render node every FrameLink desktop Linux ring uses, shared by the
// core library and the session library so both select the same device.
#include <gbm.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>

namespace fl {
// Render nodes only: no DRM master, no seat, works headless and inside
// containers. The first of renderD128..135 with a working GBM device.
inline gbm_device* openRenderNode(int* fdOut) {
    for (int i = 128; i < 136; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
        const int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        if (gbm_device* g = gbm_create_device(fd)) {
            *fdOut = fd;
            return g;
        }
        ::close(fd);
    }
    return nullptr;
}
// Device identity of an open DRM node: major then minor, little-endian 32-bit
// each. The same encoding as flMemoryResourceInfo.deviceId for
// FL_MEMORY_DEVICE_DRM_NODE; on Linux it is also flChannelInfo.adapterLUID.
inline bool renderNodeId(int fd, uint8_t out[8]) {
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode)) return false;
    const uint32_t values[] = {uint32_t(major(st.st_rdev)), uint32_t(minor(st.st_rdev))};
    for (unsigned n = 0; n < 2; n++) for (unsigned b = 0; b < 4; b++) out[n * 4 + b] = uint8_t(values[n] >> (8 * b));
    return true;
}
// This process's render node identity, as a new ring would get it.
inline bool currentRenderNodeId(uint8_t out[8]) {
    int fd = -1;
    gbm_device* g = openRenderNode(&fd);
    if (!g) return false;
    const bool ok = renderNodeId(fd, out);
    gbm_device_destroy(g);
    ::close(fd);
    return ok;
}
}

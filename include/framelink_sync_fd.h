#pragma once
#include "framelink.h"
#ifndef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
// Linux/Android explicit GPU completion (Windows uses shared timeline fences).
// FDs are borrowed on input; FrameLink transfers a duplicate to the peer.
// -1 means already complete. Export a Vulkan SYNC_FD semaphore or native fence.
// The fence must cover all prior writes/reads on this channel, as with a timeline.
FL_API flResult flSubmitSyncFd(flChannel*, const flBuffer*, int readyFd, int64_t ptsNs);
FL_API flResult flReleaseSyncFd(flChannel*, const flFrame*, int consumedFd);
// FL_GPU_SYNC consumer: borrowed ready fence for the acquired frame, or -1 if
// already complete. dup() before Vulkan import (which consumes ownership).
// Default consumers wait for readiness within flAcquireFrame's timeout budget.
// GPU-owned consumer bookkeeping may release the flFrame immediately while
// retaining its storage. Retire a monotonic watermark after ALL corresponding
// GPU reads finish. Value must not exceed the latest acquired frame.
FL_API flResult flSignalConsumedSyncFd(flChannel*, uint64_t value, int consumedFd);
FL_API flResult flFrameReadySyncFd(flChannel*, const flFrame*, int* readyFd);
#ifdef __cplusplus
}
#endif
#endif // !_WIN32

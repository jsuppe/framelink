#pragma once
// Consumer-side frames the producer has announced but the consumer has not
// acquired yet, oldest first. Serialized by the owning channel's pendingMutex.
//
// Depth 1 is the original latest-frame-wins mailbox: a new announcement
// replaces the unread one. A deeper queue keeps up to `depth` unread frames in
// order and replaces only the oldest when full, so a consumer that is briefly
// late (a scheduling hiccup, not a slow consumer) still reads every frame.
// Release stays monotonic: releasing value V frees every slot announced at or
// before V, which covers replaced frames exactly as it always has.
#include "framelink.h"
#include <array>
#include <cstdint>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace fl {
struct PendingFrame {
    uint32_t slot = 0;
    uint64_t ready = 0;
    int64_t pts = -1;
    uint64_t sequence = 0;
    uint64_t arrival = 0;
    flFrameMetadata metadata{};
    int fence = -1; // POSIX readiness sync file, owned; unused on Windows
};

class PendingFrames {
public:
    static constexpr uint32_t MaxDepth = FL_MAX_RECEIVE_QUEUE;
    ~PendingFrames() { clear(); }
    bool empty() const { return count_ == 0; }
    uint32_t size() const { return count_; }
    uint32_t depth() const { return depth_; }
    const PendingFrame& front() const { return frames_[head_]; }
    // Returns true when the queue was full and its oldest frame was replaced.
    bool push(const PendingFrame& frame) {
        const bool replaced = count_ == depth_;
        if (replaced) drop();
        frames_[(head_ + count_) % MaxDepth] = frame;
        ++count_;
        return replaced;
    }
    // Ownership of front().fence passes to the caller.
    PendingFrame pop() {
        PendingFrame f = frames_[head_];
        frames_[head_].fence = -1;
        head_ = (head_ + 1) % MaxDepth;
        --count_;
        return f;
    }
    // Shrinking discards the oldest unread frames; growing keeps them.
    void setDepth(uint32_t depth) {
        depth_ = depth;
        while (count_ > depth_) drop();
    }
    void clear() { while (count_) drop(); }
    template <class Pred> bool all(Pred pred) const {
        for (uint32_t i = 0; i < count_; ++i)
            if (!pred(frames_[(head_ + i) % MaxDepth])) return false;
        return true;
    }

private:
    void drop() {
        PendingFrame f = pop();
#ifndef _WIN32
        if (f.fence >= 0) close(f.fence);
#endif
    }
    std::array<PendingFrame, MaxDepth> frames_{};
    uint32_t head_ = 0, count_ = 0, depth_ = 1;
};
}

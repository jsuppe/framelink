#pragma once
#include "framelink.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace fl {
// Serialized by the owning channel. Tokens are process-unique, never recycled;
// generation and canonical frame data are retained here, not trusted from callers.
// A completed later frame cannot retire an earlier live reader on a timeline fence.
class ReadLedger {
public:
    static constexpr size_t MaxReaders = 256;
    struct Retirement { uint64_t generation = 0, value = 0; };
private:
    struct Frame { flFrame frame{}; uint64_t generation = 0; size_t readers = 0; };
    struct Reader { uint64_t token = 0; size_t frame = 0; bool pending = false; };
    std::array<Frame, FL_MAX_POOL_SIZE> frames_{};
    std::array<Reader, MaxReaders> readers_{};
    size_t head_ = 0, count_ = 0;
    uint64_t generation_ = 0, lastValue_ = 0;
    inline static std::atomic<uint64_t> nextToken_{1};
    static uint64_t token() {
        auto n = nextToken_.load(std::memory_order_relaxed);
        while (n != UINT64_MAX) {
            if (nextToken_.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) return n;
        }
        return 0; // Fail closed at exhaustion; never wrap into a stale token.
    }
    Reader* find(uint64_t t, bool pending = false) {
        if (!t) return nullptr;
        for (auto& r : readers_) if (r.token == t && r.pending == pending) return &r;
        return nullptr;
    }
    flResult addReader(size_t f, uint64_t& out) {
        for (auto& r : readers_) if (!r.token) {
            auto t = token(); if (!t) return FL_INTERNAL;
            r = {t, f}; ++frames_[f].readers; out = t; return FL_OK;
        }
        return FL_BUSY;
    }
public:
    ReadLedger() = default;
    ReadLedger(const ReadLedger&) = delete;
    ReadLedger& operator=(const ReadLedger&) = delete;
    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    void resetEpoch() { if(empty()) { generation_=0; lastValue_=0; } }
    flResult acquire(uint64_t generation, const flFrame& frame, uint64_t& out) {
        if (!generation || !frame.image || !frame._readyValue || frame.slot >= FL_MAX_POOL_SIZE)
            return FL_INVALID;
        if (count_ == frames_.size()) return FL_BUSY;
        if (generation_ != generation) {
            if (count_) return FL_BUSY;
        } else if (frame._readyValue <= lastValue_) return FL_INVALID;
        for (size_t i = 0; i < count_; ++i)
            if (frames_[(head_ + i) % frames_.size()].frame.slot == frame.slot) return FL_BUSY;
        const size_t f = (head_ + count_) % frames_.size();
        frames_[f] = {frame, generation, 0};
        auto result = addReader(f, out);
        if (result != FL_OK) { frames_[f] = {}; return result; }
        generation_ = generation; lastValue_ = frame._readyValue; ++count_;
        return FL_OK;
    }
    flResult retain(uint64_t source, uint64_t& out) {
        auto* r = find(source); return r ? addReader(r->frame, out) : FL_INVALID;
    }
    flResult get(uint64_t t, flFrame& out, uint64_t& generation) {
        auto* r = find(t); if (!r) return FL_INVALID;
        out = frames_[r->frame].frame; generation = frames_[r->frame].generation;
        return FL_OK;
    }
    // Only call after this reader's actual CPU/GPU work has completed.
    flResult complete(uint64_t t) {
        auto* r = find(t); if (!r) return FL_INVALID;
        --frames_[r->frame].readers; *r = {}; return FL_OK;
    }
    flResult defer(uint64_t t) {
        auto* r=find(t); if(!r)return FL_INVALID; r->pending=true; return FL_OK;
    }
    flResult finishDeferred(uint64_t t) {
        auto* r=find(t,true); if(!r)return FL_INVALID;
        --frames_[r->frame].readers; *r={}; return FL_OK;
    }
    Retirement retirement() const {
        Retirement result{};
        for (size_t i = 0; i < count_; ++i) {
            const auto& f = frames_[(head_ + i) % frames_.size()];
            if (f.readers) break;
            result = {f.generation, f.frame._readyValue};
        }
        return result;
    }
    // Separate peek/ack: backend signal failure must not discard retirement.
    flResult retired(Retirement value) {
        auto ready = retirement();
        if (!value.value || value.value != ready.value || value.generation != ready.generation)
            return FL_INVALID;
        while (count_) {
            auto& f = frames_[head_];
            if (f.readers || f.frame._readyValue > value.value) break;
            f = {}; head_ = (head_ + 1) % frames_.size(); --count_;
        }
        return FL_OK;
    }
};
}

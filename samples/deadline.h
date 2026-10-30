#pragma once
#include <glib.h>
#include <cstdio>
#include <cstdlib>

// Bound blocking pipeline pushes and teardown as well as appsink pulls.
// This executable fixture exits on timeout; it is not a library cancellation API.
class SampleDeadline {
    GMutex mutex_{};
    GCond cond_{};
    bool done_ = false;
    gint64 end_;
    GThread* thread_;
    static gpointer run(gpointer data) {
        auto* self = static_cast<SampleDeadline*>(data);
        g_mutex_lock(&self->mutex_);
        while (!self->done_) {
            if (!g_cond_wait_until(&self->cond_, &self->mutex_, self->end_) && !self->done_) {
                std::fprintf(stderr, "FAIL: media fixture exceeded its 20-second deadline\n");
                std::fflush(stderr);
                std::_Exit(1);
            }
        }
        g_mutex_unlock(&self->mutex_);
        return nullptr;
    }
public:
    SampleDeadline() : end_(g_get_monotonic_time() + 20 * G_TIME_SPAN_SECOND) {
        g_mutex_init(&mutex_);
        g_cond_init(&cond_);
        thread_ = g_thread_new("media-deadline", run, this);
    }
    ~SampleDeadline() {
        g_mutex_lock(&mutex_);
        done_ = true;
        g_cond_signal(&cond_);
        g_mutex_unlock(&mutex_);
        g_thread_join(thread_);
        g_cond_clear(&cond_);
        g_mutex_clear(&mutex_);
    }
    SampleDeadline(const SampleDeadline&) = delete;
    SampleDeadline& operator=(const SampleDeadline&) = delete;
};

// framelink slot sets - see framelink_slots.h for why this exists.
//
// Backend-independent on purpose: it is written entirely against the public
// API, so it behaves identically on Windows NT handles and Linux dma-buf, and
// a new backend gets it for free.

#define FL_EXPORTS
#include "framelink_slots.h"

#include <chrono>
#include <string>
#include <thread>

extern "C" {

FL_API flResult flOpenProducerSlot(const char* prefix, uint32_t slots, uint32_t width,
                                   uint32_t height, flFormat format, uint32_t timeoutMs,
                                   flChannel** out, uint32_t* slotIndex) {
    if (!prefix || !slots || !out) return FL_INVALID;
    *out = nullptr;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        // Tracked across the sweep so an exhausted set and an absent consumer
        // get different answers. They are different problems: one means "wait
        // for a presenter to leave", the other "the renderer is not running".
        bool anyExisted = false;
        for (uint32_t i = 0; i < slots; ++i) {
            const std::string name = std::string(prefix) + "." + std::to_string(i);
            flChannel* ch = nullptr;
            const flResult r = flOpenProducer(name.c_str(), &ch);
            if (r == FL_BUSY) {
                anyExisted = true; // occupied, not missing - keep walking
                continue;
            }
            if (r == FL_NOT_FOUND) continue;
            if (r != FL_OK) return r; // a real failure; do not paper over it
            if (width && height) flRequestGeometry(ch, width, height, format);
            if (slotIndex) *slotIndex = i;
            *out = ch;
            return FL_OK;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            return anyExisted ? FL_BUSY : FL_NOT_FOUND;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // extern "C"

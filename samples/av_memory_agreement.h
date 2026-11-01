#pragma once
#include "av_rendezvous.h"
#include "framelink_memory.h"
#include "framelink_session.h"
#include <gio/gio.h>
namespace av {
struct MemoryOffer {
    flMemoryRequirements requirements{};
    flMemoryPath path{};
    // Trusted allocator/provider-issued digest binding exact pool generation,
    // allocation identities, format/layout/device and completion contract.
    // Never hash a process-local handle or infer this from resource type alone.
    std::array<unsigned char,32> resourceBinding{};
};
// Separate explicitly-negotiated extension on an already authenticated control
// stream. BOTH endpoints must opt in before calling; legacy peers must reject
// that extension, never fall back. Socket must have a finite timeout. No payload
// resources are published until success. Does not probe/establish driver support.
// Fresh transcript nonces and directional HMAC bind both offers and full session
// configuration. Output unchanged on failure. Caller closes session on failure.
bool agreeMemory(GSocketConnection*,const Endpoint&,const flSessionConfig&,
                 bool producer,const MemoryOffer& local,MemoryOffer& remote,
                 std::string& error);
}

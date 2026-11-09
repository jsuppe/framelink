#pragma once
#include "framelink_memory.h"
// Trusted control layer applies this ONLY after authenticating both offers and
// verifying actual immutable resources. Endpoint 0 is this element. This is not
// a replacement for authentication, driver probes or completion integration.
// apply-memory-policy(pointer) returns gboolean; requires NULL + adopted channel.
// The broker-bound framelinkavsrc/avsink bins forward this action to video.
// Configure all bin properties first; successful binding freezes those properties
// until destruction. Reconnect with a new bin/session, not a replacement policy.
// The action does not authenticate its argument or enable session= discovery.
// Once applied, native-memory mode is frozen for that delegated session.
typedef struct GstFrameLinkMemoryPolicy {
    uint32_t version; // 1
    uint32_t reserved;
    uint64_t sessionGeneration; // nonzero
    flMemoryRequirements requirements[2];
    flMemoryPath paths[2];
} GstFrameLinkMemoryPolicy;
// ALLOCATION query field "framelink-memory-requirements": G_TYPE_BYTES containing
// FL_MEMORY_REQUIREMENTS_WIRE_SIZE bytes. Required on policy-bound native sinks.
// This request is evaluated alongside BOTH authenticated endpoint requirements.

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

// Windows A/V bins additionally expose set-local-native-candidate(pointer).
// This is an UNAUTHENTICATED producer-ordered template, not apply-memory-policy.
// Configure all properties first in NULL, including a nonempty session= path;
// producer native-memory=true, receiver native-memory=false (explicit readback),
// neither shared-cpu nor audio-only. Version 1, generation 0, D3D11 GPU_SAMPLE
// producer operations=0 and CPU_READ receiver operations=COPY|READBACK only.
// Both requirements must permit the chosen paths. The action copies the value
// and freezes configuration. Both peers must provide identical templates.
// The setup worker authenticates it before allocating/adopting/applying channels;
// it reorders endpoints for the receiver and preserves the producer pipeline epoch.
// Return TRUE means template accepted, NOT peer authentication or setup success.
// Errors surface on the bus. One setup attempt per bin; use new bins to reconnect,
// including after cancellation/failure. No implicit fallback. Other platforms
// return FALSE. Existing broker actions and unbound defaults remain unchanged.

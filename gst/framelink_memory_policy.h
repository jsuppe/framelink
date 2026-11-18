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

// A/V bins on Windows (D3D11), desktop Linux (DMA-BUF) and Android
// (AHardwareBuffer) additionally expose set-local-native-candidate(pointer).
// This is an UNAUTHENTICATED producer-ordered template, not apply-memory-policy.
// Configure all properties first in NULL, including a nonempty session= path;
// producer native-memory=true. Receiver native-memory=false selects CPU_READ
// with operations=COPY|READBACK; true selects GPU_SAMPLE with operations=0.
// Neither shared-cpu nor audio-only. Version 1, generation 0, platform-native
// (D3D11, DMA-BUF or AHardwareBuffer) producer
// GPU_SAMPLE or GPU_SAMPLE|GPU_RENDER, operations=0 (GPU_RENDER alone rejects). Receiver property and candidate must match. Native
// receivers require downstream retain-memory-until-gpu-complete-v1 allocation
// agreement and memory retention until actual GPU completion; no implicit copy.
// Both requirements must permit the chosen paths. The action copies the value
// and freezes configuration. Both peers must provide identical templates.
// The setup worker authenticates it before allocating/adopting/applying channels;
// it reorders endpoints for the receiver and preserves the producer pipeline epoch.
// Return TRUE means template accepted, NOT peer authentication or setup success.
// Errors surface on the bus. One setup attempt per bin; use new bins to reconnect,
// including after cancellation/failure. No implicit fallback. Other platforms
// return FALSE. Existing broker actions and unbound defaults remain unchanged.

// Windows bins: set-local-d3d11-device-owner(pointer) -> gboolean, after a
// configuration (and native candidate action, when used), while NULL before setup.
// Also supports the existing explicit D3D11 mapped-copy local A/V profile; never
// shared-cpu/audio-only or broker-adopted channels. Freezes configuration.
// Retains an explicit flD3D11DeviceOwner until bin destruction; setup transfers a
// separate reference to its session/channel. One selection, no mutation after
// setup. A fresh bin/session may reuse the application owner only after old
// channel/snapshot/GstMemory retirement. Rejection leaves existing state intact.
// Not a pointer property, implicit cache, new default, or readiness guarantee.

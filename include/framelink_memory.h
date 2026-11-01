// Opt-in boundary memory policy. This does not allocate or detect driver support.
#pragma once
#include "framelink.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FL_MEMORY_VERSION 1u
#define FL_MEMORY_REQUIREMENTS_WIRE_SIZE 64u

#define FL_MEMORY_CPU_READ       0x01u
#define FL_MEMORY_CPU_WRITE      0x02u
#define FL_MEMORY_GPU_SAMPLE     0x04u
#define FL_MEMORY_GPU_RENDER     0x08u
#define FL_MEMORY_DECODER_OUTPUT 0x10u
#define FL_MEMORY_USAGE_ALL      0x1fu

// Resource API/type, NOT a promise of physical VRAM or system-memory residency.
#define FL_MEMORY_SHARED_CPU     0x01u
#define FL_MEMORY_D3D11          0x02u
#define FL_MEMORY_DMABUF         0x04u
#define FL_MEMORY_AHB            0x08u
#define FL_MEMORY_RESOURCE_ALL   0x0fu

// Additional pixel-payload operations at this boundary, not descriptor/fence
// exchange. Report ALL applicable bits; e.g. readback plus conversion is two.
#define FL_MEMORY_COPY           0x01u
#define FL_MEMORY_READBACK       0x02u
#define FL_MEMORY_UPLOAD         0x04u
#define FL_MEMORY_CONVERT        0x08u
#define FL_MEMORY_OPERATIONS_ALL 0x0fu

typedef enum flMemoryResult {
    FL_MEMORY_OK = 0,
    FL_MEMORY_INVALID = 1,
    FL_MEMORY_VERSION_MISMATCH = 2,
    FL_MEMORY_INCOMPATIBLE = 3
} flMemoryResult;

typedef struct flMemoryRequirements {
    uint32_t version;
    uint32_t requiredUsage;
    uint32_t preferredUsage;       // May overlap required; never weakens it.
    uint32_t allowedResourceTypes; // Nonempty mask, not a single preferred type.
    uint32_t allowedOperations;    // Zero = strict no boundary payload operations.
    uint32_t reserved[3];          // Must be zero.
} flMemoryRequirements;

// A platform adapter describes a candidate for an EXACT agreed format/layout,
// device, ownership/synchronization arrangement and endpoint. These are not
// global capabilities. Evaluate BOTH endpoints before accepting an allocation.
typedef struct flMemoryPath {
    uint32_t version;
    uint32_t resourceType;         // Exactly one known resource bit.
    uint32_t supportedUsage;
    uint32_t operations;
    uint32_t reserved[4];          // Must be zero.
} flMemoryPath;

typedef struct flMemoryDecision {
    uint32_t missingRequiredUsage;
    uint32_t unmetPreferredUsage;
    uint32_t rejectedResourceType;
    uint32_t forbiddenOperations;
} flMemoryDecision;

// Explicit opt-in initialization: any known resource, no usage demands and no
// payload operations permitted. Existing channel/session defaults are untouched.
FL_API flMemoryResult flMemoryRequirementsInit(flMemoryRequirements*);
FL_API flMemoryResult flMemoryRequirementsValidate(const flMemoryRequirements*);
FL_API flMemoryResult flMemoryPathValidate(const flMemoryPath*);
// On OK or INCOMPATIBLE, publishes all policy diagnostics. On invalid/version
// error leaves output unchanged. Missing preferences alone do not reject a path.
// Success checks declared policy only; it is NOT zero-copy/driver qualification.
FL_API flMemoryResult flMemoryEvaluate(const flMemoryRequirements*,
    const flMemoryPath*, flMemoryDecision*);
// Separate fixed-size LE message; never append to/reinterpret the v1 A/V session
// wire format. Unknown versions/bits and nonzero reserved bytes reject. Caller
// must negotiate this extension explicitly; no strict downgrade to legacy peers.
FL_API flMemoryResult flMemoryRequirementsEncode(const flMemoryRequirements*, void*, size_t);
FL_API flMemoryResult flMemoryRequirementsDecode(const void*, size_t, flMemoryRequirements*);
#ifdef __cplusplus
}
#endif

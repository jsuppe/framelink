#define FL_EXPORTS
#include "framelink_memory.h"
#include <cstring>

namespace {
template <size_t N> bool zero(const uint32_t (&values)[N]) {
    for (auto value : values) if (value) return false;
    return true;
}
void put(uint8_t*& p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) { *p++ = uint8_t(value); value >>= 8; }
}
uint32_t get(const uint8_t*& p) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= uint32_t(*p++) << (8 * i);
    return value;
}
}

extern "C" {
flMemoryResult flMemoryRequirementsInit(flMemoryRequirements* out) {
    if (!out) return FL_MEMORY_INVALID;
    flMemoryRequirements value{};
    value.version = FL_MEMORY_VERSION;
    value.allowedResourceTypes = FL_MEMORY_RESOURCE_ALL;
    *out = value;
    return FL_MEMORY_OK;
}

flMemoryResult flMemoryRequirementsValidate(const flMemoryRequirements* r) {
    if (!r) return FL_MEMORY_INVALID;
    if (r->version != FL_MEMORY_VERSION) return FL_MEMORY_VERSION_MISMATCH;
    if (((r->requiredUsage | r->preferredUsage) & ~FL_MEMORY_USAGE_ALL) ||
        !r->allowedResourceTypes || (r->allowedResourceTypes & ~FL_MEMORY_RESOURCE_ALL) ||
        (r->allowedOperations & ~FL_MEMORY_OPERATIONS_ALL) || !zero(r->reserved))
        return FL_MEMORY_INVALID;
    return FL_MEMORY_OK;
}

flMemoryResult flMemoryPathValidate(const flMemoryPath* p) {
    if (!p) return FL_MEMORY_INVALID;
    if (p->version != FL_MEMORY_VERSION) return FL_MEMORY_VERSION_MISMATCH;
    if (!p->resourceType || (p->resourceType & (p->resourceType - 1)) ||
        (p->resourceType & ~FL_MEMORY_RESOURCE_ALL) ||
        (p->supportedUsage & ~FL_MEMORY_USAGE_ALL) ||
        (p->operations & ~FL_MEMORY_OPERATIONS_ALL) || !zero(p->reserved))
        return FL_MEMORY_INVALID;
    return FL_MEMORY_OK;
}

flMemoryResult flMemoryEvaluate(const flMemoryRequirements* r,
                               const flMemoryPath* p, flMemoryDecision* out) {
    if (!out) return FL_MEMORY_INVALID;
    auto result = flMemoryRequirementsValidate(r);
    if (result != FL_MEMORY_OK) return result;
    result = flMemoryPathValidate(p);
    if (result != FL_MEMORY_OK) return result;
    flMemoryDecision d{};
    d.missingRequiredUsage = r->requiredUsage & ~p->supportedUsage;
    d.unmetPreferredUsage = r->preferredUsage & ~p->supportedUsage;
    d.rejectedResourceType = p->resourceType & ~r->allowedResourceTypes;
    d.forbiddenOperations = p->operations & ~r->allowedOperations;
    *out = d;
    return (d.missingRequiredUsage || d.rejectedResourceType || d.forbiddenOperations)
        ? FL_MEMORY_INCOMPATIBLE : FL_MEMORY_OK;
}

flMemoryResult flMemoryRequirementsEncode(const flMemoryRequirements* r,
                                         void* data, size_t size) {
    if (!data || size != FL_MEMORY_REQUIREMENTS_WIRE_SIZE) return FL_MEMORY_INVALID;
    auto result = flMemoryRequirementsValidate(r);
    if (result != FL_MEMORY_OK) return result;
    // Stage before writing, preserving output on error and permitting aliasing.
    uint8_t bytes[FL_MEMORY_REQUIREMENTS_WIRE_SIZE]{};
    auto p = bytes;
    put(p, 0x31524d46u); // FMR1
    put(p, r->version);
    put(p, r->requiredUsage);
    put(p, r->preferredUsage);
    put(p, r->allowedResourceTypes);
    put(p, r->allowedOperations);
    std::memcpy(data, bytes, sizeof(bytes));
    return FL_MEMORY_OK;
}

flMemoryResult flMemoryRequirementsDecode(const void* data, size_t size,
                                         flMemoryRequirements* out) {
    if (!data || !out || size != FL_MEMORY_REQUIREMENTS_WIRE_SIZE) return FL_MEMORY_INVALID;
    auto p = static_cast<const uint8_t*>(data);
    auto end = p + size;
    if (get(p) != 0x31524d46u) return FL_MEMORY_INVALID;
    flMemoryRequirements r{};
    r.version = get(p);
    if (r.version != FL_MEMORY_VERSION) return FL_MEMORY_VERSION_MISMATCH;
    r.requiredUsage = get(p);
    r.preferredUsage = get(p);
    r.allowedResourceTypes = get(p);
    r.allowedOperations = get(p);
    while (p < end) if (*p++) return FL_MEMORY_INVALID;
    auto result = flMemoryRequirementsValidate(&r);
    if (result == FL_MEMORY_OK) *out = r;
    return result;
}
}

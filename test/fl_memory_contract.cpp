#include "framelink_memory.h"
#include "framelink_session.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
    static_assert(FL_VERSION == 6 && FL_SESSION_VERSION == 1 && FL_SESSION_WIRE_SIZE == 160,
                  "The opt-in memory contract must not silently change legacy protocols");
    flMemoryRequirements r{};
    CHECK(flMemoryRequirementsInit(&r) == FL_MEMORY_OK);
    CHECK(r.allowedOperations == 0 && r.allowedResourceTypes == FL_MEMORY_RESOURCE_ALL);
    CHECK(flMemoryRequirementsInit(nullptr) == FL_MEMORY_INVALID);
    r.requiredUsage = FL_MEMORY_GPU_SAMPLE;
    r.preferredUsage = FL_MEMORY_CPU_READ;
    flMemoryPath native{FL_MEMORY_VERSION, FL_MEMORY_D3D11, FL_MEMORY_GPU_SAMPLE, 0, {}};
    flMemoryDecision d{};
    CHECK(flMemoryEvaluate(&r, &native, &d) == FL_MEMORY_OK);
    CHECK(!d.missingRequiredUsage && d.unmetPreferredUsage == FL_MEMORY_CPU_READ);

    // A GPU handle does not authorize hidden staging/readback for a CPU reader.
    auto staging = native;
    staging.supportedUsage |= FL_MEMORY_CPU_READ;
    staging.operations = FL_MEMORY_READBACK | FL_MEMORY_COPY;
    CHECK(flMemoryEvaluate(&r, &staging, &d) == FL_MEMORY_INCOMPATIBLE);
    CHECK(d.forbiddenOperations == (FL_MEMORY_READBACK | FL_MEMORY_COPY));
    r.allowedOperations = FL_MEMORY_READBACK;
    CHECK(flMemoryEvaluate(&r, &staging, &d) == FL_MEMORY_INCOMPATIBLE);
    CHECK(d.forbiddenOperations == FL_MEMORY_COPY);
    r.allowedOperations |= FL_MEMORY_COPY;
    CHECK(flMemoryEvaluate(&r, &staging, &d) == FL_MEMORY_OK);
    CHECK(!d.unmetPreferredUsage && !d.forbiddenOperations);
    r.requiredUsage |= FL_MEMORY_CPU_READ;
    CHECK(flMemoryEvaluate(&r, &native, &d) == FL_MEMORY_INCOMPATIBLE);
    CHECK(d.missingRequiredUsage == FL_MEMORY_CPU_READ); // Preferences never weaken required usage.
    r.allowedResourceTypes = FL_MEMORY_DMABUF;
    CHECK(flMemoryEvaluate(&r, &staging, &d) == FL_MEMORY_INCOMPATIBLE);
    CHECK(d.rejectedResourceType == FL_MEMORY_D3D11);

    // Evaluate distinct producer and consumer needs, not their usage intersection.
    flMemoryRequirements producer{}, consumer{};
    CHECK(flMemoryRequirementsInit(&producer) == FL_MEMORY_OK);
    CHECK(flMemoryRequirementsInit(&consumer) == FL_MEMORY_OK);
    producer.requiredUsage = FL_MEMORY_DECODER_OUTPUT;
    consumer.requiredUsage = FL_MEMORY_GPU_SAMPLE;
    auto shared = native;
    shared.supportedUsage = FL_MEMORY_DECODER_OUTPUT | FL_MEMORY_GPU_SAMPLE;
    CHECK(flMemoryEvaluate(&producer, &shared, &d) == FL_MEMORY_OK);
    CHECK(flMemoryEvaluate(&consumer, &shared, &d) == FL_MEMORY_OK);
    shared.supportedUsage = FL_MEMORY_GPU_SAMPLE;
    CHECK(flMemoryEvaluate(&producer, &shared, &d) == FL_MEMORY_INCOMPATIBLE);

    // Each resource is an API identity, not a residency guess. Validate policy
    // uniformly; backend/device/layout qualification is a separate gate.
    for (uint32_t type : {FL_MEMORY_SHARED_CPU, FL_MEMORY_D3D11, FL_MEMORY_DMABUF, FL_MEMORY_AHB}) {
        native.resourceType = type;
        CHECK(flMemoryEvaluate(&consumer, &native, &d) == FL_MEMORY_OK);
    }
    native.resourceType = FL_MEMORY_D3D11;
    for (uint32_t operation : {FL_MEMORY_COPY, FL_MEMORY_READBACK, FL_MEMORY_UPLOAD, FL_MEMORY_CONVERT}) {
        native.operations = operation;
        CHECK(flMemoryEvaluate(&consumer, &native, &d) == FL_MEMORY_INCOMPATIBLE);
        CHECK(d.forbiddenOperations == operation);
    }
    native.operations = 0;

    const flMemoryDecision sentinel{123, 456, 789, 321};
    for (uint32_t version : {0u, 2u, UINT32_MAX}) {
        auto bad = consumer; bad.version = version; d = sentinel;
        CHECK(flMemoryEvaluate(&bad, &native, &d) == FL_MEMORY_VERSION_MISMATCH);
        CHECK(!std::memcmp(&d, &sentinel, sizeof(d)));
        auto badPath = native; badPath.version = version;
        CHECK(flMemoryEvaluate(&consumer, &badPath, &d) == FL_MEMORY_VERSION_MISMATCH);
        CHECK(!std::memcmp(&d, &sentinel, sizeof(d)));
    }
    for (unsigned bit = 5; bit < 32; ++bit) {
        auto bad = consumer; bad.preferredUsage |= uint32_t(1) << bit;
        CHECK(flMemoryRequirementsValidate(&bad) == FL_MEMORY_INVALID);
        bad = consumer; bad.requiredUsage |= uint32_t(1) << bit;
        CHECK(flMemoryRequirementsValidate(&bad) == FL_MEMORY_INVALID);
        auto badPath = native; badPath.supportedUsage |= uint32_t(1) << bit;
        CHECK(flMemoryPathValidate(&badPath) == FL_MEMORY_INVALID);
    }
    for (unsigned bit = 4; bit < 32; ++bit) {
        auto bad = consumer; bad.allowedOperations |= uint32_t(1) << bit;
        CHECK(flMemoryRequirementsValidate(&bad) == FL_MEMORY_INVALID);
        bad = consumer; bad.allowedResourceTypes |= uint32_t(1) << bit;
        CHECK(flMemoryRequirementsValidate(&bad) == FL_MEMORY_INVALID);
        auto badPath = native; badPath.operations |= uint32_t(1) << bit;
        CHECK(flMemoryPathValidate(&badPath) == FL_MEMORY_INVALID);
        badPath = native; badPath.resourceType = uint32_t(1) << bit;
        CHECK(flMemoryPathValidate(&badPath) == FL_MEMORY_INVALID);
    }
    for (uint32_t resource : {0u, FL_MEMORY_D3D11 | FL_MEMORY_DMABUF, UINT32_MAX}) {
        auto bad = native; bad.resourceType = resource;
        CHECK(flMemoryPathValidate(&bad) == FL_MEMORY_INVALID);
    }
    auto bad = consumer; bad.allowedResourceTypes = 0;
    CHECK(flMemoryRequirementsValidate(&bad) == FL_MEMORY_INVALID);
    for (unsigned i = 0; i < 3; ++i) {
        bad = consumer; bad.reserved[i] = 1;
        CHECK(flMemoryRequirementsValidate(&bad) == FL_MEMORY_INVALID);
    }
    for (unsigned i = 0; i < 4; ++i) {
        auto badPath = native; badPath.reserved[i] = 1; d = sentinel;
        CHECK(flMemoryEvaluate(&consumer, &badPath, &d) == FL_MEMORY_INVALID);
        CHECK(!std::memcmp(&d, &sentinel, sizeof(d)));
    }
    CHECK(flMemoryEvaluate(nullptr, &native, &d) == FL_MEMORY_INVALID);
    CHECK(flMemoryEvaluate(&consumer, nullptr, &d) == FL_MEMORY_INVALID);
    CHECK(flMemoryEvaluate(&consumer, &native, nullptr) == FL_MEMORY_INVALID);

    // Independent golden byte vector fixes endian/offset/padding interoperability.
    r = {1, FL_MEMORY_CPU_READ, FL_MEMORY_GPU_SAMPLE, FL_MEMORY_DMABUF, FL_MEMORY_READBACK, {}};
    uint8_t wire[64];
    const uint8_t expected[64] = {'F','M','R','1',1,0,0,0,1,0,0,0,4,0,0,0,4,0,0,0,2,0,0,0};
    CHECK(flMemoryRequirementsEncode(&r, wire, sizeof(wire)) == FL_MEMORY_OK);
    CHECK(!std::memcmp(wire, expected, sizeof(wire)));
    flMemoryRequirements decoded{};
    CHECK(flMemoryRequirementsDecode(expected, sizeof(expected), &decoded) == FL_MEMORY_OK);
    CHECK(!std::memcmp(&decoded, &r, sizeof(r)));
    const auto untouched = decoded;
    for (size_t size = 0; size < 64; ++size) {
        CHECK(flMemoryRequirementsDecode(wire, size, &decoded) == FL_MEMORY_INVALID);
        CHECK(!std::memcmp(&decoded, &untouched, sizeof(decoded)));
    }
    CHECK(flMemoryRequirementsDecode(wire, 65, &decoded) == FL_MEMORY_INVALID);
    CHECK(flMemoryRequirementsDecode(wire, SIZE_MAX, &decoded) == FL_MEMORY_INVALID);
    for (size_t i = 24; i < 64; ++i) {
        wire[i] = 1;
        CHECK(flMemoryRequirementsDecode(wire, sizeof(wire), &decoded) == FL_MEMORY_INVALID);
        CHECK(!std::memcmp(&decoded, &untouched, sizeof(decoded)));
        wire[i] = 0;
    }
    wire[4] = 2;
    CHECK(flMemoryRequirementsDecode(wire, sizeof(wire), &decoded) == FL_MEMORY_VERSION_MISMATCH);
    wire[4] = 1; wire[0] = 0;
    CHECK(flMemoryRequirementsDecode(wire, sizeof(wire), &decoded) == FL_MEMORY_INVALID);
    std::memcpy(wire, expected, 64); wire[11] = 0x80;
    CHECK(flMemoryRequirementsDecode(wire, 64, &decoded) == FL_MEMORY_INVALID);
    std::memset(wire, 0xa5, sizeof(wire)); bad = r; bad.version = 2;
    CHECK(flMemoryRequirementsEncode(&bad, wire, 64) == FL_MEMORY_VERSION_MISMATCH);
    for (auto byte : wire) CHECK(byte == 0xa5);
    CHECK(flMemoryRequirementsEncode(&r, wire, 63) == FL_MEMORY_INVALID);
    for (auto byte : wire) CHECK(byte == 0xa5);
    CHECK(flMemoryRequirementsEncode(nullptr, wire, 64) == FL_MEMORY_INVALID);
    CHECK(flMemoryRequirementsEncode(&r, nullptr, 64) == FL_MEMORY_INVALID);
    CHECK(flMemoryRequirementsDecode(nullptr, 64, &decoded) == FL_MEMORY_INVALID);
    CHECK(flMemoryRequirementsDecode(wire, 64, nullptr) == FL_MEMORY_INVALID);
    std::puts("memory policy: strict/fallback, hard/preferred usage, versions, golden wire and malformed inputs passed");
}

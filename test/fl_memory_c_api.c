/* Compile as C11 and link the exported library, not a C++ header-only mock. */
#include "framelink_memory.h"
int main(void) {
    flMemoryRequirements request = {0}, decoded = {0};
    flMemoryPath path = {FL_MEMORY_VERSION, FL_MEMORY_AHB, FL_MEMORY_GPU_SAMPLE, 0, {0}};
    flMemoryDecision decision = {0};
    unsigned char wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];
    if (flMemoryRequirementsInit(&request) != FL_MEMORY_OK) return 1;
    request.requiredUsage = FL_MEMORY_GPU_SAMPLE;
    if (flMemoryEvaluate(&request, &path, &decision) != FL_MEMORY_OK) return 2;
    path.operations = FL_MEMORY_UPLOAD;
    if (flMemoryEvaluate(&request, &path, &decision) != FL_MEMORY_INCOMPATIBLE) return 3;
    if (decision.forbiddenOperations != FL_MEMORY_UPLOAD) return 4;
    if (flMemoryRequirementsEncode(&request, wire, sizeof(wire)) != FL_MEMORY_OK) return 5;
    if (flMemoryRequirementsDecode(wire, sizeof(wire), &decoded) != FL_MEMORY_OK) return 6;
    return decoded.requiredUsage != FL_MEMORY_GPU_SAMPLE;
}

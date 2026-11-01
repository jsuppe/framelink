#pragma once
#include <chrono>
#include <cstdint>
namespace fl {
inline uint64_t arrivalNowNs() {
 return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
     std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

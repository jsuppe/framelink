#pragma once
#include "framelink.h"
#include "framelink_slots.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include "framelink_d3d11.h"
#include <d3d11_4.h>
#include <wrl/client.h>
#elif !defined(__ANDROID__)
#include "framelink_dmabuf.h"
#endif
namespace sample {
using Clock = std::chrono::steady_clock;
inline int64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}
inline void require(bool ok, const std::string &what) {
  if (!ok)
    throw std::runtime_error(what);
}
inline void check(flResult r, const char *what) {
  if (r != FL_OK)
    throw std::runtime_error(std::string(what) + ": " + flResultString(r));
}
struct Options {
  std::string channel = "framelink.sample";
  uint32_t width = 640, height = 360, pool = 4, frames = 120, timeout = 100,
           deadline = 10000, fps = 60, delay = 0, slots = 0, reconnect = 0,
           corrupt = UINT32_MAX;
#ifdef __ANDROID__
  flFormat format = FL_FORMAT_RGBA8;
#else
  flFormat format = FL_FORMAT_BGRA8;
#endif
  bool sizeSet = false, formatSet = false, verify = true, gpu = false,
       manual = false, help = false, adapterSet = false;
#ifdef _WIN32
  bool map = false;
#else
  bool map = true;
#endif
  uint8_t adapter[8]{};
};
Options parse(int argc, char **argv, bool producer);
void usage(bool producer);
inline const char *formatName(flFormat f) {
  return f == FL_FORMAT_NV12 ? "NV12" : f == FL_FORMAT_BGRA8 ? "BGRA" : f == FL_FORMAT_RGBA8 ? "RGBA" : f == FL_FORMAT_RGB10A2 ? "RGB10A2" : "other";
}
inline void report(flChannel *ch, FILE *stream = stdout) {
  flChannelInfo i{};
  check(flQuery(ch, &i), "query");
  fprintf(stream,
          "channel version=%u format=%s size=%ux%u pool=%u generation=%llu "
          "adapter=",
          i.version, formatName(i.format), i.width, i.height, i.poolSize,
          (unsigned long long)i.generation);
  for (auto b : i.adapterLUID)
    fprintf(stream, "%02x", b);
  fprintf(stream, "\n");
  fflush(stream);
}
struct Channel {
  flChannel *value = nullptr;
  ~Channel() {
    if (value)
      flClose(value);
  }
  void close() {
    if (value)
      flClose(value);
    value = nullptr;
  }
};
// Pattern has an in-band little-endian uint64 frame identity in its first 8
// bytes. PTS remains a real monotonic timestamp, not a test frame number.
inline uint8_t pattern(uint64_t id, unsigned p, unsigned y, unsigned x) {
  if (p == 0 && y == 0 && x < 8)
    return (uint8_t)(id >> (x * 8));
  return (uint8_t)(id * 17 + p * 79 + y * 3 + x * 7);
}
class Pixels {
public:
  void write(flChannel *, flImage *, const flChannelInfo &, uint64_t,
             bool corrupt = false);
  uint64_t verify(flChannel *, flImage *, const flChannelInfo &);
  void waitReady(flChannel *, const flFrame &);
  void signalConsumed(flChannel *, const flFrame &);
  flResult submit(flChannel *, const flBuffer &, int64_t, bool);

private:
#ifdef _WIN32
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  Microsoft::WRL::ComPtr<ID3D11Device> stagingDevice_;
#endif
};
} // namespace sample

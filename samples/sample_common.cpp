#include "sample_common.h"
#include <limits>
namespace sample {
void usage(bool producer) {
  printf("FrameLink %s validation sample (Windows/Linux)\n",
         producer ? "producer" : "consumer");
  puts("Common: --channel NAME --frames N --timeout-ms N --deadline-ms N\n"
       "        --format bgra|nv12 --size WIDTHxHEIGHT --help\n"
       "Producer: --fps N (0=unpaced) --slots N --reconnect-at FRAME "
       "--corrupt-at FRAME\n"
       "          --submit auto|manual (manual: Windows explicit ready fence)\n"
       "Consumer: --pool 1..8 --delay-ms N --verify 0|1 --map-cpu 0|1\n"
       "          --gpu-sync 0|1 --adapter 16_HEX_DIGITS (last two: Windows)\n"
       "Defaults: channel=framelink.sample, frames=600 producer/120 consumer, "
       "timeout=100ms, idle deadline=10000ms.\n"
       "Consumer: BGRA 640x360 pool=4 verify=1, Linux map-cpu=1. Producer: "
       "60fps,\n"
       "inherits channel format/geometry unless explicitly requested. Frame "
       "gaps are\n"
       "reported, not corruption. Verification requires this sample producer's "
       "pattern.\n"
       "Exit: 0 success/help; 1 transport/pixel failure; 2 invalid/unsupported "
       "CLI.");
}
Options parse(int argc, char **argv, bool producer) {
  Options o;
  o.frames = producer ? 600 : 120;
  auto number = [](const std::string &s) -> uint32_t {
    require(!s.empty() &&
                s.find_first_not_of("0123456789") == std::string::npos,
            "expected unsigned integer: " + s);
    auto n = std::stoull(s);
    require(n <= UINT32_MAX, "integer out of range");
    return (uint32_t)n;
  };
  for (int i = 1; i < argc; i++) {
    std::string key = argv[i];
    if (key == "--help") {
      o.help = true;
      return o;
    }
    require(i + 1 < argc, "missing value for " + key);
    std::string v = argv[++i];
    auto boolean = [&]() {
      auto n = number(v);
      require(n < 2, "expected 0 or 1 for " + key);
      return n != 0;
    };
    if (key == "--channel") {
      require(!v.empty(), "empty channel");
      o.channel = v;
    } else if (key == "--frames")
      o.frames = number(v);
    else if (key == "--timeout-ms")
      o.timeout = number(v);
    else if (key == "--deadline-ms")
      o.deadline = number(v);
    else if (key == "--format") {
      require(v == "bgra" || v == "nv12", "format must be bgra or nv12");
      o.format = v == "nv12" ? FL_FORMAT_NV12 : FL_FORMAT_BGRA8;
      o.formatSet = true;
    } else if (key == "--size") {
      auto x = v.find('x');
      require(x != std::string::npos, "size must be WIDTHxHEIGHT");
      o.width = number(v.substr(0, x));
      o.height = number(v.substr(x + 1));
      o.sizeSet = true;
    } else if (producer && key == "--fps")
      o.fps = number(v);
    else if (producer && key == "--slots")
      o.slots = number(v);
    else if (producer && key == "--corrupt-at")
      o.corrupt = number(v);
    else if (producer && key == "--reconnect-at")
      o.reconnect = number(v);
    else if (producer && key == "--submit") {
      require(v == "auto" || v == "manual", "submit must be auto or manual");
      o.manual = v == "manual";
    } else if (!producer && key == "--pool")
      o.pool = number(v);
    else if (!producer && key == "--delay-ms")
      o.delay = number(v);
    else if (!producer && key == "--verify")
      o.verify = boolean();
    else if (!producer && key == "--gpu-sync")
      o.gpu = boolean();
    else if (!producer && key == "--map-cpu")
      o.map = boolean();
    else if (!producer && key == "--adapter") {
      require(v.size() == 16 && v.find_first_not_of("0123456789abcdefABCDEF") ==
                                    std::string::npos,
              "adapter must be 16 hex digits in printed byte order");
      o.adapterSet = true;
      for (int j = 0; j < 8; j++)
        o.adapter[j] = (uint8_t)std::stoul(v.substr(j * 2, 2), nullptr, 16);
    } else
      throw std::runtime_error("unknown or wrong-role option: " + key);
  }
  require(o.width >= 8 && o.height > 0 && o.width <= 8192 && o.height <= 8192,
          "size must have width 8..8192, height 1..8192");
  require(o.format != FL_FORMAT_NV12 || (!(o.width & 1) && !(o.height & 1)),
          "NV12 dimensions must be even");
  require(o.frames > 0 && o.pool >= 1 && o.pool <= 8 && o.deadline > 0 &&
              o.timeout <= o.deadline,
          "invalid frames/pool/timeout/deadline");
  require(o.fps <= 10000 && o.delay <= 60000 && o.slots <= 1024,
          "fps/delay/slots too large");
  require(o.corrupt == UINT32_MAX || o.corrupt < o.frames,
          "corrupt-at must be less than frames");
  require(!o.reconnect || o.reconnect < o.frames,
          "reconnect-at must be less than frames");
#ifndef _WIN32
  require(
      !o.gpu && !o.manual && !o.adapterSet,
      "GPU fences, manual submit and adapter LUID selection are Windows-only");
  require(producer || !o.verify || o.map,
          "Linux verification requires --map-cpu 1");
#endif
  return o;
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
static ComPtr<ID3D11DeviceContext4> context(flChannel *ch) {
  ComPtr<ID3D11DeviceContext> base;
  flChannelD3D11Device(ch)->GetImmediateContext(&base);
  ComPtr<ID3D11DeviceContext4> ctx;
  require(SUCCEEDED(base.As(&ctx)), "D3D11 context4 unavailable");
  return ctx;
}
static ComPtr<ID3D11Fence> fence(flChannel *ch, bool ready) {
  void *h = nullptr;
  check(ready ? flSharedReadyFence(ch, &h) : flSharedConsumedFence(ch, &h),
        "shared fence");
  ComPtr<ID3D11Device5> dev;
  require(
      SUCCEEDED(flChannelD3D11Device(ch)->QueryInterface(IID_PPV_ARGS(&dev))),
      "D3D11 device5 unavailable");
  ComPtr<ID3D11Fence> f;
  require(SUCCEEDED(dev->OpenSharedFence(h, IID_PPV_ARGS(&f))),
          "open shared fence");
  return f;
}
#endif
void Pixels::waitReady(flChannel *ch, const flFrame &f) {
#ifdef _WIN32
  require(SUCCEEDED(context(ch)->Wait(fence(ch, true).Get(), f._readyValue)),
          "GPU ready wait failed");
#endif
}
void Pixels::signalConsumed(flChannel *ch, const flFrame &f) {
#ifdef _WIN32
  auto ctx = context(ch);
  require(SUCCEEDED(ctx->Signal(fence(ch, false).Get(), f._readyValue)),
          "GPU consumed signal failed");
  ctx->Flush();
#endif
}
flResult Pixels::submit(flChannel *ch, const flBuffer &b, int64_t pts,
                        bool manual) {
  if (!manual)
    return flSubmit(ch, &b, pts);
#ifdef _WIN32
  uint64_t value = 0;
  check(flBeginSubmit(ch, &b, &value), "begin submit");
  auto ctx = context(ch);
  require(SUCCEEDED(ctx->Signal(fence(ch, true).Get(), value)),
          "GPU ready signal failed");
  ctx->Flush();
  return flEndSubmit(ch, &b, value, pts);
#else
  throw std::runtime_error("manual submit unsupported");
#endif
}
void Pixels::write(flChannel *ch, flImage *image, const flChannelInfo &i,
                   uint64_t id, bool corrupt) {
  require(i.format == FL_FORMAT_BGRA8 || i.format == FL_FORMAT_NV12,
          "unsupported channel format");
  require(i.width >= 8, "pattern needs at least 8 pixels");
#ifdef _WIN32
  unsigned pitch = i.format == FL_FORMAT_NV12 ? i.width : i.width * 4;
  std::vector<uint8_t> bytes((size_t)pitch * i.height *
                             (i.format == FL_FORMAT_NV12 ? 3 : 2) / 2);
  for (unsigned p = 0; p < (i.format == FL_FORMAT_NV12 ? 2u : 1u); p++)
    for (unsigned y = 0; y < (p ? i.height / 2 : i.height); y++)
      for (unsigned x = 0; x < pitch; x++)
        bytes[(p ? (size_t)pitch * i.height : 0) + (size_t)y * pitch + x] =
            pattern(id, p, y, x);
  if (corrupt)
    bytes.back() ^= 1;
  context(ch)->UpdateSubresource(flImageD3D11(image), 0, nullptr, bytes.data(),
                                 pitch, 0);
#else
  flMappedImage m{};
  check(flImageMapPlanes(image, &m), "map planes");
  for (unsigned p = 0; p < m.planes; p++)
    for (unsigned y = 0; y < m.plane[p].rows; y++)
      for (unsigned x = 0; x < m.plane[p].rowBytes; x++)
        ((uint8_t *)m.plane[p].pixels)[y * m.plane[p].stride + x] =
            pattern(id, p, y, x);
  if (corrupt) {
    auto &last = m.plane[m.planes - 1];
    ((uint8_t *)
         last.pixels)[(last.rows - 1) * last.stride + last.rowBytes - 1] ^= 1;
  }
  flImageUnmap(image);
#endif
}
uint64_t Pixels::verify(flChannel *ch, flImage *image, const flChannelInfo &i) {
  const uint8_t *data[2]{};
  uint64_t stride[2]{};
  unsigned planes = i.format == FL_FORMAT_NV12 ? 2 : 1;
  require(i.format == FL_FORMAT_BGRA8 || i.format == FL_FORMAT_NV12,
          "unsupported verification format");
  require(i.width >= 8, "pattern needs at least 8 pixels");
#ifdef _WIN32
  auto dev = flChannelD3D11Device(ch);
  auto ctx = context(ch);
  D3D11_TEXTURE2D_DESC desc{};
  auto tex = flImageD3D11(image);
  tex->GetDesc(&desc);
  D3D11_TEXTURE2D_DESC old{};
  if (staging_)
    staging_->GetDesc(&old);
  if (!staging_ || stagingDevice_.Get() != dev || old.Width != desc.Width ||
      old.Height != desc.Height || old.Format != desc.Format) {
    staging_.Reset();
    stagingDevice_ = dev;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require(SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &staging_)),
            "create readback texture");
  }
  ctx->CopyResource(staging_.Get(), tex);
  D3D11_MAPPED_SUBRESOURCE m{};
  require(SUCCEEDED(ctx->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m)),
          "readback map");
  data[0] = (uint8_t *)m.pData;
  data[1] = data[0] + (size_t)m.RowPitch * i.height;
  stride[0] = stride[1] = m.RowPitch;
#else
  flMappedImage m{};
  check(flImageMapPlanes(image, &m), "map planes");
  require(m.planes == planes, "wrong plane count");
  for (unsigned p = 0; p < planes; p++) {
    data[p] = (uint8_t *)m.plane[p].pixels;
    stride[p] = m.plane[p].stride;
  }
#endif
  uint64_t id = 0;
  for (unsigned x = 0; x < 8; x++)
    id |= (uint64_t)data[0][x] << (x * 8);
  bool valid = true;
  unsigned badP = 0, badY = 0, badX = 0;
  for (unsigned p = 0; p < planes && valid; p++)
    for (unsigned y = 0; y < (p ? i.height / 2 : i.height) && valid; y++)
      for (unsigned x = 0; x < (planes == 2 ? i.width : i.width * 4); x++)
        if (data[p][y * stride[p] + x] != pattern(id, p, y, x)) {
          valid = false;
          badP = p;
          badY = y;
          badX = x;
          break;
        }
#ifdef _WIN32
  ctx->Unmap(staging_.Get(), 0);
#else
  flImageUnmap(image);
#endif
  if (!valid)
    throw std::runtime_error("pixel mismatch frame=" + std::to_string(id) +
                             " plane=" + std::to_string(badP) +
                             " x=" + std::to_string(badX) +
                             " y=" + std::to_string(badY));
  return id;
}
} // namespace sample

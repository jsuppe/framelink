// Independent A/V sample peers. GPU/image transport remains FrameLink;
// PCM uses GStreamer's GDP over an authenticated GIO stream, not a new PCM ABI.
#include "av_auth.h"
#include "audio_timeline.h"
#include "av_playback.h"
#include "av_rendezvous.h"
#include "framelink_session.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <memory>
#include <fstream>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __ANDROID__
#include "android_static_plugins.h"
#endif
namespace {
GstPlugin* loadFrameLinkPlugin(const std::string& path, GError** error) {
#ifdef __ANDROID__
  flRegisterAndroidPlugins();
  return gst_registry_find_plugin(gst_registry_get(), "framelink");
#else
  return gst_plugin_load_file(path.c_str(), error);
#endif
}
void need(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void need(bool ok, const std::string &what) {
  if (!ok)
    throw std::runtime_error(what);
}
struct Options {
  std::string plugin, session, format = "BGRA", fault, inputPipeline;
  bool traceMedia=false;
  bool producerClock=false;
  unsigned frames = 150, width = 160, height = 90, fps = 50, rate = 48000,
           channels = 2, timeout = 15000, sessions = 1;
  bool play = false, stream = false;
  std::string audioSink="autoaudiosink";
  unsigned latencyMs=100;
  uint64_t audioOffsetNs=0;
  int pacingPpm=0;
};
unsigned number(const char *s) {
  char *e = nullptr;
  unsigned long n = strtoul(s, &e, 10);
  need(*s && !*e && n <= 1000000, "invalid number");
  return unsigned(n);
}
Options options(int argc, char **argv) {
  need(argc >= 2, "usage: framelink_av_{producer,consumer} PLUGIN --session "
                  "PRIVATE_DIR [--frames N --format BGRA|NV12 --width W "
                  "--height H --fps N --rate N --channels 1|2 --timeout-ms N "
                  "--sessions N --play --fault missing-audio|audio-pts-reset]");
  Options o;
  o.plugin = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--trace-media") { o.traceMedia=true; continue; }
    if (a == "--stream") { o.stream=true; continue; }
    if (a == "--play") {
      o.play = true;
      continue;
    }
    need(i + 1 < argc, "option requires value");
    const char *v = argv[++i];
    if(a=="--audio-offset-ns") {
      char* end=nullptr;errno=0;auto offset=strtoll(v,&end,10);
      need(*v && end && !*end && !errno && offset>=0 && offset<=2000000000LL,
           "audio offset must be 0..2000000000 ns (video origin is zero)");
      o.audioOffsetNs=uint64_t(offset);
    }
    else if (a == "--pacing-ppm") {
      char* end=nullptr;errno=0;auto ppm=strtol(v,&end,10);
      need(*v && end && !*end && !errno && ppm>=-200000 && ppm<=200000,
           "pacing must be -200000..200000 ppm");
      o.pacingPpm=int(ppm);
    }
    else if (a == "--input-pipeline") o.inputPipeline=v;
    else if (a == "--audio-sink") o.audioSink=v;
    else if (a == "--latency-ms") o.latencyMs=number(v);
    else if (a == "--clock-policy") { need(v==std::string("producer") || v==std::string("audio"),"clock-policy must be producer or audio");o.producerClock=v==std::string("producer"); }
    else if (a == "--session")
      o.session = v;
    else if (a == "--format")
      o.format = v;
    else if (a == "--fault")
      o.fault = v;
    else if (a == "--frames")
      o.frames = number(v);
    else if (a == "--width")
      o.width = number(v);
    else if (a == "--height")
      o.height = number(v);
    else if (a == "--fps")
      o.fps = number(v);
    else if (a == "--rate")
      o.rate = number(v);
    else if (a == "--channels")
      o.channels = number(v);
    else if (a == "--timeout-ms")
      o.timeout = number(v);
    else if (a == "--sessions")
      o.sessions = number(v);
    else
      throw std::runtime_error("unknown option");
  }
  need(!o.session.empty() && o.frames && o.frames <= 100000 && o.width >= 2 &&
           o.width <= 4096 && o.height >= 2 && o.height <= 2160 &&
           o.fps >= 13 && o.fps <= 240 && o.rate >= 8000 && o.rate <= 192000 &&
           (o.channels == 1 || o.channels == 2) && o.timeout >= 100 &&
           o.timeout <= 60000 && o.sessions && o.sessions <= 100,
       "option outside bounds");
  need((o.format == "BGRA" || o.format == "NV12" || o.format == "auto") &&
           (o.format != "NV12" || (!(o.width % 2) && !(o.height % 2))),
       "unsupported video format/dimensions");
  need(o.fault.empty() || o.fault == "missing-audio" ||
           o.fault == "audio-pts-reset",
       "unsupported fault");
  need(o.audioSink == "autoaudiosink" || o.audioSink == "fakesink", "unsupported audio sink");
  need(o.latencyMs <= 2000, "latency outside bounds");
  return o;
}
struct Watchdog {
  std::atomic<bool> done{false};
  std::thread t;
  Watchdog(unsigned ms)
      : t([this, ms] {
          auto end =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
          while (!done) {
            if (std::chrono::steady_clock::now() >= end) {
              fprintf(stderr, "FAIL process deadline\n");
              fflush(stderr);
              std::_Exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        }) {}
  ~Watchdog() {
    done = true;
    t.join();
  }
};
std::string randomHex(size_t bytes) {
  std::vector<unsigned char> b(bytes);
  std::string err;
  need(av::secureRandom(b.data(), b.size(), err), err);
  std::string s;
  for (auto v : b) {
    s += "0123456789abcdef"[v >> 4];
    s += "0123456789abcdef"[v & 15];
  }
  return s;
}
flSessionId randomId() {
  flSessionId id{};
  std::string e;
  need(av::secureRandom(id.bytes, sizeof(id.bytes), e), e);
  return id;
}
struct Conn {
  GSocketConnection *p = nullptr;
  Conn() = default;
  explicit Conn(GSocketConnection *q) : p(q) {}
  Conn(const Conn &) = delete;
  ~Conn() {
    if (p) {
      g_io_stream_close(G_IO_STREAM(p), nullptr, nullptr);
      g_object_unref(p);
    }
  }
};
void write(Conn &c, const void *p, size_t n) {
  gsize done = 0;
  need(
      g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(c.p)),
                                p, n, &done, nullptr, nullptr) &&
          done == n,
      "control write/disconnect");
}
void read(Conn &c, void *p, size_t n) {
  gsize done = 0;
  need(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(c.p)),
                               p, n, &done, nullptr, nullptr) &&
           done == n,
       "control read/disconnect");
}
void put(Conn &c, uint64_t n) {
  unsigned char b[8];
  GST_WRITE_UINT64_LE(b, n);
  write(c, b, 8);
}
uint64_t get(Conn &c) {
  unsigned char b[8];
  read(c, b, 8);
  return GST_READ_UINT64_LE(b);
}
flSessionCapabilities caps(const Options &o) {
  flSessionCapabilities c{};
  c.version = 1;
  c.videoCount = c.audioCount = 1;
  c.video[0] = {static_cast<uint32_t>(o.format == "NV12" ? FL_FORMAT_NV12 : FL_FORMAT_BGRA8), o.width,
                o.height, o.fps, 1};
  c.audio[0] = {FL_PCM_S16, o.rate, o.channels};
  if (o.format == "auto" && !(o.width % 2) && !(o.height % 2)) {
    c.videoCount = 2;
    c.video[1] = c.video[0];
    c.video[1].format = FL_FORMAT_NV12;
  }
  return c;
}
// Fixed bounded offer envelope: version/counts + eight video + eight audio
// entries. Integers are LE; unused entries must be zero, never native structs.
void sendCaps(Conn &c, const flSessionCapabilities &a) {
  need(a.version == 1 && a.videoCount && a.videoCount <= 8 &&
       a.audioCount && a.audioCount <= 8, "invalid outgoing offer counts/version");
  unsigned char b[268]{};
  GST_WRITE_UINT32_LE(b, a.version);
  GST_WRITE_UINT32_LE(b + 4, a.videoCount);
  GST_WRITE_UINT32_LE(b + 8, a.audioCount);
  for (unsigned i = 0; i < a.videoCount; ++i) {
    const auto &v = a.video[i];
    const uint32_t values[] = {v.format, v.width, v.height, v.fpsNum, v.fpsDen};
    for (unsigned j = 0; j < 5; ++j)
      GST_WRITE_UINT32_LE(b + 12 + i * 20 + j * 4, values[j]);
  }
  for (unsigned i = 0; i < a.audioCount; ++i) {
    const auto &audi = a.audio[i];
    const uint32_t values[] = {audi.pcm, audi.rate, audi.layout};
    for (unsigned j = 0; j < 3; ++j)
      GST_WRITE_UINT32_LE(b + 172 + i * 12 + j * 4, values[j]);
  }
  write(c, b, sizeof(b));
}
flSessionCapabilities recvCaps(Conn &c) {
  unsigned char b[268];
  read(c, b, sizeof(b));
  flSessionCapabilities a{};
  a.version = GST_READ_UINT32_LE(b);
  a.videoCount = GST_READ_UINT32_LE(b + 4);
  a.audioCount = GST_READ_UINT32_LE(b + 8);
  need(a.version == 1 && a.videoCount && a.videoCount <= 8 && a.audioCount &&
           a.audioCount <= 8,
       "invalid offer counts/version");
  for (unsigned i = 0; i < a.videoCount; ++i) {
    auto *p = b + 12 + i * 20;
    a.video[i] = {GST_READ_UINT32_LE(p), GST_READ_UINT32_LE(p + 4),
                  GST_READ_UINT32_LE(p + 8), GST_READ_UINT32_LE(p + 12),
                  GST_READ_UINT32_LE(p + 16)};
  }
  for (unsigned i = 0; i < a.audioCount; ++i) {
    auto *p = b + 172 + i * 12;
    a.audio[i] = {GST_READ_UINT32_LE(p), GST_READ_UINT32_LE(p + 4),
                  GST_READ_UINT32_LE(p + 8)};
  }
  for (unsigned i = 12 + a.videoCount * 20; i < 172; ++i)
    need(!b[i], "nonzero unused offer video");
  for (unsigned i = 172 + a.audioCount * 12; i < 268; ++i)
    need(!b[i], "nonzero unused offer audio");
  return a;
}
av::Endpoint audioEndpoint(const av::Endpoint &root,
                           const flSessionConfig &cfg) {
  auto result = root;
  auto *h = g_hmac_new(G_CHECKSUM_SHA256, root.token.data(), root.token.size());
  const char context[] = "FrameLink audio session v1";
  g_hmac_update(h, reinterpret_cast<const guchar *>(context), sizeof(context));
  g_hmac_update(h, cfg.sessionId.bytes, sizeof(cfg.sessionId.bytes));
  unsigned char generation[8];
  GST_WRITE_UINT64_LE(generation, cfg.generation);
  g_hmac_update(h, generation, sizeof(generation));
  gsize size = result.token.size();
  g_hmac_get_digest(h, result.token.data(), &size);
  g_hmac_unref(h);
  need(size == result.token.size(), "audio key derivation");
  return result;
}
void tune(Conn &c, unsigned timeout) {
  g_socket_set_timeout(g_socket_connection_get_socket(c.p),
                       std::min(2u, std::max(1u, (timeout + 999) / 1000)));
}
Conn *connect(const av::Endpoint &r, char role, unsigned timeout,
              const av::SessionProvider* provider=nullptr) {
  if (provider) {
    std::string err;
    auto* q=provider->connect(r,role,timeout,err);
    need(q,err.empty()?"broker connection failed":err);
    std::unique_ptr<Conn> c(new Conn(q));
    tune(*c,timeout);
    need(av::authenticateClient(c->p,r,role,err),err);
    return c.release();
  }
  auto *client = g_socket_client_new();
  g_socket_client_set_timeout(
      client, std::min(2u, std::max(1u, (timeout + 999) / 1000)));
  auto *q = g_socket_client_connect_to_host(client, "127.0.0.1", r.port,
                                            nullptr, nullptr);
  g_object_unref(client);
  need(q, "connect failed");
  std::unique_ptr<Conn> c(new Conn(q));
  tune(*c, timeout);
  std::string err;
  need(av::authenticateClient(c->p, r, role, err), err);
  return c.release();
}
struct Pipeline {
  GstElement *p = nullptr;
  ~Pipeline() {
    if (p) {
      gst_element_set_state(p, GST_STATE_NULL);
      gst_object_unref(p);
    }
  }
};
void parse(Pipeline &p, const std::string &s) {
  GError *e = nullptr;
  p.p = gst_parse_launch(s.c_str(), &e);
  if (e) {
    std::string msg = e->message;
    g_error_free(e);
    throw std::runtime_error(msg);
  }
  need(p.p, "pipeline construction");
  auto *bus = gst_element_get_bus(p.p);
  gst_bus_set_sync_handler(
      bus,
      [](GstBus *, GstMessage *m, gpointer) -> GstBusSyncReply {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
          GError *e = nullptr;
          gchar *d = nullptr;
          gst_message_parse_error(m, &e, &d);
          fprintf(stderr, "pipeline error: %s (%s)\n", e->message, d ? d : "");
          g_error_free(e);
          g_free(d);
        }
        return GST_BUS_PASS;
      },
      nullptr, nullptr);
  gst_object_unref(bus);
}
GstElement *named(Pipeline &p, const char *n) {
  auto *e = gst_bin_get_by_name(GST_BIN(p.p), n);
  need(e, "missing element");
  return e;
}
void setStream(Pipeline &p, const char *name, Conn &c, bool output) {
  auto *e = named(p, name);
  g_object_set(e, "stream",
               output
                   ? (gpointer)g_io_stream_get_output_stream(G_IO_STREAM(c.p))
                   : (gpointer)g_io_stream_get_input_stream(G_IO_STREAM(c.p)),
               nullptr);
  if (output) {
    auto *pad = gst_element_get_static_pad(e, "sink");
    gst_pad_add_probe(
        pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        [](GstPad *, GstPadProbeInfo *info, gpointer) -> GstPadProbeReturn {
          auto *ev = GST_PAD_PROBE_INFO_EVENT(info);
          if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT) {
            GstSegment seg;
            gst_segment_init(&seg, GST_FORMAT_TIME);
            auto *replacement = gst_event_new_segment(&seg);
            gst_event_unref(ev);
            GST_PAD_PROBE_INFO_DATA(info) = replacement;
          }
          return GST_PAD_PROBE_OK;
        },
        nullptr, nullptr);
    gst_object_unref(pad);
  }
  g_object_unref(e);
}
void start(Pipeline &p, GstClock *clock, uint64_t base) {
  gst_pipeline_use_clock(GST_PIPELINE(p.p), clock);
  gst_element_set_start_time(p.p, GST_CLOCK_TIME_NONE);
  gst_element_set_base_time(p.p, base);
  need(gst_element_set_state(p.p, GST_STATE_PLAYING) !=
           GST_STATE_CHANGE_FAILURE,
       "pipeline start");
}
std::string videoCaps(const Options &o) {
  return "video/x-raw,format=" + o.format +
         ",width=" + std::to_string(o.width) +
         ",height=" + std::to_string(o.height) +
         ",framerate=" + std::to_string(o.fps) + "/1";
}
std::string audioCaps(const Options &o) {
  return "audio/x-raw,format=S16LE,layout=interleaved,rate=" +
         std::to_string(o.rate) + ",channels=" + std::to_string(o.channels) +
         ",channel-mask=(bitmask)" + (o.channels == 2 ? "0x3" : "0x4");
}
// Opt-in development trace: hashes canonical pixel rows (not allocator padding)
// and PCM bytes. This is deliberately not a zero-overhead performance mode.
struct MediaTrace {
  std::string side, track;
  GstSegment segment{};
  GstCaps* caps=nullptr;
  bool haveSegment=false;
  ~MediaTrace(){if(caps)gst_caps_unref(caps);}
};
GstPadProbeReturn traceMedia(GstPad*, GstPadProbeInfo* info, gpointer data) {
  auto& t=*static_cast<MediaTrace*>(data);
  if(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    auto* e=GST_PAD_PROBE_INFO_EVENT(info);
    if(GST_EVENT_TYPE(e)==GST_EVENT_SEGMENT){const GstSegment* s=nullptr;gst_event_parse_segment(e,&s);t.segment=*s;t.haveSegment=true;}
    if(GST_EVENT_TYPE(e)==GST_EVENT_CAPS){GstCaps* c=nullptr;gst_event_parse_caps(e,&c);if(t.caps)gst_caps_unref(t.caps);t.caps=gst_caps_ref(c);}
    return GST_PAD_PROBE_OK;
  }
  auto* b=GST_PAD_PROBE_INFO_BUFFER(info);
  if(!b)return GST_PAD_PROBE_OK;
  const auto monotonicNs=gst_util_get_timestamp(); // opt-in probe time, before mapping/hash
  auto pts=t.haveSegment && t.segment.format==GST_FORMAT_TIME ? gst_segment_to_running_time(&t.segment,GST_FORMAT_TIME,GST_BUFFER_PTS(b)) : GST_CLOCK_TIME_NONE;
  auto* hash=g_checksum_new(G_CHECKSUM_SHA256);
  size_t bytes=0;
  if(t.track=="video") {
    GstVideoInfo vi;gst_video_info_init(&vi);GstVideoFrame f{};
    if(t.caps && gst_video_info_from_caps(&vi,t.caps) && gst_video_frame_map(&f,&vi,b,GST_MAP_READ)) {
      for(unsigned plane=0;plane<GST_VIDEO_INFO_N_PLANES(&vi);++plane) {
        unsigned height=plane ? GST_VIDEO_INFO_HEIGHT(&vi)/2 : GST_VIDEO_INFO_HEIGHT(&vi);
        unsigned width=GST_VIDEO_INFO_WIDTH(&vi)*(GST_VIDEO_INFO_FORMAT(&vi)==GST_VIDEO_FORMAT_BGRA ? 4 : 1);
        auto* base=static_cast<const guchar*>(GST_VIDEO_FRAME_PLANE_DATA(&f,plane));
        for(unsigned y=0;y<height;++y){g_checksum_update(hash,base+y*GST_VIDEO_FRAME_PLANE_STRIDE(&f,plane),width);bytes+=width;}
      }
      gst_video_frame_unmap(&f);
    }
  } else {
    GstMapInfo map{};if(gst_buffer_map(b,&map,GST_MAP_READ)){g_checksum_update(hash,map.data,map.size);bytes=map.size;gst_buffer_unmap(b,&map);}
  }
  static std::mutex output;
  {std::lock_guard<std::mutex> lock(output);
    printf("{\"event\":\"media-trace\",\"side\":\"%s\",\"track\":\"%s\",\"monotonic_ns\":%llu,\"pts_ns\":%llu,\"duration_ns\":%llu,\"bytes\":%llu,\"sha256\":\"%s\"}\n",t.side.c_str(),t.track.c_str(),(unsigned long long)monotonicNs,(unsigned long long)pts,(unsigned long long)GST_BUFFER_DURATION(b),(unsigned long long)bytes,g_checksum_get_string(hash));fflush(stdout);}
  g_checksum_free(hash);return GST_PAD_PROBE_OK;
}
void attachTrace(Pipeline& pipeline,const char* element,const char* padName,const char* side,const char* track) {
  auto* e=named(pipeline,element);auto* pad=gst_element_get_static_pad(e,padName);gst_object_unref(e);
  need(pad,"trace pad unavailable");
  auto* trace=new MediaTrace;trace->side=side;trace->track=track;
  gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),traceMedia,trace,[](gpointer p){delete static_cast<MediaTrace*>(p);});gst_object_unref(pad);
}
std::string externalPipeline(const Options& o,const std::string& channel) {
  std::ifstream input(o.inputPipeline);need(bool(input),"cannot read input pipeline file");
  std::ostringstream contents;contents<<input.rdbuf();auto text=contents.str();
  need(text.size()<=65536,"input pipeline exceeds 64 KiB");
  auto replace=[&](const char* token,const std::string& value){auto at=text.find(token);need(at!=std::string::npos && text.find(token,at+strlen(token))==std::string::npos,"pipeline requires exactly one of each sink placeholder");text.replace(at,strlen(token),value);};
  replace("@VIDEO_SINK@",videoCaps(o)+" ! identity name=externalvideo ! framelinksink sync=true preserve-timestamps=true channel="+channel);
  replace("@AUDIO_SINK@",audioCaps(o)+" ! identity name=externalaudio sync=true ! queue max-size-buffers=4 max-size-bytes=0 max-size-time=80000000 ! gdppay ! giostreamsink name=audioio sync=false async=false");
  return text;
}
void fillVideo(unsigned char *p, const GstVideoInfo &vi, unsigned n) {
  for (unsigned y = 0; y < GST_VIDEO_INFO_HEIGHT(&vi); ++y)
    for (unsigned x = 0; x < GST_VIDEO_INFO_WIDTH(&vi); ++x) {
      auto *q =
          p + GST_VIDEO_INFO_PLANE_OFFSET(&vi, 0) +
          y * GST_VIDEO_INFO_PLANE_STRIDE(&vi, 0) +
          (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_NV12 ? x : x * 4);
      if (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_NV12)
        *q = (unsigned char)(16 + (n + x + y) % 220);
      else {
        q[0] = n;
        q[1] = x;
        q[2] = y;
        q[3] = 255;
      }
    }
  if (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_NV12)
    for (unsigned y = 0; y < GST_VIDEO_INFO_HEIGHT(&vi) / 2; ++y)
      for (unsigned x = 0; x < GST_VIDEO_INFO_WIDTH(&vi); x += 2) {
        auto *q = p + GST_VIDEO_INFO_PLANE_OFFSET(&vi, 1) +
                  y * GST_VIDEO_INFO_PLANE_STRIDE(&vi, 1) + x;
        q[0] = 96 + n % 64;
        q[1] = 160 - n % 64;
      }
}
GstBuffer *makeVideo(const Options &o, unsigned n, uint64_t pts, uint64_t dur) {
  GstVideoInfo vi{};
  gst_video_info_set_format(
      &vi, o.format == "NV12" ? GST_VIDEO_FORMAT_NV12 : GST_VIDEO_FORMAT_BGRA,
      o.width, o.height);
  auto *b = gst_buffer_new_allocate(nullptr, vi.size, nullptr);
  GstMapInfo m{};
  need(gst_buffer_map(b, &m, GST_MAP_WRITE), "map video");
  memset(m.data, 0, m.size);
  fillVideo(m.data, vi, n);
  gst_buffer_unmap(b, &m);
  GST_BUFFER_PTS(b) = pts;
  GST_BUFFER_DURATION(b) = dur;
  if (!n)
    GST_BUFFER_FLAG_SET(b, GST_BUFFER_FLAG_DISCONT);
  return b;
}
int16_t tone(uint64_t index, unsigned rate) {
  return int16_t(6000 *
                 std::sin(2 * 3.141592653589793 * 440 * double(index) / rate));
}
GstBuffer *makeAudio(const Options &o, unsigned n, uint64_t pts,
                     uint64_t &dur) {
  uint64_t first = uint64_t(n) * o.rate / o.fps,
           last = uint64_t(n + 1) * o.rate / o.fps;
  int64_t a, b;
  need(flSessionSampleOffset(first, o.rate, &a) == 0 &&
           flSessionSampleOffset(last, o.rate, &b) == 0,
       "audio sample offset");
  dur = b - a;
  auto *buffer = gst_buffer_new_allocate(
      nullptr, (last - first) * o.channels * 2, nullptr);
  GstMapInfo m{};
  need(gst_buffer_map(buffer, &m, GST_MAP_WRITE), "map audio");
  for (uint64_t i = first; i < last; ++i)
    for (unsigned c = 0; c < o.channels; ++c)
      GST_WRITE_UINT16_LE(m.data + ((i - first) * o.channels + c) * 2,
                          tone(i, o.rate));
  gst_buffer_unmap(buffer, &m);
  int64_t samplePts = 0;
  need(flSessionSampleOffset(first, o.rate, &samplePts) == 0,
       "sample PTS overflow");
  GST_BUFFER_PTS(buffer) = uint64_t(samplePts)+o.audioOffsetNs;
  GST_BUFFER_DURATION(buffer) = dur;
  if (!n)
    GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
  return buffer;
}
void validateVideo(GstSample *s, const Options &o, unsigned n, uint64_t pts) {
  auto *b = gst_sample_get_buffer(s);
  need(GST_BUFFER_PTS(b) == pts &&
           gst_segment_to_running_time(gst_sample_get_segment(s),
                                       GST_FORMAT_TIME,
                                       GST_BUFFER_PTS(b)) == pts,
       "video PTS mismatch");
  if (!n)
    need(GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_DISCONT),
         "first video missing discontinuity");
  GstVideoInfo vi{};
  need(gst_video_info_from_caps(&vi, gst_sample_get_caps(s)), "video caps");
  need(unsigned(vi.width) == o.width && unsigned(vi.height) == o.height &&
           GST_VIDEO_INFO_FORMAT(&vi) == (o.format == "NV12"
                                              ? GST_VIDEO_FORMAT_NV12
                                              : GST_VIDEO_FORMAT_BGRA),
       "video format mismatch");
  GstMapInfo m{};
  need(gst_buffer_map(b, &m, GST_MAP_READ), "map received video");
  std::vector<unsigned char> expected(vi.size);
  fillVideo(expected.data(), vi, n);
  bool ok = m.size >= vi.size;
  for (unsigned plane = 0; ok && plane < (o.format == "NV12" ? 2u : 1u);
       ++plane) {
    unsigned rows = plane ? o.height / 2 : o.height,
             bytes = o.format == "NV12" ? o.width : o.width * 4;
    for (unsigned y = 0; ok && y < rows; ++y) {
      size_t off = GST_VIDEO_INFO_PLANE_OFFSET(&vi, plane) +
                   y * GST_VIDEO_INFO_PLANE_STRIDE(&vi, plane);
      ok = !memcmp(m.data + off, expected.data() + off, bytes);
    }
  }
  gst_buffer_unmap(b, &m);
  need(ok, "video pixel mismatch");
}
void validateAudio(GstSample *s, const Options &o, unsigned n, uint64_t pts) {
  auto *b = gst_sample_get_buffer(s);
  auto *structure = gst_caps_get_structure(gst_sample_get_caps(s), 0);
  gint rate = 0, channels = 0;
  const char *fmt = gst_structure_get_string(structure, "format");
  need(gst_structure_get_int(structure, "rate", &rate) && rate == int(o.rate) &&
           gst_structure_get_int(structure, "channels", &channels) &&
           channels == int(o.channels) && fmt && !strcmp(fmt, "S16LE"),
       "received audio caps mismatch");
  if (!n)
    need(GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_DISCONT),
         "first audio missing discontinuity");
  uint64_t dur = 0;
  std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)> expectedOwner(
      makeAudio(o, n, pts, dur), gst_buffer_unref);
  auto *expected = expectedOwner.get();
  need(GST_BUFFER_PTS(b) == GST_BUFFER_PTS(expected) &&
           GST_BUFFER_DURATION(b) == dur,
       "audio timeline mismatch");
  GstMapInfo a{}, e{};
  need(gst_buffer_map(b, &a, GST_MAP_READ), "received audio map");
  if (!gst_buffer_map(expected, &e, GST_MAP_READ)) {
    gst_buffer_unmap(b, &a);
    throw std::runtime_error("expected audio map");
  }
  bool ok = a.size == e.size && !memcmp(a.data, e.data, a.size);
  gst_buffer_unmap(b, &a);
  gst_buffer_unmap(expected, &e);
  need(ok, "PCM mismatch");
  need(gst_segment_to_running_time(gst_sample_get_segment(s), GST_FORMAT_TIME,
                                   GST_BUFFER_PTS(b)) == GST_BUFFER_PTS(b),
       "audio running-time mismatch");
}
void event(const char *name, const Options &o, unsigned done,
           const std::string &id) {
  printf("{\"event\":\"%s\",\"instance\":\"%s\",\"generation\":1,\"format\":\"%"
         "s\",\"video_frames\":%u,\"audio_blocks\":%u,\"audio_offset_ns\":%llu}\n",
         name, id.c_str(), o.format.c_str(), done, done,(unsigned long long)o.audioOffsetNs);
  fflush(stdout);
}
} // namespace
namespace {
std::string idHex(const flSessionId &id) {
  std::string s;
  for (auto b : id.bytes) {
    s += "0123456789abcdef"[b >> 4];
    s += "0123456789abcdef"[b & 15];
  }
  return s;
}
struct Object {
  GObject *p;
  ~Object() {
    if (p)
      g_object_unref(p);
  }
};
std::unique_ptr<Conn> accept(GSocketListener *l, const av::Endpoint &ep,
                             char wanted, unsigned timeout) {
  for (unsigned tries = 0; tries < 8; ++tries) {
    auto *q = g_socket_listener_accept(l, nullptr, nullptr, nullptr);
    need(q, "accept failed");
    std::unique_ptr<Conn> c(new Conn(q));
    tune(*c, timeout);
    std::string err;
    if (av::authenticateServer(c->p, ep, wanted, err))
      return c;
  }
  throw std::runtime_error("authentication attempt limit");
}
struct Handles {
  std::vector<GstElement *> e;
  ~Handles() {
    for (auto *p : e)
      gst_object_unref(p);
  }
  GstElement *get(Pipeline &p, const char *n) {
    auto *x = named(p, n);
    e.push_back(x);
    return x;
  }
};
// giostreamsrc waits for full requested chunks; a GDP packet tail must not
// wait for a future media block. Feed arbitrary socket chunks into stock
// gdpdepay.
struct AudioReader {
  Conn &connection;
  Pipeline &pipeline;
  GstElement *source;
  GCancellable *cancel = g_cancellable_new();
  std::thread worker;
  AudioReader(Conn &c, Pipeline &p)
      : connection(c), pipeline(p), source(named(p, "audioio")), worker([this] {
          unsigned char bytes[4096];
          for (;;) {
            auto n = g_input_stream_read(
                g_io_stream_get_input_stream(G_IO_STREAM(connection.p)), bytes,
                sizeof(bytes), cancel, nullptr);
            if (n <= 0)
              break;
            auto *b = gst_buffer_new_allocate(nullptr, n, nullptr);
            gst_buffer_fill(b, 0, bytes, n);
            if (gst_app_src_push_buffer(GST_APP_SRC(source), b) != GST_FLOW_OK)
              break;
          }
          gst_app_src_end_of_stream(GST_APP_SRC(source));
        }) {}
  ~AudioReader() {
    g_cancellable_cancel(cancel);
    g_socket_close(g_socket_connection_get_socket(connection.p), nullptr);
    gst_element_set_state(pipeline.p, GST_STATE_NULL);
    worker.join();
    g_object_unref(cancel);
    gst_object_unref(source);
  }
};
// Configure the concrete GstAudioBaseSink rather than assuming the separate
// audioresample transform performs clock slaving. With its own provided clock
// as master no slaving is necessary; external-clock fallback uses RESAMPLE.
unsigned configureAudioSlaving(GstElement* sink) {
  unsigned confirmed=0;
  auto configure=[&](GstElement* e) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e),"slave-method")) {
      g_object_set(e,"slave-method",0,nullptr); // GST_AUDIO_BASE_SINK_SLAVE_RESAMPLE
      gint method=-1;g_object_get(e,"slave-method",&method,nullptr);
      if(method==0)++confirmed;
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e),"qos"))
      g_object_set(e,"qos",TRUE,nullptr);
  };
  configure(sink);
  if (!GST_IS_BIN(sink)) return confirmed;
  auto *it=gst_bin_iterate_recurse(GST_BIN(sink));
  GValue value=G_VALUE_INIT;
  for (;;) {
    auto result=gst_iterator_next(it,&value);
    if (result==GST_ITERATOR_OK) {configure(GST_ELEMENT(g_value_get_object(&value)));g_value_reset(&value);}
    else if (result==GST_ITERATOR_RESYNC) gst_iterator_resync(it);
    else break;
  }
  if (G_IS_VALUE(&value)) g_value_unset(&value);
  gst_iterator_free(it);
  return confirmed;
}
// A failed sink can leave an appsrc producer blocked on a full queue. Stop
// from a separate thread (never the streaming bus callback) to release that
// push and let the producer accept a replacement authenticated session.
struct ProducerFailureWatch {
  GstElement* pipeline;
  GstBus* bus;
  std::atomic<bool> done{false};
  std::thread worker;
  explicit ProducerFailureWatch(Pipeline& p):pipeline(p.p),bus(gst_element_get_bus(p.p)),worker([this]{
    while (!done.load()) {
      auto* message=gst_bus_timed_pop_filtered(bus,20*GST_MSECOND,GST_MESSAGE_ERROR);
      if(message) {gst_message_unref(message);gst_element_set_state(pipeline,GST_STATE_NULL);break;}
    }
  }) {}
  ~ProducerFailureWatch(){done=true;worker.join();gst_object_unref(bus);}
};
void checkPlaybackBus(GstBus* bus) {
  auto* message=gst_bus_pop_filtered(bus,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_CLOCK_LOST));
  if(!message) return;
  std::string error="audio clock lost: renegotiate both tracks";
  if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR) {
    GError* detail=nullptr;gchar* debug=nullptr;
    gst_message_parse_error(message,&detail,&debug);
    error=std::string("playback pipeline failure: ")+(detail?detail->message:"unknown");
    if(detail) g_error_free(detail);g_free(debug);
  }
  gst_message_unref(message);
  throw std::runtime_error(error);
}
// Mode 2 primes one PCM block, then gates both producer branches until the
// consumer has opened its device and installed the shared playback epoch.
// No video is discarded and no queue is enlarged to absorb device startup.
struct StartupGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool released=false;
  bool firstAudio=true;
  bool expired=false;
  unsigned timeout;
  GstElement* pipeline=nullptr;
  std::vector<std::pair<GstPad*,gulong>> probes;
  explicit StartupGate(unsigned ms):timeout(ms){}
  void release() {std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
  ~StartupGate() {
    release();
    if(pipeline)gst_element_set_state(pipeline,GST_STATE_NULL);
    for(auto& p:probes){gst_pad_remove_probe(p.first,p.second);gst_object_unref(p.first);}
  }
  void attach(Pipeline& pipeline,const char* element,const char* padName,bool audio) {
    this->pipeline=pipeline.p;
    auto* e=named(pipeline,element);auto* pad=gst_element_get_static_pad(e,padName);gst_object_unref(e);
    struct Context {StartupGate* gate;bool audio;};
    auto* context=new Context{this,audio};
    auto id=gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*,GstPadProbeInfo*,gpointer data){
      auto& c=*static_cast<Context*>(data);auto& g=*c.gate;
      std::unique_lock<std::mutex> lock(g.mutex);
      if(c.audio && g.firstAudio){g.firstAudio=false;return GST_PAD_PROBE_OK;}
      if(!g.changed.wait_for(lock,std::chrono::milliseconds(g.timeout),[&]{return g.released;})) {
        g.expired=true;return GST_PAD_PROBE_DROP;
      }
      return GST_PAD_PROBE_OK;
    },context,[](gpointer p){delete static_cast<Context*>(p);});
    probes.emplace_back(pad,id);
  }
};
using av::AudioTimeline;
using av::trackAudio;
unsigned runProducer(const Options &o, GSocketListener *l,
                     const av::Endpoint &ep) {
  auto control = accept(l, ep, 'C', o.timeout);
  auto local = caps(o);
  auto remote = recvCaps(*control);
  sendCaps(*control, local);
  flSessionConfig cfg{};
  cfg.version = 1;
  cfg.sessionId = randomId();
  cfg.clockId = randomId();
  cfg.generation = 1;
  cfg.videoTrackId = 1;
  cfg.audioTrackId = 2;
  cfg.videoQueueFrames = 4;
  cfg.audioQueueMs = 80;
  cfg.connectTimeoutMs = cfg.idleTimeoutMs = o.timeout;
  need(flSessionNegotiate(&local, &remote, &cfg.video, &cfg.audio) == 0,
       "incompatible capabilities");
  auto *clock = gst_system_clock_obtain();
  Object clockOwner{G_OBJECT(clock)};
  need(get(*control) == 0x434c4f434bULL, "clock request");
  auto anchor = gst_clock_get_time(clock);
  put(*control, anchor);
  cfg.epochNs = anchor + 500 * GST_MSECOND;
  unsigned char wire[FL_SESSION_WIRE_SIZE];
  need(flSessionEncode(&cfg, wire, sizeof(wire)) == 0, "encode configuration");
  write(*control, wire, sizeof(wire));
  unsigned char reply[FL_SESSION_WIRE_SIZE];
  read(*control, reply, sizeof(reply));
  need(!memcmp(wire, reply, sizeof(wire)),
       "configuration acknowledgement mismatch");
  const auto mode = get(*control);
  need(mode<=2, "unsupported playback mode");
  const bool streaming=mode!=0;
  const bool coordinated=mode==2;
  auto audio = accept(l, audioEndpoint(ep, cfg), 'A', o.timeout);
  need(get(*control) == 0x5245414459ULL, "consumer not ready");
  std::string id = idHex(cfg.sessionId), channel = "av3-" + id;
  Pipeline tx;
  parse(
      tx,
      o.inputPipeline.empty() ? "appsrc name=video is-live=true format=time block=true max-buffers=2 "
      "caps=" +
          videoCaps(o) +
          " ! framelinksink preserve-timestamps=true channel=" + channel +
          (o.pacingPpm ? " sync=false" : "") +
          " appsrc name=audio is-live=true format=time block=true "
          "max-buffers=2 caps=" +
          audioCaps(o) +
          " ! queue max-size-buffers=4 max-size-bytes=0 max-size-time=80000000 "
          "! gdppay ! giostreamsink name=audioio sync=false async=false" : externalPipeline(o,channel));
  setStream(tx, "audioio", *audio, true);
  StartupGate startup(o.timeout);
  if(coordinated) {
    startup.attach(tx,o.inputPipeline.empty()?"video":"externalvideo","src",false);
    startup.attach(tx,o.inputPipeline.empty()?"audio":"externalaudio",o.inputPipeline.empty()?"src":"sink",true);
  }
  auto completeStartup=[&] {
    if(!coordinated)return;
    need(get(*control)==0x5052494d4544ULL,"consumer did not preroll");
    // Both branches use one new origin; media PTS and intentional A/V offsets
    // remain untouched. The initial config epoch was provisional in mode 2.
    cfg.epochNs=gst_clock_get_time(clock)+50*GST_MSECOND;
    gst_element_set_base_time(tx.p,cfg.epochNs);
    // PLAYING -> PLAYING does not redistribute a bin base time. Branches are
    // gated, so install the new origin on every child before releasing them.
    auto* elements=gst_bin_iterate_recurse(GST_BIN(tx.p));GValue value=G_VALUE_INIT;
    for(;;) {
      auto result=gst_iterator_next(elements,&value);
      if(result==GST_ITERATOR_OK){gst_element_set_base_time(GST_ELEMENT(g_value_get_object(&value)),cfg.epochNs);g_value_reset(&value);}
      else if(result==GST_ITERATOR_RESYNC)gst_iterator_resync(elements);else break;
    }
    if(G_IS_VALUE(&value))g_value_unset(&value);gst_iterator_free(elements);
    put(*control,uint64_t(cfg.epochNs));
    need(get(*control)==0x474fULL,"consumer did not install playback clock");
    {std::lock_guard<std::mutex> lock(startup.mutex);need(!startup.expired,"startup gate deadline");}
    startup.release();
    printf("{\"event\":\"startup-released\",\"epoch_ns\":%llu}\n",(unsigned long long)cfg.epochNs);
  };
  if(!o.inputPipeline.empty()) {
    need(streaming,"external pipeline requires consumer --stream");
    need(o.fault.empty() && !o.pacingPpm && !o.audioOffsetNs,"synthetic fault/pacing/offset options cannot be combined with external input");
    AudioTimeline sourceAudio;sourceAudio.pipeline=tx.p;sourceAudio.rate=o.rate;sourceAudio.channels=o.channels;
    // Release blocked producer pads before sourceAudio stops its pipeline on
    // exceptional exit; teardown must not wait for the startup gate timeout.
    struct GateRelease {StartupGate& gate;~GateRelease(){gate.release();}} releaseOnExit{startup};
    auto* ae=named(tx,"externalaudio");auto* ap=gst_element_get_static_pad(ae,"src");gst_object_unref(ae);
    gst_pad_add_probe(ap,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),trackAudio,&sourceAudio,nullptr);gst_object_unref(ap);
    if(o.traceMedia){attachTrace(tx,"externalvideo","src","producer","video");attachTrace(tx,"externalaudio","src","producer","audio");}
    put(*control,o.frames);
    event("session-ready",o,0,id);
    start(tx,clock,cfg.epochNs);
    completeStartup();
    auto* bus=gst_element_get_bus(tx.p);Object busOwner{G_OBJECT(bus)};
    GstMessage* message=nullptr;
    const auto eosDeadline=g_get_monotonic_time()+gint64(o.timeout)*1000;
    while (!message) {
      const auto remaining=eosDeadline-g_get_monotonic_time();
      need(remaining>0,"external pipeline EOS deadline");
      auto* next=gst_bus_timed_pop_filtered(bus,uint64_t(remaining)*1000,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR|GST_MESSAGE_ELEMENT));
      need(next,"external pipeline EOS deadline");
      if (GST_MESSAGE_TYPE(next)==GST_MESSAGE_ELEMENT) {
        const auto* stats=gst_message_get_structure(next);
        if (stats && gst_structure_has_name(stats,"framelink-sink-stats")) {
          guint64 received=0,submitted=0,dropped=0,full=0;
          gst_structure_get_uint64(stats,"received",&received);gst_structure_get_uint64(stats,"submitted",&submitted);
          gst_structure_get_uint64(stats,"dropped",&dropped);gst_structure_get_uint64(stats,"ring-full-drops",&full);
          printf("{\"event\":\"video-transport-summary\",\"received\":%llu,\"submitted\":%llu,\"dropped\":%llu,\"ring_full_drops\":%llu}\n",(unsigned long long)received,(unsigned long long)submitted,(unsigned long long)dropped,(unsigned long long)full);
        }
        gst_message_unref(next);
      } else message=next;
    }
    const bool success=GST_MESSAGE_TYPE(message)==GST_MESSAGE_EOS;gst_message_unref(message);
    need(success,"external pipeline failed");
    need(!sourceAudio.invalid.load() && sourceAudio.samplesEnd.load()==uint64_t(o.frames)*o.rate/o.fps,"external audio must be contiguous and match declared duration");
    need(get(*control)==0x444f4e45ULL,"consumer completion missing");
    event("session-complete",o,o.frames,id);return o.frames;
  }
  start(tx, clock, cfg.epochNs);
  ProducerFailureWatch failureWatch(tx);
  Handles h;
  auto *v = h.get(tx, "video"), *a = h.get(tx, "audio");
  put(*control, o.frames);
  event("session-ready", o, 0, id);
  if(coordinated) {
    uint64_t duration;
    need(gst_app_src_push_buffer(GST_APP_SRC(a),makeAudio(o,0,0,duration))==GST_FLOW_OK,"audio prime failed");
    completeStartup();
  }
  uint64_t maxPushNs=0;
  for (unsigned n = 0; n < o.frames; ++n) {
    uint64_t pts = uint64_t(n) * GST_SECOND / o.fps,
             next = uint64_t(n + 1) * GST_SECOND / o.fps;
    auto now = gst_clock_get_time(clock);
    // Only wall-clock pacing changes. PCM sample positions, A/V PTS and
    // durations remain exact on the negotiated media timeline.
    const auto paced=gst_util_uint64_scale(pts,1000000,1000000+o.pacingPpm);
    if (uint64_t(cfg.epochNs) + paced > now)
      g_usleep((uint64_t(cfg.epochNs) + paced - now) / 1000);
    const auto pushStart=gst_util_get_timestamp();
    need(gst_app_src_push_buffer(
             GST_APP_SRC(v), makeVideo(o, n, pts, next - pts)) == GST_FLOW_OK,
         "video send failed");
    if (!(coordinated && n==0) && !(o.fault == "missing-audio" && n == 10)) {
      uint64_t dur;
      auto *b = makeAudio(o, n, pts, dur);
      if (o.fault == "audio-pts-reset" && n == 10)
        GST_BUFFER_PTS(b) = 0;
      need(gst_app_src_push_buffer(GST_APP_SRC(a), b) == GST_FLOW_OK,
           "audio send failed");
    }
    maxPushNs=std::max(maxPushNs,gst_util_get_timestamp()-pushStart);
    if (!streaming) need(get(*control) == n + 1, "frame acknowledgement mismatch");
    if (n == 0 || (n + 1) % 25 == 0)
      event("media-progress", o, n + 1, id);
  }
  printf("{\"event\":\"producer-pacing\",\"pacing_ppm\":%d,\"max_push_ns\":%llu}\n",o.pacingPpm,(unsigned long long)maxPushNs);
  gst_app_src_end_of_stream(GST_APP_SRC(a));
  gst_app_src_end_of_stream(GST_APP_SRC(v));
  need(get(*control) == 0x444f4e45ULL, "consumer completion missing");
  event("session-complete", o, o.frames, id);
  return o.frames;
}
unsigned runConsumer(const Options &requested, const av::Endpoint &ep,
                     const av::PlaybackCallbacks* cb=nullptr, const std::atomic<bool>* stop=nullptr, bool externalVideo=false, const std::atomic<bool>* mute=nullptr, const std::atomic<bool>* paused=nullptr, const av::SessionProvider* provider=nullptr) {
  Options o = requested;
  std::unique_ptr<Conn> control(connect(ep, 'C', o.timeout, provider));
  auto local = caps(o);
  sendCaps(*control, local);
  auto remote = recvCaps(*control);
  auto *clock = gst_system_clock_obtain();
  Object clockOwner{G_OBJECT(clock)};
  auto t0 = gst_clock_get_time(clock);
  put(*control, 0x434c4f434bULL);
  auto source = get(*control);
  auto t1 = gst_clock_get_time(clock);
  need(t1 >= t0 && t1 - t0 <= 100 * GST_MSECOND,
       "clock uncertainty exceeds 100 ms budget");
  unsigned char wire[FL_SESSION_WIRE_SIZE];
  read(*control, wire, sizeof(wire));
  flSessionConfig cfg{};
  need(flSessionDecode(wire, sizeof(wire), &cfg) == 0 &&
           flSessionValidateSelection(&cfg, &remote, &local) == 0,
       "invalid/unoffered configuration");
  o.format = cfg.video.format == FL_FORMAT_NV12 ? "NV12" : "BGRA";
  flClockMapping mapping{cfg.clockId, randomId(), int64_t(source),
                         int64_t(t0 + (t1 - t0) / 2), (t1 - t0 + 1) / 2};
  int64_t base = 0;
  need(flSessionMapPts(&cfg, &mapping, 50 * GST_MSECOND, 0, &base) == 0,
       "clock mapping failed");
  write(*control, wire, sizeof(wire));
  put(*control, o.stream ? (o.producerClock ? 2 : 1) : 0);
  std::unique_ptr<Conn> audio(connect(audioEndpoint(ep, cfg), 'A', o.timeout, provider));
  std::string id = idHex(cfg.sessionId), channel = "av3-" + id;
  if (o.stream) {
    Pipeline rx;
    std::string video = externalVideo ? "" : "framelinksrc channel="+channel+
      " width="+std::to_string(o.width)+" height="+std::to_string(o.height)+
      " fps="+std::to_string(o.fps)+" format="+o.format+
      " preserve-timestamps=true ! appsink name=vout sync=false max-buffers=3 drop=true ";
    parse(rx, video+"appsrc name=audioio is-live=false format=bytes block=true max-buffers=4 ! gdpdepay ! "+
      audioCaps(o)+" ! identity name=timeline ! queue name=audioqueue max-size-buffers=4 max-size-bytes=0 max-size-time=80000000 ! audioconvert ! audioresample ! volume name=playvolume ! "+o.audioSink+" name=playaudio sync=true"+(o.audioSink=="fakesink" ? " async=false" : ""));
    if(o.traceMedia){attachTrace(rx,"timeline","src","consumer","audio");if(!externalVideo)attachTrace(rx,"vout","sink","consumer","video");}
    AudioTimeline audioTimeline; audioTimeline.pipeline=rx.p; audioTimeline.rate=o.rate; audioTimeline.channels=o.channels;
    auto *identity=named(rx,"timeline");
    auto *audioPad=gst_element_get_static_pad(identity,"src");
    gst_pad_add_probe(audioPad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),trackAudio,&audioTimeline,nullptr);
    gst_object_unref(audioPad);gst_object_unref(identity);
    need(gst_element_set_state(rx.p, GST_STATE_PAUSED)!=GST_STATE_CHANGE_FAILURE,"audio open failed");
    Handles h;
    auto *v=externalVideo ? nullptr : h.get(rx,"vout");
    auto *volume=h.get(rx,"playvolume");
    auto *queue=h.get(rx,"audioqueue");
    g_signal_connect(queue,"underrun",G_CALLBACK(+[](GstElement*,gpointer p){++static_cast<AudioTimeline*>(p)->emptyEvents;}),&audioTimeline);
    g_signal_connect(queue,"overrun",G_CALLBACK(+[](GstElement*,gpointer p){++static_cast<AudioTimeline*>(p)->fullEvents;}),&audioTimeline);
    auto *sink=h.get(rx,"playaudio");
    auto *bus=gst_element_get_bus(rx.p);
    Object busOwner{G_OBJECT(bus)};
    auto latency=uint64_t(o.latencyMs)*GST_MSECOND;
    if (cb && cb->onSession) cb->onSession(cfg,channel,av::PlaybackClock{});
    AudioReader reader(*audio,rx);
    // Invalidate native video immediately on exit, before a device/socket's
    // potentially slow teardown. Callback must retain clocks it needs itself.
    struct EndNotification {
      const av::PlaybackCallbacks* cb;
      ~EndNotification(){if(cb && cb->onEnd)cb->onEnd();}
    } endNotification{cb};
    put(*control,0x5245414459ULL);
    const auto frames=get(*control);
    need(frames && frames<=100000,"invalid stream length");
    auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(o.timeout);
    // Live video may prevent whole-pipeline preroll. Await the audio sink only;
    // its first real PCM packet negotiates the device and exposes its clock.
    while (!audioTimeline.blocks.load()) {
      need(!stop || !stop->load(),"playback cancelled");
      need(!paused || !paused->load(),"session-paused");
      need(std::chrono::steady_clock::now()<end,"audio preroll deadline");
      g_usleep(1000);
    }
    // Fault injection exercises device setup after the first PCM arrives.
    if(const char* delay=g_getenv("FRAMELINK_TEST_PREROLL_DELAY_MS")) {
      const auto ms=number(delay);need(ms<=o.timeout,"invalid preroll delay");
      for(unsigned n=0;n<ms;++n) {
        need(!stop || !stop->load(),"playback cancelled");
        need(std::chrono::steady_clock::now()<end,"audio preroll deadline");
        g_usleep(1000);
      }
    }
    for(;;) {
      auto state=gst_element_get_state(sink,nullptr,nullptr,10*GST_MSECOND);
      need(state!=GST_STATE_CHANGE_FAILURE,"audio preroll failed");
      if(state!=GST_STATE_CHANGE_ASYNC)break;
      need(!stop || !stop->load(),"playback cancelled");
      need(!paused || !paused->load(),"session-paused");
      need(std::chrono::steady_clock::now()<end,"audio preroll deadline");
    }
    if(o.producerClock) {
      put(*control,0x5052494d4544ULL);
      const auto finalEpoch=get(*control);
      need(finalEpoch<=uint64_t(INT64_MAX),"invalid startup epoch");
      cfg.epochNs=int64_t(finalEpoch);
      need(flSessionMapPts(&cfg,&mapping,50*GST_MSECOND,0,&base)==0,"startup clock mapping failed");
    }
    const auto resampleSlaves=configureAudioSlaving(sink);
    auto* deviceClock=gst_element_provide_clock(sink);
    Object deviceClockOwner{deviceClock?G_OBJECT(deviceClock):nullptr};
    auto *selected=(!o.producerClock && deviceClock) ? GST_CLOCK(gst_object_ref(deviceClock)) : nullptr;
    bool audioMaster=selected!=nullptr;
    if (!selected) selected=GST_CLOCK(gst_object_ref(clock));
    Object selectedOwner{G_OBJECT(selected)};
    // ProducerMapped preserves the negotiated epoch in the receiver system clock.
    // Slave the audio device to that shared system timeline (RESAMPLE above).
    // Device clock zero during PAUSED preroll is normal, not a fault diagnosis.
    // Legacy AudioPreferred keeps the joint post-preroll re-anchor. Neither mode rewrites media PTS.
    auto mappedBase=o.producerClock ? uint64_t(base) : gst_clock_get_time(selected)+20*GST_MSECOND;
    auto *latencyQuery=gst_query_new_latency();
    if (gst_element_query(rx.p,latencyQuery)) {
      gboolean live=false; GstClockTime minimum=0, maximum=GST_CLOCK_TIME_NONE;
      gst_query_parse_latency(latencyQuery,&live,&minimum,&maximum);
      if (GST_CLOCK_TIME_IS_VALID(minimum)) latency=std::max(latency,uint64_t(minimum));
    }
    gst_query_unref(latencyQuery);
    gst_pipeline_set_latency(GST_PIPELINE(rx.p),latency);
    printf("{\"event\":\"playback-clock\",\"policy\":\"%s\",\"system_now_ns\":%llu,\"selected_now_ns\":%llu,\"device_now_ns\":%llu,\"mapped_producer_base_ns\":%lld,\"playback_base_ns\":%llu,\"latency_ns\":%llu,\"resample_slaves\":%u}\n",o.producerClock?"producer":"audio",(unsigned long long)gst_clock_get_time(clock),(unsigned long long)gst_clock_get_time(selected),(unsigned long long)(deviceClock?gst_clock_get_time(deviceClock):GST_CLOCK_TIME_NONE),(long long)base,(unsigned long long)mappedBase,(unsigned long long)latency,resampleSlaves);
    const bool traceClock=g_getenv("FRAMELINK_PLAYBACK_CLOCK_TRACE")!=nullptr;
    av::PlaybackClock timing{selected,mappedBase,latency,audioMaster};
    if (cb && cb->onStatus) cb->onStatus(audioMaster ? "audio-clock" : (o.producerClock ? "producer-system-clock" : "system-clock-fallback"));
    if (cb && cb->onClock) cb->onClock(timing);
    audioTimeline.playbackClock=GST_CLOCK(gst_object_ref(selected));
    // Preroll is not clock drift; start arrival-slope diagnostics at the next
    // post-gate PCM block while retaining the original media continuity state.
    if(o.producerClock)audioTimeline.resetArrival=true;
    audioTimeline.deliveryBase=mappedBase+latency;
    start(rx,selected,mappedBase);
    if(o.producerClock)put(*control,0x474fULL);
    av::PlaybackMetrics metrics;
    uint64_t lastMetricsAt=0;
    auto reportMetrics=[&](bool final=false) {
      const auto now=gst_util_get_timestamp();
      if(!final && now-lastMetricsAt<100*GST_MSECOND) return;
      guint level=0;guint64 time=0;
      g_object_get(queue,"current-level-buffers",&level,"current-level-time",&time,nullptr);
      metrics.queueBuffers=level;metrics.queueTimeNs=time;
      metrics.peakQueueBuffers=std::max(metrics.peakQueueBuffers,unsigned(level));
      metrics.peakQueueTimeNs=std::max(metrics.peakQueueTimeNs,uint64_t(time));
      metrics.audioBlocks=audioTimeline.blocks.load();metrics.audioSamples=audioTimeline.samplesEnd.load();
      metrics.queueEmptyEvents=audioTimeline.emptyEvents.load();metrics.queueFullEvents=audioTimeline.fullEvents.load();
      metrics.lateAudioBlocks=audioTimeline.lateBlocks.load();metrics.arrivalDriftNs=audioTimeline.arrivalDrift.load();
      if(cb && cb->onMetrics)cb->onMetrics(metrics);
      lastMetricsAt=now;
      if(traceClock) printf("{\"event\":\"playback-clock-sample\",\"system_ns\":%llu,\"selected_ns\":%llu,\"device_ns\":%llu,\"audio_samples\":%llu}\n",(unsigned long long)gst_clock_get_time(clock),(unsigned long long)gst_clock_get_time(selected),(unsigned long long)(deviceClock?gst_clock_get_time(deviceClock):GST_CLOCK_TIME_NONE),(unsigned long long)metrics.audioSamples);
      if(final) printf("{\"event\":\"playback-metrics\",\"peak_queue_buffers\":%u,\"peak_queue_ns\":%llu,\"queue_empty_events\":%llu,\"queue_full_events\":%llu,\"late_audio_blocks\":%llu,\"arrival_drift_ns\":%lld}\n",metrics.peakQueueBuffers,(unsigned long long)metrics.peakQueueTimeNs,(unsigned long long)metrics.queueEmptyEvents,(unsigned long long)metrics.queueFullEvents,(unsigned long long)metrics.lateAudioBlocks,(long long)metrics.arrivalDriftNs);
    };
    uint64_t received=0,dropped=0,lastPts=GST_CLOCK_TIME_NONE,firstPts=GST_CLOCK_TIME_NONE;
    const uint64_t finalPts=(frames-1)*GST_SECOND/o.fps;

    // An audio sink pad EOS probe reports only arrival, not audible completion;
    // drain until final media deadline on the selected shared clock instead.
    const auto audioOrigin=audioTimeline.originNs.load();
    const auto duration=frames*GST_SECOND/o.fps;
    int64_t audioDuration=0;
    need(flSessionSampleOffset(frames*o.rate/o.fps,o.rate,&audioDuration)==0,"audio duration overflow");
    need(audioOrigin<=GST_CLOCK_TIME_NONE-1-duration-mappedBase-latency,"audio timeline overflow");
    const auto finishAt=mappedBase+std::max(duration,audioOrigin+uint64_t(audioDuration))+latency;
    while (gst_clock_get_time(selected)<finishAt || (!externalVideo && (!GST_CLOCK_TIME_IS_VALID(lastPts) || lastPts<finalPts))) {
      need(!stop || !stop->load(),"playback cancelled");
      need(!paused || !paused->load(),"session-paused");
      g_object_set(volume,"mute",mute && mute->load(),nullptr);
      need(!audioTimeline.invalid.load(),"audio discontinuity: renegotiate both tracks");
      need(std::chrono::steady_clock::now()<end,"playback deadline");
      reportMetrics();
      if(audioTimeline.exhausted.load()) {
        reportMetrics(true);
        throw std::runtime_error("audio playout budget exhausted: renegotiate both tracks");
      }
      checkPlaybackBus(bus);
      if (!v) { g_usleep(5000); continue; }
      auto *sample=gst_app_sink_try_pull_sample(GST_APP_SINK(v),10*GST_MSECOND);
      if (!sample) continue;
      std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sampleOwner(sample,gst_sample_unref);
      auto *b=gst_sample_get_buffer(sample);
      auto pts=gst_segment_to_running_time(gst_sample_get_segment(sample),GST_FORMAT_TIME,GST_BUFFER_PTS(b));
      if (!GST_CLOCK_TIME_IS_VALID(pts) || pts>finalPts) throw std::runtime_error("invalid video running PTS");
      if(!GST_CLOCK_TIME_IS_VALID(firstPts))firstPts=pts;
      lastPts=pts; ++received;
      if (cb && cb->onVideo) cb->onVideo(sample,pts,timing);
      else {
        auto due=mappedBase+pts+latency, now=gst_clock_get_time(selected);
        if (now>due+40*GST_MSECOND) ++dropped;
        else while (due>gst_clock_get_time(selected)) {
          need(!stop || !stop->load(),"playback cancelled");
          need(!paused || !paused->load(),"session-paused");
          need(std::chrono::steady_clock::now()<end,"playback deadline");
          g_usleep(1000);
        }
      }
    }
    reportMetrics(true);
    need(!audioTimeline.exhausted.load(),"audio playout budget exhausted: renegotiate both tracks");
    need(!audioTimeline.invalid.load() && audioTimeline.samplesEnd.load()==frames*o.rate/o.fps,"incomplete/discontinuous audio");
    gst_element_set_state(rx.p,GST_STATE_NULL);
    put(*control,0x444f4e45ULL);
    printf("{\"event\":\"playback-complete\",\"audio_master\":%s,\"video_received\":%llu,\"video_late\":%llu,\"audio_blocks\":%llu,\"audio_samples\":%llu,\"latency_ns\":%llu}\n", audioMaster?"true":"false",(unsigned long long)received,(unsigned long long)dropped,(unsigned long long)audioTimeline.blocks.load(),(unsigned long long)audioTimeline.samplesEnd.load(),(unsigned long long)latency);
    printf("{\"event\":\"playback-timing\",\"audio_first_running_pts_ns\":%llu,\"video_first_running_pts_ns\":%llu,\"audio_end_ns\":%llu,\"video_end_ns\":%llu}\n",(unsigned long long)audioOrigin,(unsigned long long)firstPts,(unsigned long long)(audioOrigin+uint64_t(audioDuration)),(unsigned long long)duration);
    return unsigned(frames);
  }
  std::string vo = "appsink name=vout sync=true max-buffers=2",
              ao = "appsink name=aout sync=true max-buffers=2";
  if (o.play) {
    vo = "tee name=vt vt. ! queue max-size-buffers=2 ! videoconvert ! "
         "autovideosink sync=true vt. ! queue max-size-buffers=2 ! " +
         vo;
    ao = "tee name=at at. ! queue max-size-buffers=4 ! audioconvert ! "
         "audioresample ! autoaudiosink sync=true at. ! queue "
         "max-size-buffers=4 ! " +
         ao;
  }
  Pipeline rx;
  parse(rx, "framelinksrc channel=" + channel +
                " width=" + std::to_string(o.width) + " height=" +
                std::to_string(o.height) + " fps=" + std::to_string(o.fps) +
                " format=" + o.format + " preserve-timestamps=true ! " + vo +
                " appsrc name=audioio is-live=true format=bytes block=true "
                "max-buffers=4 ! gdpdepay ! " +
                audioCaps(o) + " ! " + ao);
  start(rx, clock, base);
  AudioReader reader(*audio, rx);
  Handles h;
  auto *v = h.get(rx, "vout"), *a = h.get(rx, "aout");
  put(*control, 0x5245414459ULL);
  need(get(*control) == o.frames, "frame count disagreement");
  event("session-ready", o, 0, id);
  for (unsigned n = 0; n < o.frames; ++n) {
    auto *sv = gst_app_sink_try_pull_sample(
        GST_APP_SINK(v), guint64(std::min(o.timeout, 2000u)) * GST_MSECOND);
    need(sv, "video timeout/EOS");
    auto *sa = gst_app_sink_try_pull_sample(
        GST_APP_SINK(a), guint64(std::min(o.timeout, 2000u)) * GST_MSECOND);
    if (!sa) {
      gst_sample_unref(sv);
      throw std::runtime_error("audio timeout/EOS");
    }
    try {
      auto pts = uint64_t(n) * GST_SECOND / o.fps;
      validateVideo(sv, o, n, pts);
      validateAudio(sa, o, n, pts);
    } catch (...) {
      gst_sample_unref(sv);
      gst_sample_unref(sa);
      throw;
    }
    gst_sample_unref(sv);
    gst_sample_unref(sa);
    put(*control, n + 1);
    if (n == 0 || (n + 1) % 25 == 0)
      event("media-progress", o, n + 1, id);
  }
  put(*control, 0x444f4e45ULL);
  event("session-complete", o, o.frames, id);
  return o.frames;
}
} // namespace
#ifndef AV_PLAYBACK_LIBRARY
int main(int argc, char **argv) {
  try {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
      puts("Usage: framelink_av_{producer,consumer} PLUGIN --session PRIVATE_DIR\n"
           "  --frames N --format BGRA|NV12|auto --width W --height H --fps N\n"
           "  --rate N --channels 1|2 --timeout-ms N --sessions N\n"
           "  Consumer: --stream --audio-sink autoaudiosink|fakesink --latency-ms N (scheduled streaming); --play (verification window/sound); producer: --fault missing-audio|audio-pts-reset\n"
           "  --audio-offset-ns 0..2000000000 (producer fixture; verifier must use same offset)\n"
           "  --pacing-ppm -200000..200000 (producer pacing injection; PTS unchanged)\n"
           "  --input-pipeline FILE (producer; @VIDEO_SINK@ and @AUDIO_SINK@; requires streaming consumer)\n"
           "  --clock-policy audio|producer (stream receiver; default audio)\n"
           "  --trace-media (expensive payload hashes and running PTS for integration validation)\n"
           "  auto is consumer-only. See docs/M3_AV_APPS.md for bounds and recovery semantics.");
      return 0;
    }
    gst_init(&argc, &argv);
    auto o = options(argc, argv);
    Watchdog watchdog(o.timeout);
    GError *error = nullptr;
    auto *plugin = loadFrameLinkPlugin(o.plugin, &error);
    if (error) {
      fprintf(stderr, "plugin: %s\n", error->message);
      g_error_free(error);
    }
    need(plugin, "FrameLink plugin load");
    gst_object_unref(plugin);
    unsigned completed = 0, total = 0, disconnects = 0;
#ifdef AV_PRODUCER
    const char *role = "producer";
    need(!o.play, "--play is consumer-only");
    need(o.format != "auto", "producer requires concrete --format");
    auto *l = g_socket_listener_new();
    Object listener{G_OBJECT(l)};
    auto *addr = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    auto *socketAddress = g_inet_socket_address_new(addr, 0);
    g_object_unref(addr);
    GSocketAddress *effective = nullptr;
    need(g_socket_listener_add_address(l, socketAddress, G_SOCKET_TYPE_STREAM,
                                       G_SOCKET_PROTOCOL_TCP, nullptr,
                                       &effective, nullptr),
         "loopback bind");
    g_object_unref(socketAddress);
    av::Endpoint ep{};
    ep.port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));
    g_object_unref(effective);
    std::string err;
    need(av::secureRandom(ep.token.data(), ep.token.size(), err), err);
    av::RendezvousOwner owner;
    need(owner.publish(o.session, ep, err), err);
    for (unsigned attempt = 0;
         completed < o.sessions && attempt < o.sessions + 3; ++attempt) {
      try {
        total += runProducer(o, l, ep);
        ++completed;
      } catch (const std::exception &e) {
        if (o.sessions == 1)
          throw;
        ++disconnects;
        fprintf(stderr, "session disconnected: %s\n", e.what());
      }
    }
#else
    const char *role = "consumer";
    need(o.inputPipeline.empty(),"--input-pipeline is producer-only");
    need(o.fault.empty(), "fault injection is producer-only");
    for (unsigned attempt = 0;
         completed < o.sessions && attempt < o.sessions + 3; ++attempt) {
      av::Endpoint ep{};
      std::string err;
      for (;;) {
        if (av::readEndpoint(o.session, ep, err))
          break;
        g_usleep(10000);
      }
      try {
        total += runConsumer(o, ep);
        ++completed;
      } catch (const std::exception &e) {
        if (o.sessions == 1)
          throw;
        // A stale protected record can outlive a crashed owner. Connection
        // retries are deadline-bounded; do not spend media-session attempts
        // before a replacement owner has published its new endpoint.
        if (!strcmp(e.what(), "connect failed")) {
          --attempt;
          g_usleep(100000);
          continue;
        }
        ++disconnects;
        g_usleep(100000);
        fprintf(stderr, "session disconnected: %s\n", e.what());
      }
    }
#endif
    need(completed == o.sessions, "session retry limit");
    if (o.stream) { printf("{\"role\":\"%s\",\"ok\":true,\"mode\":\"streaming\",\"sessions\":%u}\n",role,completed); return 0; }
    printf("{\"role\":\"%s\",\"ok\":true,\"sessions\":%u,\"video_frames\":%u,"
           "\"audio_blocks\":%u,\"errors\":0,\"disconnects\":%u}\n",
           role, completed, total, total, disconnects);
    fflush(stdout);
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}

#endif
namespace av {
bool runPlayback(const PlaybackOptions& p,const PlaybackCallbacks& cb,const std::atomic<bool>& stop,std::string& error) {
  bool sessionOpen=false;
  try {
    need(p.clockPolicy==av::PlaybackClockPolicy::AudioPreferred || p.clockPolicy==av::PlaybackClockPolicy::ProducerMapped,"invalid playback clock policy");
    need(!p.sessionProvider || (p.sessionProvider->resolve && p.sessionProvider->connect),"incomplete session provider");
    need(!p.externalVideo || bool(cb.onClock),"native playback requires onClock callback");
    Options o; o.plugin=p.plugin; o.session=p.session; o.format=p.format;
    o.width=p.width;o.height=p.height;o.fps=p.fps;o.rate=p.rate;o.channels=p.channels;
    o.producerClock=p.clockPolicy==av::PlaybackClockPolicy::ProducerMapped;
    o.timeout=p.timeoutMs;o.latencyMs=p.latencyMs;o.audioSink=p.audioSink;o.stream=true;
    need(o.fps && o.width && o.height && o.timeout && o.latencyMs<=2000,"invalid playback options");
    gst_init(nullptr,nullptr);
    GError* e=nullptr; auto* plugin=loadFrameLinkPlugin(o.plugin,&e);
    if (e) {error=e->message;g_error_free(e);}
    need(plugin,"FrameLink plugin load");gst_object_unref(plugin);
    need(p.recoveryAttempts <= 16,"recovery attempt limit");
    unsigned failures=0;
    for (;;) {
      need(!stop.load(),"playback cancelled");
      // An intentional pause may last indefinitely, but cancellation is checked
      // every 5 ms. Connection and active playback remain deadline bounded.
      while (p.paused && p.paused->load()) {
        need(!stop.load(),"playback cancelled"); g_usleep(5000);
      }
      av::Endpoint ep{};
      auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(o.timeout);
      bool ended=false;
      auto sessionCallbacks=cb;
      sessionCallbacks.onSession=[&](const flSessionConfig& config,const std::string& channel,const PlaybackClock& clock){
        sessionOpen=true;if(cb.onSession)cb.onSession(config,channel,clock);
      };
      sessionCallbacks.onEnd=[&]{ended=true;if(sessionOpen){sessionOpen=false;if(cb.onEnd)cb.onEnd();}};
      try {
        while (!(p.sessionProvider ? p.sessionProvider->resolve(ep,o.timeout,error) : av::readEndpoint(o.session,ep,error))) {
          need(!stop.load(),"playback cancelled");
          need(!p.paused || !p.paused->load(),"session-paused");
          need(std::chrono::steady_clock::now()<end,"rendezvous deadline");g_usleep(10000);
        }
        runConsumer(o,ep,&sessionCallbacks,&stop,p.externalVideo,p.mute,p.paused,p.sessionProvider);
        error.clear(); return true;
      } catch (const std::exception& failure) {
        // Cover setup failures before EndNotification was installed. No old
        // clock is carried into the next session.
        if (!ended) sessionCallbacks.onEnd();
        error=failure.what();
        if (stop.load()) return false;
        if (error=="session-paused" || (p.paused && p.paused->load())) {
          if(cb.onStatus) cb.onStatus("session-paused");
          continue;
        }
        if(failures++ >= p.recoveryAttempts) return false;
        if(cb.onStatus) cb.onStatus("session-reconnecting: "+error);
        g_usleep(100000);
      }
    }
  } catch(const std::exception& e) {error=e.what();if(sessionOpen && cb.onEnd)cb.onEnd();return false;}
}
}

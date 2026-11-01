#define FL_TRACE_NS gsttrace
#define FL_TRACE_ENV "FRAMELINK_GST_TRACE"
#include "framelink_trace.hpp"
// GStreamer elements for framelink: the transport meets the ecosystem.
//
//   framelinksink  - a framelink PRODUCER. Any pipeline becomes a feed:
//                      videotestsrc ! framelinksink channel=vkrmod.in slots=12
//                    dials into a consumer-owned ring (walking a slot set when
//                    slots > 0), and drops frames when the ring is full -
//                    latest-wins backpressure is framelink's contract.
//
//   framelinksrc   - a framelink CONSUMER. The ring's owner as an element:
//                      framelinksrc channel=rec.scene ! x264enc ! ...
//                    creates the channel and pushes whatever a producer
//                    submits; on Linux `! v4l2sink device=/dev/videoN` is an
//                    instant virtual camera.
//
// The names read inverted on purpose: a GStreamer *source* of frames is a
// framelink *consumer* (it owns and creates the channel), and a GStreamer
// *sink* is a framelink *producer* (it dials in). That is the correct mapping
// of roles, stated here once so nobody re-derives it in a debugger.
//
// Stage 1 is the CPU path (sysmem caps): portable everywhere framelink runs.
// The legacy Windows sink uploads with UpdateSubresource on
// the channel's own D3D11 device and the src reads back through a cached
// staging texture - the exact patterns vkrender's receivers and framecam use.
// Native source memory is opt-in; native sink/allocation negotiation is still
// under integration. See docs/GST_NATIVE_MEMORY.md for exact platform limits.

#include <algorithm>
#if defined(FL_GST_DMABUF) || defined(FL_GST_D3D11) || defined(__ANDROID__)
#define FL_GST_NATIVE 1
#include <memory>
#endif
#ifdef FL_GST_D3D11
#include <gst/d3d11/gstd3d11.h>
#endif
#ifdef FL_GST_DMABUF
#include <gst/allocators/gstdmabuf.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <gst/base/gstbasesink.h>
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <string.h>

#include "framelink.h"
#include "fl_gst_metadata.hpp"
#ifdef FL_GST_D3D11
#include "fl_gst_d3d11_producer.hpp"
#include "fl_gst_d3d11_pool.hpp"
#endif
#include "framelink_slots.h"
#ifdef FL_GST_DMABUF
#include "fl_gst_dmabuf_producer.hpp"
#endif
#ifdef __ANDROID__
#include "framelink_ahb.h"
#include "framelink_native.h"
#include "fl_gst_ahb_memory.hpp"
#include "fl_gst_ahb_producer.hpp"
#endif
#if !defined(_WIN32)
#include "framelink_dmabuf.h"
#endif
#ifdef _WIN32
#include "framelink_d3d11.h"
#endif

GST_DEBUG_CATEGORY_STATIC(framelink_debug);
#define GST_CAT_DEFAULT framelink_debug

// ---- shared helpers ---------------------------------------------------------

static flFormat toFlFormat(GstVideoFormat f) {
    return f == GST_VIDEO_FORMAT_RGB10A2_LE ? FL_FORMAT_RGB10A2 : f == GST_VIDEO_FORMAT_NV12 ? FL_FORMAT_NV12 : f == GST_VIDEO_FORMAT_RGBA ? FL_FORMAT_RGBA8 : f == GST_VIDEO_FORMAT_BGRA ? FL_FORMAT_BGRA8 : (flFormat)0;
}

static GstVideoFormat toGstFormat(flFormat f) {
    return f == FL_FORMAT_RGB10A2 ? GST_VIDEO_FORMAT_RGB10A2_LE : f == FL_FORMAT_NV12 ? GST_VIDEO_FORMAT_NV12 : f == FL_FORMAT_RGBA8 ? GST_VIDEO_FORMAT_RGBA : f == FL_FORMAT_BGRA8 ? GST_VIDEO_FORMAT_BGRA : GST_VIDEO_FORMAT_UNKNOWN;
}

// Row copy between two 4-byte-per-pixel surfaces, optionally swapping R/B -
// the only conversion these elements do themselves (BGRA ring, RGBA caps or
// vice versa). Anything richer belongs to videoconvert upstream.
static void copyPixels(guint8* dst, gsize dstStride, const guint8* src, gsize srcStride,
                       guint width, guint height, gboolean swapRB) {
    const gsize rowBytes = (gsize)width * 4;
    for (guint y = 0; y < height; ++y) {
        guint8* d = dst + y * dstStride;
        const guint8* s = src + y * srcStride;
        if (!swapRB) {
            memcpy(d, s, rowBytes);
        } else {
            for (guint x = 0; x < width; ++x) {
                d[x * 4 + 0] = s[x * 4 + 2];
                d[x * 4 + 1] = s[x * 4 + 1];
                d[x * 4 + 2] = s[x * 4 + 0];
                d[x * 4 + 3] = s[x * 4 + 3];
            }
        }
    }
}

#if defined(_WIN32) || defined(__linux__)
#define FRAMELINK_CAPS GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA, NV12, RGB10A2_LE }")
#else
#define FRAMELINK_CAPS GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA }")
#endif

// Reading a GBM/dma-buf CPU mapping with plain loads is catastrophically slow
// - the mapping is write-combined, and WC reads bypass the cache one access
// at a time. Measured on melchior (RTX 3090, LINEAR bo): 10 MB/s, 363 ms per
// 720p frame. MOVNTDQA (SSE4.1 streaming load) is the architected way to read
// WC memory at bandwidth; writes to the ordinary GstBuffer stay plain stores.
// x86-only: Android's AHardwareBuffer_lock hands back a cached mapping, so
// ARM never sees this problem.
#if defined(__GNUC__) && defined(__x86_64__)
#include <smmintrin.h>
__attribute__((target("sse4.1"))) static void wcRowCopySse41(guint8* dst, const guint8* src,
                                                             gsize bytes) {
    gsize i = 0;
    for (; i + 64 <= bytes; i += 64) {
        __m128i a = _mm_stream_load_si128((__m128i*)(src + i));
        __m128i b = _mm_stream_load_si128((__m128i*)(src + i + 16));
        __m128i c = _mm_stream_load_si128((__m128i*)(src + i + 32));
        __m128i d = _mm_stream_load_si128((__m128i*)(src + i + 48));
        _mm_storeu_si128((__m128i*)(dst + i), a);
        _mm_storeu_si128((__m128i*)(dst + i + 16), b);
        _mm_storeu_si128((__m128i*)(dst + i + 32), c);
        _mm_storeu_si128((__m128i*)(dst + i + 48), d);
    }
    for (; i + 16 <= bytes; i += 16) {
        _mm_storeu_si128((__m128i*)(dst + i),
                         _mm_stream_load_si128((__m128i*)(src + i)));
    }
    if (i < bytes) memcpy(dst + i, src + i, bytes - i);
}

static void wcCopy(guint8* dst, gsize dstStride, const guint8* src, gsize srcStride,
                   guint width, guint height) {
    static const gboolean sse41 = __builtin_cpu_supports("sse4.1");
    const gsize rowBytes = (gsize)width * 4;
    for (guint y = 0; y < height; ++y) {
        const guint8* s = src + y * srcStride;
        guint8* d = dst + y * dstStride;
        if (sse41 && ((uintptr_t)s & 15) == 0) wcRowCopySse41(d, s, rowBytes);
        else memcpy(d, s, rowBytes);
    }
}
#else
static void wcCopy(guint8* dst, gsize dstStride, const guint8* src, gsize srcStride,
                   guint width, guint height) {
    copyPixels(dst, dstStride, src, srcStride, width, height, FALSE);
}
#endif

#if defined(FL_GST_D3D11)
#define FL_GST_NATIVE_SINK 1
#define FL_SINK_MEMORY "memory:D3D11Memory"
using NativeSinkProducer=FrameLinkD3DProducer;
#elif defined(__ANDROID__)
#define FL_GST_NATIVE_SINK 1
#define FL_SINK_MEMORY "memory:FrameLinkAHardwareBuffer"
using NativeSinkProducer=FrameLinkAhbProducer;
#elif defined(FL_GST_DMABUF)
#define FL_GST_NATIVE_SINK 1
#define FL_SINK_MEMORY "memory:DMABuf"
using NativeSinkProducer=FrameLinkDmabufProducer;
#else
#define FL_SINK_MEMORY "unsupported-native-memory"
#endif

// =============================================================================
// framelinksink
// =============================================================================

typedef struct _GstFrameLinkSink {
    GstBaseSink parent;

    // properties
    gchar* channel; // exact channel name, or the slot-set prefix when slots > 0
    gboolean preserveTimestamps;
    gboolean preserveFrameMetadata;
    guint64 streamGeneration;
    gboolean nativeMemory;
#ifdef FL_GST_NATIVE_SINK
    std::shared_ptr<NativeSinkProducer>* nativeProducer;
#endif
    gint slots;     // 0 = open `channel` directly; N = walk `channel.0..N-1`

    flChannel* ch;
    gboolean delegated; // one adopted lease; never fall back to named redial
    flChannelInfo cinfo;
    GstVideoInfo vinfo;
    gboolean haveInfo;
    gint64 nextConnectUs; // lazy-connect rate limit (the consumer may not be up)
    gboolean busyWarned;
    guint connectTimeoutMs;
    guint bufferWaitMs;
    gboolean errorOnRingFull;
    gboolean testOpenDelayed;
    guint64 submitted, dropped, received, ringFullDrops;
#ifndef _WIN32
    gboolean mapRefused; // consumer ring not FL_MAP_CPU - fatal, but say why
#endif
} GstFrameLinkSink;

typedef struct _GstFrameLinkSinkClass {
    GstBaseSinkClass parent;
} GstFrameLinkSinkClass;

G_DEFINE_TYPE(GstFrameLinkSink, gst_framelink_sink, GST_TYPE_BASE_SINK)
#define GST_FRAMELINK_SINK(o) \
    (G_TYPE_CHECK_INSTANCE_CAST((o), gst_framelink_sink_get_type(), GstFrameLinkSink))

enum { SINK_PROP_0, SINK_PROP_CHANNEL, SINK_PROP_SLOTS, SINK_PROP_PRESERVE_TIMESTAMPS, SINK_PROP_FRAME_METADATA, SINK_PROP_CONNECT_TIMEOUT, SINK_PROP_BUFFER_WAIT, SINK_PROP_ERROR_ON_RING_FULL, SINK_PROP_NATIVE_MEMORY };

#ifdef FL_GST_D3D11
#define FL_SINK_CAPS FRAMELINK_CAPS ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:D3D11Memory", "{ BGRA, RGBA, NV12, RGB10A2_LE }")
#elif defined(__ANDROID__)
#define FL_SINK_CAPS FRAMELINK_CAPS ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:FrameLinkAHardwareBuffer", "{ RGBA, RGB10A2_LE }")
#elif defined(FL_GST_DMABUF)
#define FL_SINK_CAPS FRAMELINK_CAPS ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:DMABuf", "{ BGRA, RGBA, NV12, RGB10A2_LE }")
#else
#define FL_SINK_CAPS FRAMELINK_CAPS
#endif
static GstStaticPadTemplate sinkTemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(FL_SINK_CAPS));

static void sinkDisconnect(GstFrameLinkSink* self) {
#ifdef FL_GST_NATIVE_SINK
    if(self->nativeProducer){(*self->nativeProducer)->active=false;delete self->nativeProducer;self->nativeProducer=nullptr;self->ch=nullptr;}
#endif
    if (self->ch) {
        flClose(self->ch);
        self->ch = NULL;
    }
}

// One connection attempt, rate-limited to 1/s. Never fatal: the consumer may
// start after the pipeline (a receiver launched at boot), die and come back
// (renderer restart) - the sink just keeps dialling.
static void sinkTryConnect(GstFrameLinkSink* self) {
    if(self->ch || self->delegated)return;
    const gint64 now = g_get_monotonic_time();
    if (now < self->nextConnectUs) return;
    self->nextConnectUs = now + G_USEC_PER_SEC;

    const guint w = self->haveInfo ? GST_VIDEO_INFO_WIDTH(&self->vinfo) : 0;
    const guint h = self->haveInfo ? GST_VIDEO_INFO_HEIGHT(&self->vinfo) : 0;
    const flFormat fmt =
        self->haveInfo ? toFlFormat(GST_VIDEO_INFO_FORMAT(&self->vinfo)) : FL_FORMAT_BGRA8;

    gsttrace::Scope openTrace("gst.producer_open",gsttrace::id(self->channel));
    // Deterministic setup-delay fault: before import, never attached to a frame.
    if(!self->testOpenDelayed) {
        self->testOpenDelayed=TRUE;
        const char* delay=g_getenv("FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS");
        if(delay)g_usleep((std::min)(g_ascii_strtoull(delay,nullptr,10),guint64(5000))*1000);
    }
    flResult r;
    guint32 slotIndex = 0;
    if (self->slots > 0) {
        r = flOpenProducerSlot(self->channel, (guint32)self->slots, w, h, fmt, 0, &self->ch,
                               &slotIndex);
    } else {
        r = flOpenProducer(self->channel, &self->ch);
        if (r == FL_OK && w && h) flRequestGeometry(self->ch, w, h, fmt);
    }
    openTrace.result(r);
    if (r != FL_OK) {
        // BUSY (all slots taken) is an operational condition worth one line;
        // NOT_FOUND (consumer absent) is the boring startup race - stay quiet.
        if (r == FL_BUSY && !self->busyWarned) {
            GST_WARNING_OBJECT(self, "channel '%s' busy - all producer slots taken",
                               self->channel);
            self->busyWarned = TRUE;
        }
        return;
    }
    self->busyWarned = FALSE;
    flQuery(self->ch, &self->cinfo);
    GST_INFO_OBJECT(self, "producing on '%s%s%u' (%ux%u %s, ring of %u)", self->channel,
                    self->slots > 0 ? "." : "", self->slots > 0 ? slotIndex : 0,
                    self->cinfo.width, self->cinfo.height,
                    gst_video_format_to_string(toGstFormat(self->cinfo.format)),
                    self->cinfo.poolSize);
}

// Opt-in session readiness. The default remains lazy reconnect for ordinary
// gst-launch pipelines; coordinated A/V sessions pay device import cost before
// choosing/releasing their common playback epoch, not in the first render.
static gboolean sinkStartLegacy(GstBaseSink* base) {
    auto* self=GST_FRAMELINK_SINK(base);
    if(self->delegated) {
        if(self->ch)return TRUE;
        GST_ELEMENT_ERROR(self,RESOURCE,OPEN_WRITE,("Fresh broker video lease required"),(NULL));
        return FALSE;
    }
    self->testOpenDelayed=FALSE;
    if(!self->connectTimeoutMs)return TRUE;
    // A previous stream's caps are not this stream's geometry. Claim the
    // existing consumer ring unchanged; fresh CAPS will request its geometry.
    self->haveInfo=FALSE;
    gst_video_info_init(&self->vinfo);
    gsttrace::Scope readyTrace("gst.producer_preconnect",gsttrace::id(self->channel));
    const auto deadline=g_get_monotonic_time()+gint64(self->connectTimeoutMs)*1000;
    self->nextConnectUs=0;
    do {
        sinkTryConnect(self);
        if(self->ch && g_get_monotonic_time()<=deadline)return readyTrace.result(TRUE);
        if(g_get_monotonic_time()>=deadline)break;
        g_usleep(1000);
    } while(true);
    sinkDisconnect(self);
    GST_ELEMENT_ERROR(self,RESOURCE,OPEN_WRITE,("FrameLink producer readiness deadline"),
                      ("Channel %s was not ready within %u ms",self->channel,self->connectTimeoutMs));
    return readyTrace.result(FALSE);
}

static gboolean sinkStart(GstBaseSink* base) {
    auto* self=GST_FRAMELINK_SINK(base);
#ifndef FL_GST_NATIVE_SINK
    if(self->nativeMemory){GST_ELEMENT_ERROR(self,RESOURCE,SETTINGS,("native sink is not supported by this build"),(NULL));return FALSE;}
#else
    if(self->nativeMemory && !self->connectTimeoutMs)self->connectTimeoutMs=1000;
#endif
    if(!sinkStartLegacy(base))return FALSE;
#ifdef FL_GST_NATIVE_SINK
    if(self->nativeMemory){
        if(!self->ch)return FALSE;
        self->nativeProducer=new std::shared_ptr<NativeSinkProducer>(std::make_shared<NativeSinkProducer>(self->ch));
        if(!(*self->nativeProducer)->valid()){sinkDisconnect(self);return FALSE;}
    }
#endif
    return TRUE;
}
static gpointer sinkAcquireNative(GstFrameLinkSink* self,guint timeoutMs){
#ifdef FL_GST_NATIVE_SINK
    if(timeoutMs<=1000 && self->nativeMemory && self->nativeProducer && GST_STATE(self)>=GST_STATE_PAUSED)
        return (*self->nativeProducer)->acquire(timeoutMs);
#endif
    return nullptr;
}
static gboolean sinkSetNativeReadyFd(GstFrameLinkSink* self,GstBuffer* buffer,gint fd){
#if defined(__ANDROID__) || defined(FL_GST_DMABUF)
    return self->nativeMemory&&self->nativeProducer&&(*self->nativeProducer)->setReady(buffer,fd);
#else
    return FALSE;
#endif
}
static GstCaps* sinkGetCaps(GstBaseSink* base,GstCaps* filter){
    auto* self=GST_FRAMELINK_SINK(base);
    GstCaps* caps=gst_caps_from_string(FRAMELINK_CAPS);
    if(self->nativeMemory){
#ifdef FL_GST_NATIVE_SINK
#ifdef __ANDROID__
        gst_caps_unref(caps);caps=gst_caps_from_string(GST_VIDEO_CAPS_MAKE("{ RGBA, RGB10A2_LE }"));
#endif
        gst_caps_set_features(caps,0,gst_caps_features_new(FL_SINK_MEMORY,nullptr));
#else
        gst_caps_unref(caps);caps=gst_caps_new_empty();
#endif
    }
    if(filter){auto* intersect=gst_caps_intersect_full(filter,caps,GST_CAPS_INTERSECT_FIRST);gst_caps_unref(caps);return intersect;}
    return caps;
}
static gboolean sinkProposeAllocation(GstBaseSink* base,GstQuery* query){
    auto* self=GST_FRAMELINK_SINK(base);
    if(!self->nativeMemory){
        auto* parent=GST_BASE_SINK_CLASS(gst_framelink_sink_parent_class);
        return parent->propose_allocation?parent->propose_allocation(base,query):TRUE;
    }
#ifdef FL_GST_D3D11
    const auto* structure=gst_query_get_structure(query);
    const char* contract=gst_structure_get_string(structure,"framelink-completion-contract");
    if(!contract || strcmp(contract,"same-device-before-abandon-v1") || !self->nativeProducer)return FALSE;
    GstCaps* caps=nullptr;gboolean needPool=FALSE;gst_query_parse_allocation(query,&caps,&needPool);
    if(!caps || !needPool)return FALSE;
    auto* pool=flCreateD3DPool(*self->nativeProducer,caps,self->bufferWaitMs);if(!pool)return FALSE;
    GstVideoInfo vi{};gst_video_info_from_caps(&vi,caps);
    gst_query_add_allocation_pool(query,pool,(guint)vi.size,0,self->cinfo.poolSize);
    gst_query_add_allocation_meta(query,GST_VIDEO_META_API_TYPE,nullptr);gst_object_unref(pool);return TRUE;
#else
    return FALSE;
#endif
}
static gboolean sinkSetCaps(GstBaseSink* base, GstCaps* caps) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(base);
    if (!gst_video_info_from_caps(&self->vinfo, caps)) return FALSE;
    if(GST_VIDEO_INFO_FORMAT(&self->vinfo)==GST_VIDEO_FORMAT_NV12 &&
       ((GST_VIDEO_INFO_WIDTH(&self->vinfo)&1)||(GST_VIDEO_INFO_HEIGHT(&self->vinfo)&1)))return FALSE;
    if(self->nativeMemory){
        if(!self->ch || !gst_caps_features_contains(gst_caps_get_features(caps,0),FL_SINK_MEMORY) ||
           GST_VIDEO_INFO_WIDTH(&self->vinfo)!=(int)self->cinfo.width || GST_VIDEO_INFO_HEIGHT(&self->vinfo)!=(int)self->cinfo.height ||
           toFlFormat(GST_VIDEO_INFO_FORMAT(&self->vinfo))!=self->cinfo.format)return FALSE;
        self->haveInfo=TRUE;return TRUE; // no in-flight pool replacement
    }
    self->haveInfo = TRUE;
    if (self->ch) {
        // Renegotiation mid-stream: state the new geometry; the consumer may
        // reallocate (generation bump) or refuse. flQuery is the truth.
        flRequestGeometry(self->ch, GST_VIDEO_INFO_WIDTH(&self->vinfo),
                          GST_VIDEO_INFO_HEIGHT(&self->vinfo),
                          toFlFormat(GST_VIDEO_INFO_FORMAT(&self->vinfo)));
        flQuery(self->ch, &self->cinfo);
    }
    return TRUE;
}

static GstFlowReturn sinkRender(GstBaseSink* base, GstBuffer* buffer) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(base);
    gsttrace::Scope trace("gst.sinkRender",gsttrace::id(self->channel));
    ++self->received;
    // Optional identity-bearing accounting. PTS is the original media running
    // time; received is a local ordinal, not a transport sequence. No I/O.
    const auto account = [&](const char* op) {
        if (!trace.active) return;
        const GstSegment* segment = &GST_BASE_SINK(self)->segment;
        const GstClockTime pts = segment->format == GST_FORMAT_TIME
            ? gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer))
            : GST_CLOCK_TIME_NONE;
        gsttrace::record({op, gsttrace::clockNs(), 0, gsttrace::id(self->channel),
                          self->received, 0, 0,
                          GST_CLOCK_TIME_IS_VALID(pts) ? (int64_t)pts : -1, 0});
    };
    account("gst.frame_receive");
    if(self->nativeMemory){
#ifdef FL_GST_NATIVE_SINK
        guint64 pts=g_get_monotonic_time()*1000;flFrameMetadata metadata{};bool valid=true;
        if(self->preserveFrameMetadata)valid=frameMetadataFromBuffer(buffer,&base->segment,self->streamGeneration,pts,metadata);
        else if(self->preserveTimestamps)valid=frameRunningTime(&base->segment,GST_BUFFER_PTS(buffer),pts);
        if(valid && self->haveInfo && self->nativeProducer && (*self->nativeProducer)->submit(buffer,pts,self->preserveFrameMetadata?&metadata:nullptr)==FL_OK){
            ++self->submitted;account("gst.native_frame_submitted");return trace.result(GST_FLOW_OK);
        }
#endif
        ++self->dropped;account("gst.frame_drop.native_rejected");
        GST_ELEMENT_ERROR(self,STREAM,FORMAT,("native sink requires its own reserved allocation and valid metadata; no copy fallback"),(NULL));
        return trace.result(GST_FLOW_ERROR);
    }

    if (!self->haveInfo) {
        ++self->dropped;
        account("gst.frame_drop.no_caps");
        return trace.result(GST_FLOW_OK);
    }

    if (!self->ch) {
        sinkTryConnect(self);
        if (!self->ch) {
            ++self->dropped;
            account("gst.frame_drop.no_consumer");
            return trace.result(GST_FLOW_OK); // keep the pipeline alive until a consumer appears
        }
    }

    const flFormat incoming=toFlFormat(GST_VIDEO_INFO_FORMAT(&self->vinfo));
    if(self->cinfo.width!=(guint)GST_VIDEO_INFO_WIDTH(&self->vinfo) ||
       self->cinfo.height!=(guint)GST_VIDEO_INFO_HEIGHT(&self->vinfo) ||
       ((incoming==FL_FORMAT_NV12 || self->cinfo.format==FL_FORMAT_NV12 ||
         incoming==FL_FORMAT_RGB10A2 || self->cinfo.format==FL_FORMAT_RGB10A2) && incoming!=self->cinfo.format)) {
        GST_ELEMENT_ERROR(self,CORE,NEGOTIATION,("channel format/geometry does not match input"),
            ("Create the consumer with matching format and geometry; no implicit YUV/RGB conversion"));
        ++self->dropped;
        account("gst.frame_drop.negotiation");
        return trace.result(GST_FLOW_NOT_NEGOTIATED);
    }

    flBuffer fb = {};
    flResult r = flAcquireBuffer(self->ch, &fb, self->bufferWaitMs);
    if (r == FL_TIMEOUT || r == FL_BUSY) {
        ++self->ringFullDrops;
        account("gst.frame_drop.ring_full");
        ++self->dropped;
        if(self->errorOnRingFull) {
            GST_ELEMENT_ERROR(self,RESOURCE,BUSY,("FrameLink pool exhausted within configured wait budget"),
                ("buffer-wait-ms=%u; frame was not submitted",self->bufferWaitMs));
            return trace.result(GST_FLOW_ERROR);
        }
        return trace.result(GST_FLOW_OK);
    }
    if (r != FL_OK) {
        ++self->dropped;
        account("gst.frame_drop.channel_lost");
        GST_WARNING_OBJECT(self, "channel lost (%s) - will redial", flResultString(r));
        sinkDisconnect(self);
        if(self->delegated)return trace.result(GST_FLOW_ERROR);
        return trace.result(GST_FLOW_OK);
    }

    account("gst.frame_acquired");
    guint64 metadataPts=0;
    if(self->preserveFrameMetadata){
        flFrameMetadata metadata{};
        if(!frameMetadataFromBuffer(buffer,&GST_BASE_SINK(self)->segment,self->streamGeneration,metadataPts,metadata) ||
           flSetFrameMetadata(self->ch,&fb,&metadata)!=FL_OK){
            flCancelBuffer(self->ch,&fb);++self->dropped;
            GST_ELEMENT_ERROR(self,STREAM,FORMAT,("valid supported frame metadata and TIME mapping required"),(NULL));
            return trace.result(GST_FLOW_ERROR);
        }
    }
    GstVideoFrame vframe;
    if (!gst_video_frame_map(&vframe, &self->vinfo, buffer, GST_MAP_READ)) {
        ++self->dropped;
        account("gst.frame_drop.map_failed");
        sinkDisconnect(self); // do not publish uninitialized pixels
        return trace.result(GST_FLOW_ERROR);
    }
    const guint8* src = (const guint8*)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0);
    const gsize srcStride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
    const guint w = MIN((guint)GST_VIDEO_FRAME_WIDTH(&vframe), self->cinfo.width);
    const guint h = MIN((guint)GST_VIDEO_FRAME_HEIGHT(&vframe), self->cinfo.height);
    const gboolean swap =
        toFlFormat(GST_VIDEO_FRAME_FORMAT(&vframe)) != self->cinfo.format;

    gboolean wrote = FALSE;
#ifdef _WIN32
    // No CPU map on Windows: upload through the channel's own D3D11 device,
    // exactly as vkrender's WHIP receiver does.
    ID3D11Device* dev = flChannelD3D11Device(self->ch);
    ID3D11Texture2D* tex = flImageD3D11(fb.image);
    if (dev && tex) {
        ID3D11DeviceContext* ctx = NULL;
        dev->GetImmediateContext(&ctx);
        const guint8* upload = src;
        gsize uploadStride = srcStride;
        GByteArray* scratch = NULL;
        if (incoming == FL_FORMAT_NV12) {
            // D3D11 upload uses one pitch for Y and UV. Repack GstVideoMeta's
            // independently strided planes without changing their values.
            scratch = g_byte_array_sized_new(w * h * 3 / 2);
            g_byte_array_set_size(scratch, w * h * 3 / 2);
            for (guint p=0;p<2;p++) {
                const gint pitch=GST_VIDEO_FRAME_PLANE_STRIDE(&vframe,p);
                if(pitch<(gint)w) {
                    account("gst.frame_drop.invalid_pitch");
                    ctx->Release();g_byte_array_unref(scratch);
                    gst_video_frame_unmap(&vframe);sinkDisconnect(self);return trace.result(GST_FLOW_ERROR);
                }
                for(guint y=0;y<(p?h/2:h);y++)
                    memcpy(scratch->data+(p?w*h:0)+y*w,
                           (const guint8*)GST_VIDEO_FRAME_PLANE_DATA(&vframe,p)+(gsize)y*pitch,w);
            }
            upload=scratch->data;uploadStride=w;
        } else if (swap) {
            scratch = g_byte_array_sized_new(w * 4 * h);
            g_byte_array_set_size(scratch, w * 4 * h);
            copyPixels(scratch->data, w * 4, src, srcStride, w, h, TRUE);
            upload = scratch->data;
            uploadStride = w * 4;
        }
        D3D11_BOX box = {0, 0, 0, w, h, 1};
        ctx->UpdateSubresource(tex, 0, incoming==FL_FORMAT_NV12 ? nullptr : &box, upload, (UINT)uploadStride,
                               (UINT)(uploadStride * h));
        ctx->Release();
        if (scratch) g_byte_array_unref(scratch);
        wrote = TRUE;
    }
#else
    void* dst = NULL;
    guint64 dstStride = 0;
#if !defined(_WIN32)
    if(incoming==FL_FORMAT_NV12) {
        flMappedImage view{};
        r=flImageMapPlanes(fb.image,&view);
        if(r==FL_OK) {
            wrote=view.planes==2;
            for(guint p=0;p<2 && wrote;p++) {
                const gint pitch=GST_VIDEO_FRAME_PLANE_STRIDE(&vframe,p);
                if(pitch<(gint)w || view.plane[p].stride<w || view.plane[p].rows!=(p?h/2:h)) {wrote=FALSE;break;}
                for(guint y=0;y<view.plane[p].rows;y++)
                    memcpy((guint8*)view.plane[p].pixels+y*view.plane[p].stride,
                           (guint8*)GST_VIDEO_FRAME_PLANE_DATA(&vframe,p)+(gsize)y*pitch,w);
            }
            flImageUnmap(fb.image);
        }
    } else
#endif
    {
    r = flImageMap(fb.image, &dst, &dstStride);
    if (r == FL_OK) {
        copyPixels((guint8*)dst, (gsize)dstStride, src, srcStride, w, h, swap);
        flImageUnmap(fb.image);
        wrote = TRUE;
    } else if (r == FL_FORMAT_UNSUPPORTED && !self->mapRefused) {
        // The consumer allocated a ring flImageMap cannot address (no
        // FL_MAP_CPU). That is a configuration problem, not a race - say so
        // loudly, once, and keep the pipeline limping rather than killing it.
        self->mapRefused = TRUE;
        GST_ELEMENT_WARNING(self, RESOURCE, WRITE,
                            ("channel '%s' is not CPU-mappable", self->channel),
                            ("the consumer must create its ring with FL_MAP_CPU for a "
                             "CPU producer to write into it"));
    }
    }
#endif
    gst_video_frame_unmap(&vframe);

    if (wrote) {
        // Default monotonic submit time retains existing hop-latency semantics.
        // Explicit session mode instead carries agreed media running time.
        guint64 pts = self->preserveFrameMetadata?metadataPts:g_get_monotonic_time() * 1000;
        if (self->preserveTimestamps && !self->preserveFrameMetadata) {
            pts = gst_segment_to_running_time(&GST_BASE_SINK(self)->segment,
                                              GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
            if (!GST_CLOCK_TIME_IS_VALID(pts) || pts > G_MAXINT64) {
                ++self->dropped;
        account("gst.frame_drop.invalid_pts");
                sinkDisconnect(self);
                GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("valid signed-nanosecond TIME segment and PTS required"), (NULL));
                return trace.result(GST_FLOW_ERROR);
            }
        }
        if (flSubmit(self->ch, &fb, pts) != FL_OK) {
            account("gst.frame_drop.submit_failed");
            ++self->dropped;
            sinkDisconnect(self);
            if(self->preserveFrameMetadata){GST_ELEMENT_ERROR(self,RESOURCE,WRITE,("metadata frame publication failed"),(NULL));return trace.result(GST_FLOW_ERROR);}
            return trace.result(GST_FLOW_OK);
        }
        if (++self->submitted % 300 == 1)
            GST_INFO_OBJECT(self, "%" G_GUINT64_FORMAT " frames submitted, %" G_GUINT64_FORMAT
                            " dropped", self->submitted, self->dropped);
    } else {
        ++self->dropped;
        account("gst.frame_drop.copy_failed");
        sinkDisconnect(self); // Never publish a failed/partial copy.
        return trace.result(GST_FLOW_ERROR);
    }
    return trace.result(GST_FLOW_OK);
}

// EOS is serialized with render calls, so this snapshot needs no per-frame
// reporting lock or synchronous logging. Querying GstBaseSink's generic stats
// alone would hide frames dropped by the transport's full-ring policy.
static gboolean sinkEvent(GstBaseSink* base, GstEvent* event) {
    if(GST_EVENT_TYPE(event)==GST_EVENT_STREAM_START){
        auto* self=GST_FRAMELINK_SINK(base);
        if(self->streamGeneration==G_MAXUINT64)return FALSE;
        ++self->streamGeneration;
    }
    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        auto* self = GST_FRAMELINK_SINK(base);
        auto* stats = gst_structure_new("framelink-sink-stats",
            "received", G_TYPE_UINT64, self->received,
            "submitted", G_TYPE_UINT64, self->submitted,
            "dropped", G_TYPE_UINT64, self->dropped,
            "ring-full-drops", G_TYPE_UINT64, self->ringFullDrops, NULL);
        gst_element_post_message(GST_ELEMENT(base),
            gst_message_new_element(GST_OBJECT(base), stats));
    }
    return GST_BASE_SINK_CLASS(gst_framelink_sink_parent_class)->event(base, event);
}

static gboolean sinkStop(GstBaseSink* base) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(base);
    GST_INFO_OBJECT(self, "stopping: %" G_GUINT64_FORMAT " submitted, %" G_GUINT64_FORMAT
                    " dropped", self->submitted, self->dropped);
    sinkDisconnect(self);
    return TRUE;
}

static void sinkSetProperty(GObject* obj, guint id, const GValue* value, GParamSpec* spec) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(obj);
    switch (id) {
        case SINK_PROP_BUFFER_WAIT:
            if(GST_STATE(self)>GST_STATE_READY){GST_WARNING_OBJECT(self,"buffer wait cannot change while running");break;}
            self->bufferWaitMs=g_value_get_uint(value);break;
        case SINK_PROP_ERROR_ON_RING_FULL:
            if(GST_STATE(self)>GST_STATE_READY){GST_WARNING_OBJECT(self,"overload policy cannot change while running");break;}
            self->errorOnRingFull=g_value_get_boolean(value);break;
        case SINK_PROP_CONNECT_TIMEOUT:
            if(GST_STATE(self)>GST_STATE_READY){GST_WARNING_OBJECT(self,"connect timeout cannot change while running");break;}
            self->connectTimeoutMs=g_value_get_uint(value);break;
        case SINK_PROP_NATIVE_MEMORY:
            if(GST_STATE(self)>GST_STATE_READY || self->ch){GST_WARNING_OBJECT(self,"native mode cannot change while allocated");break;}
            self->nativeMemory=g_value_get_boolean(value);break;
        case SINK_PROP_CHANNEL:
            g_free(self->channel);
            self->channel = g_value_dup_string(value);
            break;
        case SINK_PROP_FRAME_METADATA:
            if(GST_STATE(self)>GST_STATE_READY || self->ch){GST_WARNING_OBJECT(self,"metadata mode cannot change while allocated");break;}
            self->preserveFrameMetadata=g_value_get_boolean(value);break;
        case SINK_PROP_PRESERVE_TIMESTAMPS:
            if (GST_STATE(self) > GST_STATE_READY) { GST_WARNING_OBJECT(self,"timestamp mode cannot change while running"); break; }
            self->preserveTimestamps = g_value_get_boolean(value); break;
        case SINK_PROP_SLOTS: self->slots = g_value_get_int(value); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void sinkGetProperty(GObject* obj, guint id, GValue* value, GParamSpec* spec) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(obj);
    switch (id) {
        case SINK_PROP_BUFFER_WAIT: g_value_set_uint(value,self->bufferWaitMs);break;
        case SINK_PROP_ERROR_ON_RING_FULL: g_value_set_boolean(value,self->errorOnRingFull);break;
        case SINK_PROP_CONNECT_TIMEOUT: g_value_set_uint(value,self->connectTimeoutMs);break;
        case SINK_PROP_NATIVE_MEMORY: g_value_set_boolean(value,self->nativeMemory);break;
        case SINK_PROP_CHANNEL: g_value_set_string(value, self->channel); break;
        case SINK_PROP_FRAME_METADATA: g_value_set_boolean(value,self->preserveFrameMetadata);break;
        case SINK_PROP_PRESERVE_TIMESTAMPS: g_value_set_boolean(value, self->preserveTimestamps); break;
        case SINK_PROP_SLOTS: g_value_set_int(value, self->slots); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void sinkFinalize(GObject* obj) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(obj);
    sinkDisconnect(self); // adoption can precede PLAYING and fail during setup
    g_free(self->channel);
    G_OBJECT_CLASS(gst_framelink_sink_parent_class)->finalize(obj);
}

// Action signal accepts one OWNED producer channel only in NULL. TRUE transfers
// ownership; FALSE leaves it with the caller. No allocation/lookup is performed.
static gboolean sinkAdoptChannel(GstFrameLinkSink* self,flChannel* channel) {
    if(GST_STATE(self)!=GST_STATE_NULL || self->ch || !channel)return FALSE;
    flChannelInfo info{};if(flQuery(channel,&info)!=FL_OK)return FALSE;
    uint32_t consumerFlags=0;if(flQueryConsumerFlags(channel,&consumerFlags)==FL_OK)return FALSE;
    self->ch=channel;self->cinfo=info;self->delegated=TRUE;return TRUE;
}

static void gst_framelink_sink_class_init(GstFrameLinkSinkClass* klass) {
    GObjectClass* gobject = G_OBJECT_CLASS(klass);
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass* basesink = GST_BASE_SINK_CLASS(klass);

    gobject->set_property = sinkSetProperty;
    gobject->get_property = sinkGetProperty;
    gobject->finalize = sinkFinalize;
    g_signal_new_class_handler("adopt-channel",G_TYPE_FROM_CLASS(klass),
        GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(sinkAdoptChannel),
        nullptr,nullptr,nullptr,G_TYPE_BOOLEAN,1,G_TYPE_POINTER);

    g_signal_new_class_handler("set-native-ready-fd",G_TYPE_FROM_CLASS(klass),
        GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(sinkSetNativeReadyFd),
        nullptr,nullptr,nullptr,G_TYPE_BOOLEAN,2,G_TYPE_POINTER,G_TYPE_INT);
    g_signal_new_class_handler("acquire-native-buffer",G_TYPE_FROM_CLASS(klass),
        GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(sinkAcquireNative),
        nullptr,nullptr,nullptr,G_TYPE_POINTER,1,G_TYPE_UINT);
    g_object_class_install_property(gobject,SINK_PROP_NATIVE_MEMORY,
        g_param_spec_boolean("native-memory","Native producer allocations",
            "Strict native buffers acquired from this sink; no implicit copy fallback",FALSE,
            (GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(
        gobject, SINK_PROP_CHANNEL,
        g_param_spec_string("channel", "Channel",
                            "framelink channel name (or slot-set prefix when slots > 0)",
                            "framelink", (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, SINK_PROP_SLOTS,
        g_param_spec_int("slots", "Slots",
                         "walk consumer slot channels <channel>.0..N-1 and take the first "
                         "free one (0 = open <channel> directly)",
                         0, 64, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject,SINK_PROP_FRAME_METADATA,
        g_param_spec_boolean("preserve-frame-metadata","Preserve frame metadata",
            "Require exact running-time PTS/DTS/duration and media generation; both endpoints must opt in",
            FALSE,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject, SINK_PROP_PRESERVE_TIMESTAMPS,
        g_param_spec_boolean("preserve-timestamps", "Preserve timestamps",
            "Opt-in agreed session running-time PTS; both pipelines must share clock and base time",
            FALSE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    gst_element_class_set_static_metadata(
        element, "framelink sink", "Sink/Video",
        "Submits video frames into a framelink channel (a framelink producer)",
        "framelink");
    gst_element_class_add_static_pad_template(element, &sinkTemplate);

    g_object_class_install_property(gobject,SINK_PROP_CONNECT_TIMEOUT,
        g_param_spec_uint("connect-timeout-ms","Connection readiness timeout",
            "Open the producer before streaming; 0 keeps lazy connection",
            0,60000,0,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject,SINK_PROP_BUFFER_WAIT,
        g_param_spec_uint("buffer-wait-ms","Buffer wait budget",
            "Maximum wait for a reusable image; zero is nonblocking, not an added presentation delay",
            0,1000,5,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject,SINK_PROP_ERROR_ON_RING_FULL,
        g_param_spec_boolean("error-on-ring-full","Reject overload",
            "Raise an error instead of dropping when the bounded buffer wait expires",
            FALSE,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    basesink->start = sinkStart;
    basesink->propose_allocation = sinkProposeAllocation;
    basesink->get_caps = sinkGetCaps;
    basesink->set_caps = sinkSetCaps;
    basesink->render = sinkRender;
    basesink->event = sinkEvent;
    basesink->stop = sinkStop;
}

static void gst_framelink_sink_init(GstFrameLinkSink* self) {
    self->channel = g_strdup("framelink");
    self->bufferWaitMs=5;
    // The scene composites on its own clock; a sink synchronising to pipeline
    // running time would just add latency framelink then measures as hop.
    gst_base_sink_set_sync(GST_BASE_SINK(self), FALSE);
}

// =============================================================================
// framelinksrc
// =============================================================================

#ifdef FL_GST_NATIVE
// Source shutdown may precede downstream GstMemory destruction. The last memory
// reader owns the channel until its lease retires; no buffer pool is reused early.
struct NativeSourceOwner {
    flChannel* channel;
#ifdef _WIN32
    GstD3D11Device* device;
    explicit NativeSourceOwner(flChannel* c):channel(c),device(gst_d3d11_device_new_wrapped(flChannelD3D11Device(c))){}
    ~NativeSourceOwner(){if(device)gst_object_unref(device);flClose(channel);}
#else
    explicit NativeSourceOwner(flChannel* c):channel(c){}
    ~NativeSourceOwner(){flClose(channel);}
#endif
};
struct NativeSourceReader {
    std::shared_ptr<NativeSourceOwner> owner;
    uint64_t token;
    ~NativeSourceReader(){
        flCompleteReadLease(owner->channel,token);
        flCollectReadLeases(owner->channel);
    }
};
#endif

typedef struct _GstFrameLinkSrc {
    GstPushSrc parent;

    // properties: the ring THIS element allocates (it is the consumer)
    gchar* channel;
    gchar* format;
    gint width, height, fps;
    guint poolSize,maxQueueAgeMs;
    gboolean nativeMemory;
#ifdef FL_GST_NATIVE
    std::shared_ptr<NativeSourceOwner>* nativeOwner;
#endif
    guint64 expiredFrames;
    gboolean preserveTimestamps;
    gboolean preserveFrameMetadata;
    guint64 streamGeneration;
    guint64 lastPts, lastSequence;
    gboolean haveTimestamp;

    flChannel* ch;
    gboolean delegated;
    flChannelInfo cinfo;
    guint64 generation; // re-read geometry + renegotiate caps when it moves
    volatile gint flushing;
#ifdef _WIN32
    ID3D11Texture2D* staging; // cached; rebuilt when the generation moves
#endif
} GstFrameLinkSrc;

typedef struct _GstFrameLinkSrcClass {
    GstPushSrcClass parent;
} GstFrameLinkSrcClass;

G_DEFINE_TYPE(GstFrameLinkSrc, gst_framelink_src, GST_TYPE_PUSH_SRC)
#define GST_FRAMELINK_SRC(o) \
    (G_TYPE_CHECK_INSTANCE_CAST((o), gst_framelink_src_get_type(), GstFrameLinkSrc))

enum { SRC_PROP_0, SRC_PROP_CHANNEL, SRC_PROP_WIDTH, SRC_PROP_HEIGHT, SRC_PROP_FPS, SRC_PROP_FORMAT, SRC_PROP_PRESERVE_TIMESTAMPS, SRC_PROP_FRAME_METADATA, SRC_PROP_POOL_SIZE, SRC_PROP_MAX_QUEUE_AGE, SRC_PROP_EXPIRED_FRAMES, SRC_PROP_NATIVE_MEMORY };

#ifdef FL_GST_DMABUF
#define FL_SOURCE_CAPS FRAMELINK_CAPS ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:DMABuf", "{ BGRA, RGBA, NV12, RGB10A2_LE }")
#elif defined(FL_GST_D3D11)
#define FL_SOURCE_CAPS FRAMELINK_CAPS ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:D3D11Memory", "{ BGRA, RGBA, NV12, RGB10A2_LE }")
#elif defined(__ANDROID__)
#define FL_SOURCE_CAPS FRAMELINK_CAPS ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:FrameLinkAHardwareBuffer", "{ RGBA, RGB10A2_LE }")
#else
#define FL_SOURCE_CAPS FRAMELINK_CAPS
#endif
static GstStaticPadTemplate srcTemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(FL_SOURCE_CAPS));

#ifdef _WIN32
static void srcDropStaging(GstFrameLinkSrc* self) {
    if (self->staging) {
        self->staging->Release();
        self->staging = NULL;
    }
}
#endif

static GstCaps* srcMakeCaps(GstFrameLinkSrc* self) {
    // After start() the channel is the truth (its format differs per platform:
    // BGRA on desktop, RGBA where AHardwareBuffer has no BGRA).
    const gint w = self->ch ? (gint)self->cinfo.width : self->width;
    const gint h = self->ch ? (gint)self->cinfo.height : self->height;
    const GstVideoFormat fmt =
        self->ch ? toGstFormat(self->cinfo.format) : gst_video_format_from_string(self->format);
    if(fmt==GST_VIDEO_FORMAT_UNKNOWN)return gst_caps_new_empty();
    GstCaps* caps=gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                               gst_video_format_to_string(fmt), "width", G_TYPE_INT, w,
                               "height", G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION,
                               self->preserveFrameMetadata?0:self->fps, 1, NULL);
    if(self->nativeMemory)gst_caps_set_features(caps,0,gst_caps_features_new(
#ifdef _WIN32
        "memory:D3D11Memory",
#elif defined(__ANDROID__)
        "memory:FrameLinkAHardwareBuffer",
#else
        "memory:DMABuf",
#endif
        nullptr));
    return caps;
}

static GstCaps* srcGetCaps(GstBaseSrc* base, GstCaps* filter) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(base);
    GstCaps* caps = srcMakeCaps(self);
    if (filter) {
        GstCaps* out = gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
        return out;
    }
    return caps;
}

static gboolean srcStart(GstBaseSrc* base) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(base);
#ifndef FL_GST_NATIVE
    if(self->nativeMemory){GST_ELEMENT_ERROR(self,RESOURCE,SETTINGS,("native-memory output is not supported by this build"),(NULL));return FALSE;}
#else
    if(self->nativeMemory && self->delegated){
        uint32_t flags=0;
        if(!self->ch || flQueryConsumerFlags(self->ch,&flags)!=FL_OK || !(flags&FL_DEFER_RELEASE) || (flags&FL_GPU_SYNC)){
            GST_ELEMENT_ERROR(self,RESOURCE,SETTINGS,("native adoption requires deferred release with normal readiness waits"),(NULL));return FALSE;
        }
    }
#endif
#ifdef __ANDROID__
    if(self->nativeMemory && strcmp(self->format,"RGBA") && strcmp(self->format,"RGB10A2_LE")){
        GST_ELEMENT_ERROR(self,RESOURCE,SETTINGS,("native AHB output currently requires exact RGBA/RGB10 layout; opaque YUV is not NV12 bytes"),(NULL));return FALSE;
    }
#endif
    // FL_MAP_CPU: this ring exists to be read byte-wise. Windows ignores the
    // flag (no CPU path) and the readback goes through a staging texture.
    flResult r = self->delegated ? (self->ch ? FL_OK : FL_DISCONNECTED) : flCreateChannelEx(self->channel, (guint32)self->width,
                                   (guint32)self->height, toFlFormat(gst_video_format_from_string(self->format)), self->poolSize, FL_MAP_CPU|(self->nativeMemory?FL_DEFER_RELEASE:0)|(self->maxQueueAgeMs?FL_TRACK_ARRIVAL:0)|(self->preserveFrameMetadata?FL_REQUIRE_FRAME_METADATA:0),
                                   &self->ch);
    if (r != FL_OK) {
        GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ,
                          ("cannot create framelink channel '%s'", self->channel),
                          ("flCreateChannelEx: %s", flResultString(r)));
        return FALSE;
    }
    flQuery(self->ch, &self->cinfo);
#ifdef FL_GST_NATIVE
    if(self->nativeMemory)self->nativeOwner=new std::shared_ptr<NativeSourceOwner>(std::make_shared<NativeSourceOwner>(self->ch));
#endif
    self->generation = self->cinfo.generation;
    self->haveTimestamp = FALSE;
    GST_OBJECT_LOCK(self);self->expiredFrames=0;GST_OBJECT_UNLOCK(self);
    gst_base_src_set_do_timestamp(base, !(self->preserveTimestamps||self->preserveFrameMetadata));
    GST_INFO_OBJECT(self, "channel '%s' up: %ux%u %s, ring of %u - waiting for a producer",
                    self->channel, self->cinfo.width, self->cinfo.height,
                    gst_video_format_to_string(toGstFormat(self->cinfo.format)),
                    self->cinfo.poolSize);
    return TRUE;
}

static gboolean srcStop(GstBaseSrc* base) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(base);
#ifdef _WIN32
    srcDropStaging(self);
#endif
#ifdef FL_GST_NATIVE
    if(self->nativeOwner){delete self->nativeOwner;self->nativeOwner=nullptr;self->ch=nullptr;}
#endif
    if (self->ch) {flClose(self->ch);self->ch=nullptr;}
    return TRUE;
}

static gboolean srcUnlock(GstBaseSrc* base) {
    g_atomic_int_set(&GST_FRAMELINK_SRC(base)->flushing, 1);
    return TRUE;
}

static gboolean srcUnlockStop(GstBaseSrc* base) {
    g_atomic_int_set(&GST_FRAMELINK_SRC(base)->flushing, 0);
    return TRUE;
}

static flResult srcReleaseFrame(GstFrameLinkSrc* self,const flFrame* frame) {
    if(!self->nativeMemory)return flRelease(self->ch,frame);
    uint64_t token=0;auto r=flLeaseFrame(self->ch,frame,&token);
    if(r!=FL_OK)return r;
    r=flCompleteReadLease(self->ch,token);
    return r==FL_OK?flCollectReadLeases(self->ch):r;
}

#ifdef FL_GST_DMABUF
static GstBuffer* srcNativeBuffer(GstFrameLinkSrc* self,const flFrame& frame) {
    flDmabuf d{};
    if(!self->nativeOwner || flImageDmabuf(frame.image,&d)!=FL_OK || d.modifier!=0)return nullptr;
    guint planes=self->cinfo.format==FL_FORMAT_NV12?2:1;
    if(d.planes!=planes)return nullptr;
    struct stat first{};if(fstat(d.fd[0],&first))return nullptr;
    gsize offsets[GST_VIDEO_MAX_PLANES]{};gint strides[GST_VIDEO_MAX_PLANES]{};gsize bytes=0;
    for(guint i=0;i<planes;++i){
        struct stat st{};if(fstat(d.fd[i],&st)||st.st_dev!=first.st_dev||st.st_ino!=first.st_ino)return nullptr;
        guint rows=i?self->cinfo.height/2:self->cinfo.height;
        if(d.stride[i]>G_MAXINT || d.offset[i]>G_MAXSIZE || !rows ||
           d.stride[i]>(G_MAXSIZE-d.offset[i])/rows)return nullptr;
        offsets[i]=d.offset[i];strides[i]=d.stride[i];bytes=std::max(bytes,offsets[i]+gsize(strides[i])*rows);
    }
    int fd=fcntl(d.fd[0],F_DUPFD_CLOEXEC,0);if(fd<0)return nullptr;
    auto*allocator=gst_dmabuf_allocator_new();auto*mem=gst_dmabuf_allocator_alloc(allocator,fd,bytes);gst_object_unref(allocator);
    if(!mem){close(fd);return nullptr;}
    uint64_t token=0;if(flLeaseFrame(self->ch,&frame,&token)!=FL_OK){gst_memory_unref(mem);return nullptr;}
    auto*reader=new NativeSourceReader{*self->nativeOwner,token};
    gst_mini_object_set_qdata(GST_MINI_OBJECT(mem),g_quark_from_static_string("framelink-reader"),reader,
        [](gpointer p){delete static_cast<NativeSourceReader*>(p);});
    auto*buf=gst_buffer_new();gst_buffer_append_memory(buf,mem);
    gst_buffer_add_video_meta_full(buf,GST_VIDEO_FRAME_FLAG_NONE,toGstFormat(self->cinfo.format),
        self->cinfo.width,self->cinfo.height,planes,offsets,strides);
    return buf;
}
#endif

#ifdef FL_GST_D3D11
static GstBuffer* srcNativeBuffer(GstFrameLinkSrc* self,const flFrame& frame) {
    if(!self->nativeOwner || !(*self->nativeOwner)->device)return nullptr;
    auto*tex=flImageD3D11(frame.image);if(!tex)return nullptr;
    // size=0 asks GStreamer to probe an EMPTY staging allocation's pitch, not
    // copy the shared image. Do not guess CPU stride from logical row width.
    auto*mem=gst_d3d11_allocator_alloc_wrapped(nullptr,(*self->nativeOwner)->device,tex,0,nullptr,nullptr);
    if(!mem)return nullptr;
    guint pitch=0;if(!gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(mem),&pitch) || !pitch || pitch>G_MAXINT){gst_memory_unref(mem);return nullptr;}
    uint64_t token=0;if(flLeaseFrame(self->ch,&frame,&token)!=FL_OK){gst_memory_unref(mem);return nullptr;}
    auto*reader=new NativeSourceReader{*self->nativeOwner,token};
    gst_mini_object_set_qdata(GST_MINI_OBJECT(mem),g_quark_from_static_string("framelink-reader"),reader,
        [](gpointer p){delete static_cast<NativeSourceReader*>(p);});
    // A later explicit CPU map must download current shared pixels, not return
    // the empty staging probe. Native GPU consumers use the original texture.
    GST_MINI_OBJECT_FLAG_SET(mem,GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
    auto*buf=gst_buffer_new();gst_buffer_append_memory(buf,mem);
    gsize offsets[GST_VIDEO_MAX_PLANES]{};gint strides[GST_VIDEO_MAX_PLANES]{};
    guint planes=self->cinfo.format==FL_FORMAT_NV12?2:1;
    strides[0]=pitch;if(planes==2){strides[1]=pitch;offsets[1]=gsize(pitch)*self->cinfo.height;}
    gst_buffer_add_video_meta_full(buf,GST_VIDEO_FRAME_FLAG_NONE,toGstFormat(self->cinfo.format),
        self->cinfo.width,self->cinfo.height,planes,offsets,strides);
    return buf;
}
#endif

#ifdef __ANDROID__
static GstBuffer* srcNativeBuffer(GstFrameLinkSrc* self,const flFrame& frame){
    if(!self->nativeOwner)return nullptr;
    auto*ahb=flImageAHardwareBuffer(frame.image);if(!ahb)return nullptr;
    AHardwareBuffer_Desc d{};AHardwareBuffer_describe(ahb,&d);
    bool rgba=self->cinfo.format==FL_FORMAT_RGBA8&&d.format==AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    bool rgb10=self->cinfo.format==FL_FORMAT_RGB10A2&&d.format==AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
    if((!rgba&&!rgb10)||d.layers!=1||d.stride<d.width||d.stride>G_MAXINT/4||
       d.width!=self->cinfo.width||d.height!=self->cinfo.height)return nullptr;
    gsize bytes=gsize(d.stride)*d.height*4;
    auto*a=GST_ALLOCATOR(g_object_new(gst_framelink_ahb_allocator_get_type(),nullptr));
    auto*m=g_new0(FrameLinkAhbMemory,1);m->buffer=ahb;AHardwareBuffer_acquire(ahb);
    gst_memory_init(&m->memory,(GstMemoryFlags)0,a,nullptr,bytes,0,0,bytes);gst_object_unref(a);
    uint64_t token=0;if(flLeaseFrame(self->ch,&frame,&token)!=FL_OK){gst_memory_unref(&m->memory);return nullptr;}
    gst_mini_object_set_qdata(GST_MINI_OBJECT(&m->memory),g_quark_from_static_string("framelink-ahb"),ahb,nullptr);
    auto*reader=new NativeSourceReader{*self->nativeOwner,token};
    gst_mini_object_set_qdata(GST_MINI_OBJECT(&m->memory),g_quark_from_static_string("framelink-reader"),reader,
        [](gpointer p){delete static_cast<NativeSourceReader*>(p);});
    auto*buf=gst_buffer_new();gst_buffer_append_memory(buf,&m->memory);
    gsize offsets[GST_VIDEO_MAX_PLANES]{};gint strides[GST_VIDEO_MAX_PLANES]{};strides[0]=d.stride*4;
    gst_buffer_add_video_meta_full(buf,GST_VIDEO_FRAME_FLAG_NONE,toGstFormat(self->cinfo.format),d.width,d.height,1,offsets,strides);
    return buf;
}
#endif

static GstFlowReturn srcCreate(GstPushSrc* push, GstBuffer** outbuf) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(push);
    gsttrace::Scope trace("gst.srcCreate",gsttrace::id(self->channel));

    flFrame frame = {};
    for (;;) {
        if (g_atomic_int_get(&self->flushing)) return trace.result(GST_FLOW_FLUSHING);
        if(self->nativeMemory){
            auto collected=flCollectReadLeases(self->ch);
            if(collected!=FL_OK&&collected!=FL_BUSY){
                GST_ELEMENT_ERROR(self,RESOURCE,READ,("native reader retirement failed"),("%s",flResultString(collected)));
                return trace.result(GST_FLOW_ERROR);
            }
        }
        flResult r = flAcquireFrame(self->ch, &frame, 100);
        if (r == FL_OK) {
            if(self->maxQueueAgeMs) {
                uint64_t age=0;auto ageResult=flFrameAgeNs(self->ch,&frame,&age);
                if(ageResult!=FL_OK){srcReleaseFrame(self,&frame);GST_ELEMENT_ERROR(self,RESOURCE,SETTINGS,("Arrival tracking required by max-queue-age-ms"),(NULL));return trace.result(GST_FLOW_ERROR);}
                if(age>uint64_t(self->maxQueueAgeMs)*1000000) {
                    srcReleaseFrame(self,&frame);
                    GST_OBJECT_LOCK(self);++self->expiredFrames;GST_OBJECT_UNLOCK(self);
                    if(trace.active)gsttrace::record({"gst.frame_drop.expired",gsttrace::clockNs(),age,gsttrace::id(self->channel),frame.sequence,frame._readyValue,frame.slot,frame.ptsNs,0});
                    continue;
                }
            }
            break;
        }
        // TIMEOUT = no producer yet; DISCONNECTED = producer left, the ring
        // (ours) lives on and the next producer just dials in. Both mean wait.
        if (r == FL_TIMEOUT || (r == FL_DISCONNECTED && !self->delegated)) continue;
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("framelink acquire failed"),
                          ("flAcquireFrame: %s", flResultString(r)));
        return trace.result(GST_FLOW_ERROR);
    }

    if ((self->preserveTimestamps||self->preserveFrameMetadata) && frame.ptsNs < 0) {
        srcReleaseFrame(self, &frame);
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("session frame has no valid timestamp"), (NULL));
        return trace.result(GST_FLOW_ERROR);
    }

    flFrameMetadata metadata{};
    if(self->preserveFrameMetadata && flGetFrameMetadata(self->ch,&frame,&metadata)!=FL_OK){
        srcReleaseFrame(self,&frame);
        GST_ELEMENT_ERROR(self,STREAM,FORMAT,("required frame metadata missing"),(NULL));
        return trace.result(GST_FLOW_ERROR);
    }

    if(trace.active){trace.event.sequence=frame.sequence;trace.event.pts=frame.ptsNs;trace.event.fence=frame._readyValue;trace.event.slot=frame.slot;}

    // A producer's flRequestGeometry may have reallocated OUR ring (that is
    // the consumer-owned contract: we allocate, they state intent, the library
    // honours it). New generation = new buffers AND possibly a new size, so
    // renegotiate downstream before pushing the first frame of the new ring.
    flChannelInfo now = {};
    flQuery(self->ch, &now);
    if (now.generation != self->generation) {
        GST_INFO_OBJECT(self, "ring reallocated: %ux%u -> %ux%u (generation %" G_GUINT64_FORMAT
                        ")", self->cinfo.width, self->cinfo.height, now.width, now.height,
                        now.generation);
        self->cinfo = now;
        self->generation = now.generation;
        self->haveTimestamp = FALSE;
#ifdef _WIN32
        srcDropStaging(self); // stale size/format; rebuilt below
#endif
        GstCaps* caps = srcMakeCaps(self);
        const gboolean ok = gst_base_src_set_caps(GST_BASE_SRC(self), caps);
        gst_caps_unref(caps);
        if (!ok) {
            srcReleaseFrame(self, &frame);
            GST_ELEMENT_ERROR(self, CORE, NEGOTIATION,
                              ("downstream refused the producer's new geometry"), (NULL));
            return trace.result(GST_FLOW_ERROR);
        }
    }

    const guint w = self->cinfo.width, h = self->cinfo.height;
    GstVideoInfo outputInfo;
    gst_video_info_init(&outputInfo);
    if(!gst_video_info_set_format(&outputInfo,toGstFormat(self->cinfo.format),w,h)) {
        srcReleaseFrame(self,&frame);return trace.result(GST_FLOW_NOT_NEGOTIATED);
    }
    GstBuffer* buf=nullptr;
#ifdef FL_GST_NATIVE
    if(self->nativeMemory){
        buf=srcNativeBuffer(self,frame);
        if(!buf){srcReleaseFrame(self,&frame);GST_ELEMENT_ERROR(self,RESOURCE,SETTINGS,("native layout/lease unsupported; refusing CPU fallback"),(NULL));return trace.result(GST_FLOW_ERROR);}
    } else
#endif
    {
    buf = gst_buffer_new_allocate(NULL, outputInfo.size, NULL);
    GstMapInfo map;
    gboolean filled = FALSE;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
#ifdef _WIN32
        ID3D11Device* dev = flChannelD3D11Device(self->ch);
        ID3D11Texture2D* tex = flImageD3D11(frame.image);
        if (dev && tex) {
            ID3D11DeviceContext* ctx = NULL;
            dev->GetImmediateContext(&ctx);
            if (!self->staging) {
                D3D11_TEXTURE2D_DESC td = {};
                tex->GetDesc(&td);
                td.Usage = D3D11_USAGE_STAGING;
                td.BindFlags = 0;
                td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                td.MiscFlags = 0;
                dev->CreateTexture2D(&td, NULL, &self->staging);
            }
            if (self->staging) {
                ctx->CopyResource(self->staging, tex);
                D3D11_MAPPED_SUBRESOURCE ms;
                if (SUCCEEDED(gsttrace::call("gst.d3d11_readback_map", [&]{return ctx->Map(self->staging, 0, D3D11_MAP_READ, 0, &ms);}))) {
                    if(self->cinfo.format==FL_FORMAT_NV12) {
                        memset(map.data,0,map.size);
                        for(guint p=0;p<2;p++)for(guint y=0;y<(p?h/2:h);y++)
                            memcpy(map.data+outputInfo.offset[p]+y*outputInfo.stride[p],
                                   (const guint8*)ms.pData+(p?(gsize)ms.RowPitch*h:0)+(gsize)y*ms.RowPitch,w);
                    } else {
                        copyPixels(map.data, (gsize)w * 4, (const guint8*)ms.pData, ms.RowPitch,w,h,FALSE);
                    }
                    ctx->Unmap(self->staging, 0);
                    filled = TRUE;
                }
            }
            ctx->Release();
        }
#else
#if !defined(_WIN32)
        if(self->cinfo.format==FL_FORMAT_NV12) {
            flMappedImage view{};
            if(flImageMapPlanes(frame.image,&view)==FL_OK) {
                filled=view.planes==2;
                memset(map.data,0,map.size);
                for(guint p=0;p<2 && filled;p++) {
                    if(view.plane[p].stride<w || view.plane[p].rows!=(p?h/2:h)) {filled=FALSE;break;}
                    for(guint y=0;y<view.plane[p].rows;y++)
                        memcpy(map.data+outputInfo.offset[p]+(gsize)y*outputInfo.stride[p],
                               (guint8*)view.plane[p].pixels+y*view.plane[p].stride,w);
                }
                flImageUnmap(frame.image);
            }
        } else
#endif
        {
        void* pixels = NULL;
        guint64 stride = 0;
        if (flImageMap(frame.image, &pixels, &stride) == FL_OK) {
            // WC-aware copy: see wcCopy - a plain memcpy here ran at 10 MB/s.
            wcCopy(map.data, (gsize)w * 4, (const guint8*)pixels, (gsize)stride, w, h);
            flImageUnmap(frame.image);
            filled = TRUE;
        }
        }
#endif
        gst_buffer_unmap(buf, &map);
    }
    srcReleaseFrame(self, &frame);

    if (!filled) {
        gst_buffer_unref(buf);
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("cannot read frame pixels"),
                          ("platform readback failed on '%s'", self->channel));
        return trace.result(GST_FLOW_ERROR);
    }
    } // CPU path; native output has transferred ownership to GstMemory.
    if (!self->preserveFrameMetadata && self->fps > 0)
        GST_BUFFER_DURATION(buf) = gst_util_uint64_scale(GST_SECOND, 1, (guint64)self->fps);
    if (self->preserveTimestamps||self->preserveFrameMetadata) {
        GST_BUFFER_PTS(buf) = frame.ptsNs;
        if (!self->haveTimestamp || frame.ptsNs <= self->lastPts ||
            frame.sequence != self->lastSequence + 1 ||
            (self->preserveFrameMetadata && metadata.streamGeneration!=self->streamGeneration))
            GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);
        self->lastPts = frame.ptsNs;
        self->lastSequence = frame.sequence;
        self->haveTimestamp = TRUE;
    }
    if(self->preserveFrameMetadata){
        GST_BUFFER_DTS(buf)=(metadata.flags&FL_FRAME_HAS_DTS)?metadata.dtsNs:GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(buf)=(metadata.flags&FL_FRAME_HAS_DURATION)?metadata.durationNs:GST_CLOCK_TIME_NONE;
        if(metadata.flags&FL_FRAME_DISCONT)GST_BUFFER_FLAG_SET(buf,GST_BUFFER_FLAG_DISCONT);
        if(GST_BUFFER_FLAG_IS_SET(buf,GST_BUFFER_FLAG_DISCONT))metadata.flags|=FL_FRAME_DISCONT;
        self->streamGeneration=metadata.streamGeneration;
        auto* meta=(GstFrameLinkFrameMeta*)gst_buffer_add_meta(buf,frameMetaInfo(),nullptr);
        if(!meta){gst_buffer_unref(buf);return trace.result(GST_FLOW_ERROR);}
        meta->frame=metadata;
    }
    *outbuf = buf; // default remains arrival-time timestamping
    return trace.result(GST_FLOW_OK);
}

static void srcSetProperty(GObject* obj, guint id, const GValue* value, GParamSpec* spec) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(obj);
    switch (id) {
        case SRC_PROP_NATIVE_MEMORY:
            if(GST_STATE(self)>GST_STATE_READY || self->ch){GST_WARNING_OBJECT(self,"memory mode cannot change while allocated");break;}
            self->nativeMemory=g_value_get_boolean(value);break;
        case SRC_PROP_MAX_QUEUE_AGE:
            if(GST_STATE(self)>GST_STATE_READY || self->ch){GST_WARNING_OBJECT(self,"age policy cannot change while allocated");break;}
            self->maxQueueAgeMs=g_value_get_uint(value);break;
        case SRC_PROP_POOL_SIZE:
            if(GST_STATE(self)>GST_STATE_READY || self->ch){GST_WARNING_OBJECT(self,"pool size cannot change while allocated");break;}
            self->poolSize=g_value_get_uint(value);break;
        case SRC_PROP_CHANNEL:
            g_free(self->channel);
            self->channel = g_value_dup_string(value);
            break;
        case SRC_PROP_FORMAT:
            if (self->ch) { GST_WARNING_OBJECT(self,"format cannot change while the channel is running"); break; }
            g_free(self->format);
            self->format=g_strdup(g_value_get_string(value) ? g_value_get_string(value) : "BGRA");
            break;
        case SRC_PROP_WIDTH: self->width = g_value_get_int(value); break;
        case SRC_PROP_HEIGHT: self->height = g_value_get_int(value); break;
        case SRC_PROP_FRAME_METADATA:
            if(GST_STATE(self)>GST_STATE_READY || self->ch){GST_WARNING_OBJECT(self,"metadata mode cannot change while allocated");break;}
            self->preserveFrameMetadata=g_value_get_boolean(value);break;
        case SRC_PROP_PRESERVE_TIMESTAMPS:
            if (GST_STATE(self) > GST_STATE_READY) { GST_WARNING_OBJECT(self,"timestamp mode cannot change while running"); break; }
            self->preserveTimestamps = g_value_get_boolean(value); break;
        case SRC_PROP_FPS: self->fps = g_value_get_int(value); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void srcGetProperty(GObject* obj, guint id, GValue* value, GParamSpec* spec) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(obj);
    switch (id) {
        case SRC_PROP_NATIVE_MEMORY: g_value_set_boolean(value,self->nativeMemory);break;
        case SRC_PROP_EXPIRED_FRAMES: GST_OBJECT_LOCK(self);g_value_set_uint64(value,self->expiredFrames);GST_OBJECT_UNLOCK(self);break;
        case SRC_PROP_MAX_QUEUE_AGE: g_value_set_uint(value,self->maxQueueAgeMs);break;
        case SRC_PROP_POOL_SIZE: g_value_set_uint(value,self->ch?self->cinfo.poolSize:self->poolSize);break;
        case SRC_PROP_CHANNEL: g_value_set_string(value, self->channel); break;
        case SRC_PROP_FORMAT: g_value_set_string(value,self->format); break;
        case SRC_PROP_WIDTH: g_value_set_int(value, self->width); break;
        case SRC_PROP_HEIGHT: g_value_set_int(value, self->height); break;
        case SRC_PROP_FRAME_METADATA: g_value_set_boolean(value,self->preserveFrameMetadata);break;
        case SRC_PROP_PRESERVE_TIMESTAMPS: g_value_set_boolean(value, self->preserveTimestamps); break;
        case SRC_PROP_FPS: g_value_set_int(value, self->fps); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void srcFinalize(GObject* obj) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(obj);
#ifdef FL_GST_NATIVE
    if(self->nativeOwner){delete self->nativeOwner;self->nativeOwner=nullptr;self->ch=nullptr;}
#endif
    if(self->ch){flClose(self->ch);self->ch=nullptr;}
    g_free(self->channel);
    g_free(self->format);
    G_OBJECT_CLASS(gst_framelink_src_parent_class)->finalize(obj);
}

static gboolean srcAdoptChannel(GstFrameLinkSrc* self,flChannel* channel) {
    if(GST_STATE(self)!=GST_STATE_NULL || self->ch || !channel)return FALSE;
    flChannelInfo info{};if(flQuery(channel,&info)!=FL_OK)return FALSE;
    if(info.width!=(guint)self->width || info.height!=(guint)self->height ||
       info.format!=toFlFormat(gst_video_format_from_string(self->format)))return FALSE;
    uint32_t flags=0;if(flQueryConsumerFlags(channel,&flags)!=FL_OK)return FALSE;
    if(self->nativeMemory && (!(flags&FL_DEFER_RELEASE)||(flags&FL_GPU_SYNC)))return FALSE;
    if(self->maxQueueAgeMs && !(flags&FL_TRACK_ARRIVAL))return FALSE;
    if(self->preserveFrameMetadata && !(flags&FL_REQUIRE_FRAME_METADATA))return FALSE;
    self->ch=channel;self->cinfo=info;self->delegated=TRUE;return TRUE;
}

static void gst_framelink_src_class_init(GstFrameLinkSrcClass* klass) {
    GObjectClass* gobject = G_OBJECT_CLASS(klass);
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass* basesrc = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass* pushsrc = GST_PUSH_SRC_CLASS(klass);

    gobject->set_property = srcSetProperty;
    gobject->get_property = srcGetProperty;
    gobject->finalize = srcFinalize;
    g_signal_new_class_handler("adopt-channel",G_TYPE_FROM_CLASS(klass),
        GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(srcAdoptChannel),
        nullptr,nullptr,nullptr,G_TYPE_BOOLEAN,1,G_TYPE_POINTER);

    g_object_class_install_property(
        gobject, SRC_PROP_CHANNEL,
        g_param_spec_string("channel", "Channel",
                            "framelink channel to create (this element is the consumer "
                            "and owns the ring)",
                            "framelink", (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, SRC_PROP_FORMAT,
        g_param_spec_string("format","Format","Ring pixel format (BGRA, RGBA, NV12 or RGB10A2_LE; device-dependent)","BGRA",
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, SRC_PROP_WIDTH,
        g_param_spec_int("width", "Width", "ring width to allocate", 16, 8192, 1280,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, SRC_PROP_HEIGHT,
        g_param_spec_int("height", "Height", "ring height to allocate", 16, 8192, 720,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, SRC_PROP_FPS,
        g_param_spec_int("fps", "FPS", "nominal frame rate advertised in the caps", 1, 240,
                         30, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject,SRC_PROP_NATIVE_MEMORY,
        g_param_spec_boolean("native-memory","Native memory output",
            "Opt-in native DMA-BUF, D3D11 or exact RGB AHardwareBuffer output; unsupported paths fail without CPU fallback",
            FALSE,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject,SRC_PROP_MAX_QUEUE_AGE,
        g_param_spec_uint("max-queue-age-ms","Receiver queue age limit",
            "Discard frames whose local receipt/readiness wait exceeds this age; 0 disables; never derived from media PTS",
            0,60000,0,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject,SRC_PROP_EXPIRED_FRAMES,
        g_param_spec_uint64("expired-frames","Expired frames","Frames discarded by receiver-local age policy",
            0,G_MAXUINT64,0,(GParamFlags)(G_PARAM_READABLE|G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject,SRC_PROP_POOL_SIZE,
        g_param_spec_uint("pool-size","Transport image capacity",
            "Allocated images, not a queue-fill target or presentation delay; adopted channels retain their actual capacity",
            1,FL_MAX_POOL_SIZE,4,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject,SRC_PROP_FRAME_METADATA,
        g_param_spec_boolean("preserve-frame-metadata","Preserve frame metadata",
            "Require exact running-time PTS/DTS/duration and media generation; both endpoints must opt in",
            FALSE,(GParamFlags)(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(gobject, SRC_PROP_PRESERVE_TIMESTAMPS,
        g_param_spec_boolean("preserve-timestamps", "Preserve timestamps",
            "Opt-in agreed session running-time PTS; both pipelines must share clock and base time",
            FALSE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    gst_element_class_set_static_metadata(
        element, "framelink source", "Source/Video",
        "Creates a framelink channel and pushes submitted frames (a framelink consumer)",
        "framelink");
    gst_element_class_add_static_pad_template(element, &srcTemplate);

    basesrc->get_caps = srcGetCaps;
    basesrc->start = srcStart;
    basesrc->stop = srcStop;
    basesrc->unlock = srcUnlock;
    basesrc->unlock_stop = srcUnlockStop;
    pushsrc->create = srcCreate;
}

static void gst_framelink_src_init(GstFrameLinkSrc* self) {
    self->poolSize=4;
    self->channel = g_strdup("framelink");
    self->format = g_strdup("BGRA");
    self->width = 1280;
    self->height = 720;
    self->fps = 30;
    gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
    gst_base_src_set_do_timestamp(GST_BASE_SRC(self), TRUE);
}

// ---- plugin -----------------------------------------------------------------

static gboolean plugin_init(GstPlugin* plugin) {
    GST_DEBUG_CATEGORY_INIT(framelink_debug, "framelink", 0, "framelink elements");
    return gst_element_register(plugin, "framelinksink", GST_RANK_NONE,
                                gst_framelink_sink_get_type()) &&
           gst_element_register(plugin, "framelinksrc", GST_RANK_NONE,
                                gst_framelink_src_get_type());
}

#ifndef PACKAGE
#define PACKAGE "framelink"
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.1.0"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, framelink,
                  "framelink zero-copy frame transport elements", plugin_init,
                  PACKAGE_VERSION, "MIT/X11", PACKAGE, "https://github.com/jsuppe/framelink")

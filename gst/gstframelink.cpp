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
// Windows has no flImageMap, so the sink uploads with UpdateSubresource on
// the channel's own D3D11 device and the src reads back through a cached
// staging texture - the exact patterns vkrender's receivers and framecam use.
// Zero-copy caps (memory:DmaBuf, memory:D3D11Memory) are a later stage.

#include <gst/base/gstbasesink.h>
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <string.h>

#include "framelink.h"
#include "framelink_slots.h"
#ifdef _WIN32
#include "framelink_d3d11.h"
#endif

GST_DEBUG_CATEGORY_STATIC(framelink_debug);
#define GST_CAT_DEFAULT framelink_debug

// ---- shared helpers ---------------------------------------------------------

static flFormat toFlFormat(GstVideoFormat f) {
    return f == GST_VIDEO_FORMAT_RGBA ? FL_FORMAT_RGBA8 : FL_FORMAT_BGRA8;
}

static GstVideoFormat toGstFormat(flFormat f) {
    return f == FL_FORMAT_RGBA8 ? GST_VIDEO_FORMAT_RGBA : GST_VIDEO_FORMAT_BGRA;
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

#define FRAMELINK_CAPS GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA }")

// =============================================================================
// framelinksink
// =============================================================================

typedef struct _GstFrameLinkSink {
    GstBaseSink parent;

    // properties
    gchar* channel; // exact channel name, or the slot-set prefix when slots > 0
    gint slots;     // 0 = open `channel` directly; N = walk `channel.0..N-1`

    flChannel* ch;
    flChannelInfo cinfo;
    GstVideoInfo vinfo;
    gboolean haveInfo;
    gint64 nextConnectUs; // lazy-connect rate limit (the consumer may not be up)
    gboolean busyWarned;
    guint64 submitted, dropped;
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

enum { SINK_PROP_0, SINK_PROP_CHANNEL, SINK_PROP_SLOTS };

static GstStaticPadTemplate sinkTemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(FRAMELINK_CAPS));

static void sinkDisconnect(GstFrameLinkSink* self) {
    if (self->ch) {
        flClose(self->ch);
        self->ch = NULL;
    }
}

// One connection attempt, rate-limited to 1/s. Never fatal: the consumer may
// start after the pipeline (a receiver launched at boot), die and come back
// (renderer restart) - the sink just keeps dialling.
static void sinkTryConnect(GstFrameLinkSink* self) {
    const gint64 now = g_get_monotonic_time();
    if (now < self->nextConnectUs) return;
    self->nextConnectUs = now + G_USEC_PER_SEC;

    const guint w = self->haveInfo ? GST_VIDEO_INFO_WIDTH(&self->vinfo) : 0;
    const guint h = self->haveInfo ? GST_VIDEO_INFO_HEIGHT(&self->vinfo) : 0;
    const flFormat fmt =
        self->haveInfo ? toFlFormat(GST_VIDEO_INFO_FORMAT(&self->vinfo)) : FL_FORMAT_BGRA8;

    flResult r;
    guint32 slotIndex = 0;
    if (self->slots > 0) {
        r = flOpenProducerSlot(self->channel, (guint32)self->slots, w, h, fmt, 0, &self->ch,
                               &slotIndex);
    } else {
        r = flOpenProducer(self->channel, &self->ch);
        if (r == FL_OK && w && h) flRequestGeometry(self->ch, w, h, fmt);
    }
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
                    self->cinfo.format == FL_FORMAT_RGBA8 ? "RGBA" : "BGRA",
                    self->cinfo.poolSize);
}

static gboolean sinkSetCaps(GstBaseSink* base, GstCaps* caps) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(base);
    if (!gst_video_info_from_caps(&self->vinfo, caps)) return FALSE;
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
    if (!self->haveInfo) return GST_FLOW_OK;

    if (!self->ch) {
        sinkTryConnect(self);
        if (!self->ch) {
            ++self->dropped;
            return GST_FLOW_OK; // keep the pipeline alive until a consumer appears
        }
    }

    flBuffer fb = {};
    flResult r = flAcquireBuffer(self->ch, &fb, 5);
    if (r == FL_TIMEOUT || r == FL_BUSY) {
        ++self->dropped; // ring full: the consumer is behind, latest-wins says drop
        return GST_FLOW_OK;
    }
    if (r != FL_OK) {
        GST_WARNING_OBJECT(self, "channel lost (%s) - will redial", flResultString(r));
        sinkDisconnect(self);
        return GST_FLOW_OK;
    }

    GstVideoFrame vframe;
    if (!gst_video_frame_map(&vframe, &self->vinfo, buffer, GST_MAP_READ)) {
        flSubmit(self->ch, &fb, -1); // hand the slot back; nothing to write
        return GST_FLOW_OK;
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
        if (swap) {
            scratch = g_byte_array_sized_new(w * 4 * h);
            g_byte_array_set_size(scratch, w * 4 * h);
            copyPixels(scratch->data, w * 4, src, srcStride, w, h, TRUE);
            upload = scratch->data;
            uploadStride = w * 4;
        }
        D3D11_BOX box = {0, 0, 0, w, h, 1};
        ctx->UpdateSubresource(tex, 0, &box, upload, (UINT)uploadStride,
                               (UINT)(uploadStride * h));
        ctx->Release();
        if (scratch) g_byte_array_unref(scratch);
        wrote = TRUE;
    }
#else
    void* dst = NULL;
    guint64 dstStride = 0;
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
#endif
    gst_video_frame_unmap(&vframe);

    if (wrote) {
        // Stamp with monotonic now: framelink pts measures producer-to-scene
        // hop latency, not media time.
        flSubmit(self->ch, &fb, g_get_monotonic_time() * 1000);
        if (++self->submitted % 300 == 1)
            GST_INFO_OBJECT(self, "%" G_GUINT64_FORMAT " frames submitted, %" G_GUINT64_FORMAT
                            " dropped", self->submitted, self->dropped);
    } else {
        flSubmit(self->ch, &fb, -1); // slot must go back regardless
    }
    return GST_FLOW_OK;
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
        case SINK_PROP_CHANNEL:
            g_free(self->channel);
            self->channel = g_value_dup_string(value);
            break;
        case SINK_PROP_SLOTS: self->slots = g_value_get_int(value); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void sinkGetProperty(GObject* obj, guint id, GValue* value, GParamSpec* spec) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(obj);
    switch (id) {
        case SINK_PROP_CHANNEL: g_value_set_string(value, self->channel); break;
        case SINK_PROP_SLOTS: g_value_set_int(value, self->slots); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void sinkFinalize(GObject* obj) {
    GstFrameLinkSink* self = GST_FRAMELINK_SINK(obj);
    g_free(self->channel);
    G_OBJECT_CLASS(gst_framelink_sink_parent_class)->finalize(obj);
}

static void gst_framelink_sink_class_init(GstFrameLinkSinkClass* klass) {
    GObjectClass* gobject = G_OBJECT_CLASS(klass);
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass* basesink = GST_BASE_SINK_CLASS(klass);

    gobject->set_property = sinkSetProperty;
    gobject->get_property = sinkGetProperty;
    gobject->finalize = sinkFinalize;

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

    gst_element_class_set_static_metadata(
        element, "framelink sink", "Sink/Video",
        "Submits video frames into a framelink channel (a framelink producer)",
        "framelink");
    gst_element_class_add_static_pad_template(element, &sinkTemplate);

    basesink->set_caps = sinkSetCaps;
    basesink->render = sinkRender;
    basesink->stop = sinkStop;
}

static void gst_framelink_sink_init(GstFrameLinkSink* self) {
    self->channel = g_strdup("framelink");
    // The scene composites on its own clock; a sink synchronising to pipeline
    // running time would just add latency framelink then measures as hop.
    gst_base_sink_set_sync(GST_BASE_SINK(self), FALSE);
}

// =============================================================================
// framelinksrc
// =============================================================================

typedef struct _GstFrameLinkSrc {
    GstPushSrc parent;

    // properties: the ring THIS element allocates (it is the consumer)
    gchar* channel;
    gint width, height, fps;

    flChannel* ch;
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

enum { SRC_PROP_0, SRC_PROP_CHANNEL, SRC_PROP_WIDTH, SRC_PROP_HEIGHT, SRC_PROP_FPS };

static GstStaticPadTemplate srcTemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(FRAMELINK_CAPS));

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
        self->ch ? toGstFormat(self->cinfo.format) : GST_VIDEO_FORMAT_BGRA;
    return gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                               gst_video_format_to_string(fmt), "width", G_TYPE_INT, w,
                               "height", G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION,
                               self->fps, 1, NULL);
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
    // FL_MAP_CPU: this ring exists to be read byte-wise. Windows ignores the
    // flag (no CPU path) and the readback goes through a staging texture.
    flResult r = flCreateChannelEx(self->channel, (guint32)self->width,
                                   (guint32)self->height, FL_FORMAT_BGRA8, 4, FL_MAP_CPU,
                                   &self->ch);
    if (r != FL_OK) {
        GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ,
                          ("cannot create framelink channel '%s'", self->channel),
                          ("flCreateChannelEx: %s", flResultString(r)));
        return FALSE;
    }
    flQuery(self->ch, &self->cinfo);
    self->generation = self->cinfo.generation;
    GST_INFO_OBJECT(self, "channel '%s' up: %ux%u %s, ring of %u - waiting for a producer",
                    self->channel, self->cinfo.width, self->cinfo.height,
                    self->cinfo.format == FL_FORMAT_RGBA8 ? "RGBA" : "BGRA",
                    self->cinfo.poolSize);
    return TRUE;
}

static gboolean srcStop(GstBaseSrc* base) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(base);
#ifdef _WIN32
    srcDropStaging(self);
#endif
    if (self->ch) {
        flClose(self->ch);
        self->ch = NULL;
    }
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

static GstFlowReturn srcCreate(GstPushSrc* push, GstBuffer** outbuf) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(push);

    flFrame frame = {};
    for (;;) {
        if (g_atomic_int_get(&self->flushing)) return GST_FLOW_FLUSHING;
        flResult r = flAcquireFrame(self->ch, &frame, 100);
        if (r == FL_OK) break;
        // TIMEOUT = no producer yet; DISCONNECTED = producer left, the ring
        // (ours) lives on and the next producer just dials in. Both mean wait.
        if (r == FL_TIMEOUT || r == FL_DISCONNECTED) continue;
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("framelink acquire failed"),
                          ("flAcquireFrame: %s", flResultString(r)));
        return GST_FLOW_ERROR;
    }

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
#ifdef _WIN32
        srcDropStaging(self); // stale size/format; rebuilt below
#endif
        GstCaps* caps = srcMakeCaps(self);
        const gboolean ok = gst_base_src_set_caps(GST_BASE_SRC(self), caps);
        gst_caps_unref(caps);
        if (!ok) {
            flRelease(self->ch, &frame);
            GST_ELEMENT_ERROR(self, CORE, NEGOTIATION,
                              ("downstream refused the producer's new geometry"), (NULL));
            return GST_FLOW_ERROR;
        }
    }

    const guint w = self->cinfo.width, h = self->cinfo.height;
    GstBuffer* buf = gst_buffer_new_allocate(NULL, (gsize)w * h * 4, NULL);
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
                if (SUCCEEDED(ctx->Map(self->staging, 0, D3D11_MAP_READ, 0, &ms))) {
                    copyPixels(map.data, (gsize)w * 4, (const guint8*)ms.pData, ms.RowPitch,
                               w, h, FALSE);
                    ctx->Unmap(self->staging, 0);
                    filled = TRUE;
                }
            }
            ctx->Release();
        }
#else
        void* pixels = NULL;
        guint64 stride = 0;
        if (flImageMap(frame.image, &pixels, &stride) == FL_OK) {
            copyPixels(map.data, (gsize)w * 4, (const guint8*)pixels, (gsize)stride, w, h,
                       FALSE);
            flImageUnmap(frame.image);
            filled = TRUE;
        }
#endif
        gst_buffer_unmap(buf, &map);
    }
    flRelease(self->ch, &frame);

    if (!filled) {
        gst_buffer_unref(buf);
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("cannot read frame pixels"),
                          ("platform readback failed on '%s'", self->channel));
        return GST_FLOW_ERROR;
    }
    if (self->fps > 0)
        GST_BUFFER_DURATION(buf) = gst_util_uint64_scale(GST_SECOND, 1, (guint64)self->fps);
    *outbuf = buf; // live source with do-timestamp: basesrc stamps arrival time
    return GST_FLOW_OK;
}

static void srcSetProperty(GObject* obj, guint id, const GValue* value, GParamSpec* spec) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(obj);
    switch (id) {
        case SRC_PROP_CHANNEL:
            g_free(self->channel);
            self->channel = g_value_dup_string(value);
            break;
        case SRC_PROP_WIDTH: self->width = g_value_get_int(value); break;
        case SRC_PROP_HEIGHT: self->height = g_value_get_int(value); break;
        case SRC_PROP_FPS: self->fps = g_value_get_int(value); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void srcGetProperty(GObject* obj, guint id, GValue* value, GParamSpec* spec) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(obj);
    switch (id) {
        case SRC_PROP_CHANNEL: g_value_set_string(value, self->channel); break;
        case SRC_PROP_WIDTH: g_value_set_int(value, self->width); break;
        case SRC_PROP_HEIGHT: g_value_set_int(value, self->height); break;
        case SRC_PROP_FPS: g_value_set_int(value, self->fps); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec); break;
    }
}

static void srcFinalize(GObject* obj) {
    GstFrameLinkSrc* self = GST_FRAMELINK_SRC(obj);
    g_free(self->channel);
    G_OBJECT_CLASS(gst_framelink_src_parent_class)->finalize(obj);
}

static void gst_framelink_src_class_init(GstFrameLinkSrcClass* klass) {
    GObjectClass* gobject = G_OBJECT_CLASS(klass);
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass* basesrc = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass* pushsrc = GST_PUSH_SRC_CLASS(klass);

    gobject->set_property = srcSetProperty;
    gobject->get_property = srcGetProperty;
    gobject->finalize = srcFinalize;

    g_object_class_install_property(
        gobject, SRC_PROP_CHANNEL,
        g_param_spec_string("channel", "Channel",
                            "framelink channel to create (this element is the consumer "
                            "and owns the ring)",
                            "framelink", (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
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
    self->channel = g_strdup("framelink");
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

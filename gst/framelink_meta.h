#pragma once
#include <gst/gst.h>
#include "framelink.h"
/* Registered by gstframelink. Keep the buffer alive while using this pointer.
 * This carries media generation, not native allocation identity. */
typedef struct { GstMeta meta; flFrameMetadata frame; } GstFrameLinkFrameMeta;
static inline GstFrameLinkFrameMeta* gst_buffer_get_framelink_frame_meta(GstBuffer* buffer) {
    GType api=g_type_from_name("GstFrameLinkFrameMetaAPI");
    return api?(GstFrameLinkFrameMeta*)gst_buffer_get_meta(buffer,api):NULL;
}

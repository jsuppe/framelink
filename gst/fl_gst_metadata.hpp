#pragma once
#include "framelink_meta.h"
#include <cmath>
static const GstMetaInfo* frameMetaInfo();
static gboolean frameMetaInit(GstMeta* meta,gpointer,GstBuffer*) {
    ((GstFrameLinkFrameMeta*)meta)->frame={};return TRUE;
}
static gboolean frameMetaCopy(GstBuffer* dest,GstMeta* meta,GstBuffer*,GQuark type,gpointer data) {
    if(!GST_META_TRANSFORM_IS_COPY(type))return FALSE;
    auto* copy=(GstMetaTransformCopy*)data;
    if(copy->region)return FALSE; // A byte-range copy is not necessarily a frame.
    auto* out=(GstFrameLinkFrameMeta*)gst_buffer_add_meta(dest,frameMetaInfo(),nullptr);
    if(!out)return FALSE;
    out->frame=((GstFrameLinkFrameMeta*)meta)->frame;return TRUE;
}
static const GstMetaInfo* frameMetaInfo() {
    static gsize value=0;
    if(g_once_init_enter(&value)) {
        static const gchar* tags[]={"video",nullptr};
        GType api=gst_meta_api_type_register("GstFrameLinkFrameMetaAPI",tags);
        const auto* info=gst_meta_register(api,"GstFrameLinkFrameMeta",sizeof(GstFrameLinkFrameMeta),frameMetaInit,nullptr,frameMetaCopy);
        g_once_init_leave(&value,(gsize)info);
    }
    return (const GstMetaInfo*)value;
}
static bool frameRunningTime(const GstSegment* segment,guint64 time,guint64& out) {
    if(!GST_CLOCK_TIME_IS_VALID(time))return false;
    out=gst_segment_to_running_time(segment,GST_FORMAT_TIME,time);
    return GST_CLOCK_TIME_IS_VALID(out)&&out<=G_MAXINT64;
}
static bool frameMetadataFromBuffer(GstBuffer* buffer,const GstSegment* segment,
                                    guint64 generation,guint64& pts,flFrameMetadata& out) {
    if(segment->format!=GST_FORMAT_TIME || !std::isfinite(segment->rate) || segment->rate<=0 ||
       !frameRunningTime(segment,GST_BUFFER_PTS(buffer),pts))return false;
    out={};out.version=FL_FRAME_METADATA_VERSION;out.streamGeneration=generation;
    if(auto* meta=gst_buffer_get_framelink_frame_meta(buffer)) {
        if(meta->frame.version!=FL_FRAME_METADATA_VERSION || !meta->frame.streamGeneration)return false;
        out.streamGeneration=meta->frame.streamGeneration;
    }
    if(!out.streamGeneration)return false;
    if(GST_BUFFER_DTS_IS_VALID(buffer)) {
        guint64 dts;if(!frameRunningTime(segment,GST_BUFFER_DTS(buffer),dts))return false;
        out.flags|=FL_FRAME_HAS_DTS;out.dtsNs=(int64_t)dts;
    }
    if(GST_BUFFER_DURATION_IS_VALID(buffer)) {
        auto duration=GST_BUFFER_DURATION(buffer),start=GST_BUFFER_PTS(buffer);
        if(duration>G_MAXUINT64-start)return false;
        guint64 end;if(!frameRunningTime(segment,start+duration,end)||end<pts)return false;
        out.flags|=FL_FRAME_HAS_DURATION;out.durationNs=end-pts;
    }
    if(GST_BUFFER_FLAG_IS_SET(buffer,GST_BUFFER_FLAG_DISCONT))out.flags|=FL_FRAME_DISCONT;
    return true;
}

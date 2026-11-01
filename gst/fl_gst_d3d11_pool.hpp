#pragma once
#include "fl_gst_d3d11_producer.hpp"
// This pool is offered ONLY after upstream explicitly accepts the completion
// contract. Generic decoders must not accidentally select it and abandon queued
// GPU writes without a completion integration.
typedef struct {
    GstBufferPool parent;
    std::shared_ptr<FrameLinkD3DProducer>* owner;
    guint waitMs;
} FrameLinkD3DPool;
typedef struct { GstBufferPoolClass parent; } FrameLinkD3DPoolClass;
G_DEFINE_TYPE(FrameLinkD3DPool,framelink_d3d_pool,GST_TYPE_BUFFER_POOL)
static gboolean flPoolStart(GstBufferPool*){return TRUE;}
static gboolean flPoolStop(GstBufferPool*){return TRUE;}
static const gchar** flPoolOptions(GstBufferPool*){
    static const gchar* options[]={GST_BUFFER_POOL_OPTION_VIDEO_META,"FrameLinkExplicitCompletionV1",nullptr};return options;
}
static gboolean flPoolConfig(GstBufferPool* base,GstStructure* config){
    auto* self=(FrameLinkD3DPool*)base;GstCaps*caps=nullptr;guint size=0,min=0,max=0;
    if(!self->owner || !gst_buffer_pool_config_get_params(config,&caps,&size,&min,&max) || !caps || min!=0 || max!=(*self->owner)->info.poolSize)return FALSE;
    GstVideoInfo vi{};if(!gst_video_info_from_caps(&vi,caps))return FALSE;
    auto& ci=(*self->owner)->info;
    const char* format=ci.format==FL_FORMAT_RGBA8?"RGBA":ci.format==FL_FORMAT_BGRA8?"BGRA":ci.format==FL_FORMAT_NV12?"NV12":"RGB10A2_LE";
    if(GST_VIDEO_INFO_WIDTH(&vi)!=(int)ci.width || GST_VIDEO_INFO_HEIGHT(&vi)!=(int)ci.height ||
       GST_VIDEO_INFO_FORMAT(&vi)!=gst_video_format_from_string(format) ||
       gst_caps_features_is_any(gst_caps_get_features(caps,0)) ||
       !gst_caps_features_contains(gst_caps_get_features(caps,0),"memory:D3D11Memory") ||
       !gst_buffer_pool_config_has_option(config,"FrameLinkExplicitCompletionV1"))return FALSE;
    return GST_BUFFER_POOL_CLASS(framelink_d3d_pool_parent_class)->set_config(base,config);
}
static GstFlowReturn flPoolAcquire(GstBufferPool* base,GstBuffer** out,GstBufferPoolAcquireParams* params){
    auto* self=(FrameLinkD3DPool*)base;
    if(GST_BUFFER_POOL_IS_FLUSHING(base)||!self->owner||!(*self->owner)->active.load())return GST_FLOW_FLUSHING;
    guint timeout=params&&(params->flags&GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT)?0:self->waitMs;
    auto* buffer=(*self->owner)->acquire(timeout);
    if(!buffer)return (*self->owner)->active.load()?GST_FLOW_EOS:GST_FLOW_FLUSHING;
    *out=buffer;return GST_FLOW_OK;
}
static void flPoolRelease(GstBufferPool* base,GstBuffer* buffer){
    // Never cache a formerly-published reservation as a free writable buffer.
    // The actual image pool persists; only the lightweight wrapper is retired.
    GST_BUFFER_FLAG_SET(buffer,GST_BUFFER_FLAG_TAG_MEMORY);
    GST_BUFFER_POOL_CLASS(framelink_d3d_pool_parent_class)->release_buffer(base,buffer);
}
static void flPoolFinalize(GObject* object){
    auto*self=(FrameLinkD3DPool*)object;delete self->owner;
    G_OBJECT_CLASS(framelink_d3d_pool_parent_class)->finalize(object);
}
static void framelink_d3d_pool_class_init(FrameLinkD3DPoolClass* klass){
    auto*pool=GST_BUFFER_POOL_CLASS(klass);pool->start=flPoolStart;pool->stop=flPoolStop;
    pool->get_options=flPoolOptions;pool->set_config=flPoolConfig;pool->acquire_buffer=flPoolAcquire;pool->release_buffer=flPoolRelease;
    G_OBJECT_CLASS(klass)->finalize=flPoolFinalize;
}
static void framelink_d3d_pool_init(FrameLinkD3DPool*){}
static GstBufferPool* flCreateD3DPool(const std::shared_ptr<FrameLinkD3DProducer>& owner,GstCaps* caps,guint waitMs){
    auto*self=(FrameLinkD3DPool*)g_object_new(framelink_d3d_pool_get_type(),nullptr);
    self->owner=new std::shared_ptr<FrameLinkD3DProducer>(owner);self->waitMs=waitMs;
    auto*pool=GST_BUFFER_POOL(self);GstVideoInfo vi{};
    if(!gst_video_info_from_caps(&vi,caps)||vi.size>G_MAXUINT){gst_object_unref(pool);return nullptr;}
    auto*config=gst_buffer_pool_get_config(pool);gst_buffer_pool_config_set_params(config,caps,(guint)vi.size,0,owner->info.poolSize);
    gst_buffer_pool_config_add_option(config,GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option(config,"FrameLinkExplicitCompletionV1");
    if(!gst_buffer_pool_set_config(pool,config)){gst_object_unref(pool);return nullptr;}
    return pool;
}

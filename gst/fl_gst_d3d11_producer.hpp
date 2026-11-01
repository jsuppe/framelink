#pragma once
// Internal native sink building block. Not a decoder interoperability claim.
#include "framelink_d3d11.h"
#include <gst/d3d11/gstd3d11.h>
#include <gst/video/video.h>
#include <memory>
#include <mutex>
struct FrameLinkD3DProducer;
struct FrameLinkD3DReservation {
    std::shared_ptr<FrameLinkD3DProducer> owner;
    flBuffer buffer{};
    uint64_t reference=0;
    bool published=false;
    gsize memoryOffset=0,memorySize=0;
    ~FrameLinkD3DReservation();
};
struct FrameLinkD3DProducer : std::enable_shared_from_this<FrameLinkD3DProducer> {
    flChannel* channel;
    flChannelInfo info{};
    GstD3D11Device* device=nullptr;
    std::mutex mutex;
    explicit FrameLinkD3DProducer(flChannel* ch):channel(ch){
        if(flQuery(ch,&info)==FL_OK)device=gst_d3d11_device_new_wrapped(flChannelD3D11Device(ch));
    }
    ~FrameLinkD3DProducer(){if(device)gst_object_unref(device);flClose(channel);}
    static GQuark key(){return g_quark_from_static_string("framelink-d3d11-producer-reservation");}
    GstBuffer* acquire(guint timeoutMs){
        std::lock_guard<std::mutex> lock(mutex);
        if(!device)return nullptr;
        flBuffer b{};if(flAcquireBuffer(channel,&b,timeoutMs)!=FL_OK)return nullptr;
        uint64_t ref=0;
        if(flRetainBufferReference(channel,&b,&ref)!=FL_OK){flCancelBuffer(channel,&b);return nullptr;}
        auto* mem=gst_d3d11_allocator_alloc_wrapped(nullptr,device,flImageD3D11(b.image),0,nullptr,nullptr);
        guint pitch=0;
        if(!mem || !gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(mem),&pitch) || !pitch || pitch>G_MAXINT){
            if(mem)gst_memory_unref(mem);flCancelBuffer(channel,&b);flReleaseBufferReference(channel,ref);return nullptr;
        }
        auto* reservation=new FrameLinkD3DReservation{shared_from_this(),b,ref,false,mem->offset,mem->size};
        gst_mini_object_set_qdata(GST_MINI_OBJECT(mem),key(),reservation,[](gpointer p){delete (FrameLinkD3DReservation*)p;});
        // Native GPU access uses the original allocation. Explicit CPU maps are
        // fallback operations and must download rather than use empty staging.
        GST_MINI_OBJECT_FLAG_SET(mem,GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
        auto* result=gst_buffer_new();gst_buffer_append_memory(result,mem);
        GstVideoFormat format=GST_VIDEO_FORMAT_UNKNOWN;
        switch(info.format){case FL_FORMAT_BGRA8:format=GST_VIDEO_FORMAT_BGRA;break;case FL_FORMAT_RGBA8:format=GST_VIDEO_FORMAT_RGBA;break;
          case FL_FORMAT_NV12:format=GST_VIDEO_FORMAT_NV12;break;case FL_FORMAT_RGB10A2:format=GST_VIDEO_FORMAT_RGB10A2_LE;break;default:break;}
        gsize offsets[GST_VIDEO_MAX_PLANES]{};gint strides[GST_VIDEO_MAX_PLANES]{};
        strides[0]=pitch;guint planes=info.format==FL_FORMAT_NV12?2:1;
        if(planes==2){offsets[1]=gsize(pitch)*info.height;strides[1]=pitch;}
        gst_buffer_add_video_meta_full(result,GST_VIDEO_FRAME_FLAG_NONE,format,info.width,info.height,planes,offsets,strides);
        return result;
    }
    flResult submit(GstBuffer* buffer,int64_t pts,const flFrameMetadata* metadata=nullptr){
        std::lock_guard<std::mutex> lock(mutex);
        if(!buffer || gst_buffer_n_memory(buffer)!=1)return FL_INVALID;
        auto* mem=gst_buffer_peek_memory(buffer,0);
        auto* r=(FrameLinkD3DReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(mem),key());
        if(!r || r->owner.get()!=this || r->published || !gst_is_d3d11_memory(mem))return FL_INVALID;
        if(mem->offset!=r->memoryOffset || mem->size!=r->memorySize)return FL_INVALID;
        if(GST_MINI_OBJECT_FLAG_IS_SET(mem,GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD))return FL_FORMAT_UNSUPPORTED;
        // Only resources issued by THIS pool are accepted. A same-adapter GPU
        // texture from another allocator is not automatically the ring image.
        if(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem))!=(ID3D11Resource*)flImageD3D11(r->buffer.image))return FL_INVALID;
        if(metadata){auto result=flSetFrameMetadata(channel,&r->buffer,metadata);if(result!=FL_OK)return result;}
        // Upstream must write through this wrapped device's immediate context.
        // FrameLink signals readiness on that same ordered context, no CPU copy.
        gst_d3d11_device_lock(device);
        auto result=flSubmit(channel,&r->buffer,pts);
        gst_d3d11_device_unlock(device);
        if(result==FL_OK)r->published=true;
        return result;
    }
};
inline FrameLinkD3DReservation::~FrameLinkD3DReservation(){
    // Explicit producer adapter contract: before abandoning an unpublished
    // buffer, caller finishes its GPU work. Generic decoder cancellation needs
    // an upstream completion integration before it may use this adapter.
    std::lock_guard<std::mutex> lock(owner->mutex);
    if(!published)flCancelBuffer(owner->channel,&buffer);
    flReleaseBufferReference(owner->channel,reference);
}

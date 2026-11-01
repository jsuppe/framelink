#pragma once
#include "framelink_ahb.h"
#include "framelink_sync_fd.h"
#include "fl_gst_ahb_memory.hpp"
#include <gst/video/video.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
struct FrameLinkAhbProducer;
struct FrameLinkAhbReservation {
    std::shared_ptr<FrameLinkAhbProducer> owner;
    flBuffer buffer{};uint64_t reference=0;bool published=false,readySet=false;
    int readyFd=-1;gsize size=0;
    ~FrameLinkAhbReservation();
};
struct FrameLinkAhbProducer : std::enable_shared_from_this<FrameLinkAhbProducer> {
    flChannel* channel;flChannelInfo info{};std::mutex mutex;std::atomic<bool> active{true};
    explicit FrameLinkAhbProducer(flChannel*c):channel(c){flQuery(c,&info);}
    ~FrameLinkAhbProducer(){flClose(channel);}
    bool valid()const{return info.format==FL_FORMAT_RGBA8||info.format==FL_FORMAT_RGB10A2;}
    static GQuark key(){return g_quark_from_static_string("framelink-ahb-producer-reservation");}
    GstBuffer* acquire(guint timeoutMs){
        std::unique_lock<std::mutex> lock(mutex);if(!valid()||!active.load())return nullptr;
        const auto deadline=g_get_monotonic_time()+gint64(timeoutMs)*1000;flBuffer b{};
        for(;;){auto r=flAcquireBuffer(channel,&b,0);if(r==FL_OK)break;
            if(r!=FL_TIMEOUT||g_get_monotonic_time()>=deadline||!active.load())return nullptr;
            lock.unlock();g_usleep(1000);lock.lock();if(!active.load())return nullptr;
        }
        auto*ahb=flImageAHardwareBuffer(b.image);AHardwareBuffer_Desc d{};if(ahb)AHardwareBuffer_describe(ahb,&d);
        uint32_t expected=info.format==FL_FORMAT_RGBA8?AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
        if(!ahb||d.format!=expected||d.width!=info.width||d.height!=info.height||d.layers!=1||d.stride<d.width||d.stride>G_MAXINT/4){flCancelBuffer(channel,&b);return nullptr;}
        uint64_t ref=0;if(flRetainBufferReference(channel,&b,&ref)!=FL_OK){flCancelBuffer(channel,&b);return nullptr;}
        gsize bytes=gsize(d.stride)*d.height*4;
        auto*a=GST_ALLOCATOR(g_object_new(gst_framelink_ahb_allocator_get_type(),nullptr));
        auto*m=g_new0(FrameLinkAhbMemory,1);m->buffer=ahb;AHardwareBuffer_acquire(ahb);
        gst_memory_init(&m->memory,(GstMemoryFlags)0,a,nullptr,bytes,0,0,bytes);gst_object_unref(a);
        auto*r=new FrameLinkAhbReservation{shared_from_this(),b,ref,false,false,-1,bytes};
        gst_mini_object_set_qdata(GST_MINI_OBJECT(&m->memory),key(),r,[](gpointer p){delete (FrameLinkAhbReservation*)p;});
        gst_mini_object_set_qdata(GST_MINI_OBJECT(&m->memory),g_quark_from_static_string("framelink-ahb"),ahb,nullptr);
        auto*out=gst_buffer_new();gst_buffer_append_memory(out,&m->memory);
        gsize offsets[GST_VIDEO_MAX_PLANES]{};gint strides[GST_VIDEO_MAX_PLANES]{};strides[0]=d.stride*4;
        gst_buffer_add_video_meta_full(out,GST_VIDEO_FRAME_FLAG_NONE,info.format==FL_FORMAT_RGBA8?GST_VIDEO_FORMAT_RGBA:GST_VIDEO_FORMAT_RGB10A2_LE,d.width,d.height,1,offsets,strides);return out;
    }
    FrameLinkAhbReservation* reservation(GstBuffer*b){
        if(!b||gst_buffer_n_memory(b)!=1)return nullptr;auto*m=gst_buffer_peek_memory(b,0);
        auto*r=(FrameLinkAhbReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(m),key());
        return r&&r->owner.get()==this&&!r->published&&m->offset==0&&m->size==r->size?r:nullptr;
    }
    bool setReady(GstBuffer*b,int fd){
        std::lock_guard<std::mutex> lock(mutex);auto*r=reservation(b);
        if(!active.load()||!r||fd< -1||r->readySet)return false;
        int owned=fd<0?-1:fcntl(fd,F_DUPFD_CLOEXEC,0);if(fd>=0&&owned<0)return false;
        r->readyFd=owned;r->readySet=true;return true;
    }
    flResult submit(GstBuffer*b,int64_t pts,const flFrameMetadata* metadata=nullptr){
        std::lock_guard<std::mutex> lock(mutex);if(!active.load())return FL_DISCONNECTED;
        auto*r=reservation(b);if(!r||!r->readySet)return FL_INVALID;
        if(metadata){auto result=flSetFrameMetadata(channel,&r->buffer,metadata);if(result!=FL_OK)return result;}
        auto result=flSubmitSyncFd(channel,&r->buffer,r->readyFd,pts);if(result==FL_OK)r->published=true;return result;
    }
};
inline FrameLinkAhbReservation::~FrameLinkAhbReservation(){
    // Before abandoning an unpublished buffer caller must finish its GPU work.
    if(!published){std::lock_guard<std::mutex> lock(owner->mutex);flCancelBuffer(owner->channel,&buffer);}
    flReleaseBufferReference(owner->channel,reference);if(readyFd>=0)close(readyFd);
}

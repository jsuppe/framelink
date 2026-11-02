#pragma once
#include "framelink_dmabuf.h"
#include "framelink_sync_fd.h"
#include <gst/allocators/gstdmabuf.h>
#include <sys/stat.h>
#include <algorithm>
#include <gst/video/video.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
struct FrameLinkDmabufProducer;
struct FrameLinkDmabufReservation {
    std::shared_ptr<FrameLinkDmabufProducer> owner;
    flBuffer buffer{};uint64_t reference=0;bool published=false,readySet=false;
    int readyFd=-1;gsize size=0;
    ~FrameLinkDmabufReservation();
};
struct FrameLinkDmabufProducer : std::enable_shared_from_this<FrameLinkDmabufProducer> {
    flChannel* channel;flChannelInfo info{};std::mutex mutex;std::atomic<bool> active{true};
    explicit FrameLinkDmabufProducer(flChannel*c):channel(c){flQuery(c,&info);}
    ~FrameLinkDmabufProducer(){flClose(channel);}
    bool valid()const{return info.format==FL_FORMAT_BGRA8||info.format==FL_FORMAT_RGBA8||info.format==FL_FORMAT_NV12||info.format==FL_FORMAT_RGB10A2;}
    static GQuark key(){return g_quark_from_static_string("framelink-dmabuf-producer-reservation");}
    GstBuffer* acquire(guint timeoutMs){
        std::unique_lock<std::mutex> lock(mutex);if(!valid()||!active.load())return nullptr;
        const auto deadline=g_get_monotonic_time()+gint64(timeoutMs)*1000;flBuffer b{};
        for(;;){auto r=flAcquireBuffer(channel,&b,0);if(r==FL_OK)break;
            if(r!=FL_TIMEOUT||g_get_monotonic_time()>=deadline||!active.load())return nullptr;
            lock.unlock();g_usleep(1000);lock.lock();if(!active.load())return nullptr;
        }
        flDmabuf d{};guint planes=info.format==FL_FORMAT_NV12?2:1;
        if(flImageDmabuf(b.image,&d)!=FL_OK || d.modifier!=0 || d.planes!=planes){flCancelBuffer(channel,&b);return nullptr;}
        struct stat backing{};if(fstat(d.fd[0],&backing)){flCancelBuffer(channel,&b);return nullptr;}
        gsize offsets[GST_VIDEO_MAX_PLANES]{};gint strides[GST_VIDEO_MAX_PLANES]{};gsize bytes=0;
        for(guint i=0;i<planes;++i){struct stat current{};guint rows=i?info.height/2:info.height;
            if(fstat(d.fd[i],&current)||current.st_dev!=backing.st_dev||current.st_ino!=backing.st_ino||
               !rows||d.stride[i]>G_MAXINT||d.offset[i]>G_MAXSIZE||d.stride[i]>(G_MAXSIZE-d.offset[i])/rows){flCancelBuffer(channel,&b);return nullptr;}
            offsets[i]=d.offset[i];strides[i]=d.stride[i];bytes=std::max(bytes,offsets[i]+gsize(strides[i])*rows);
        }
        int fd=fcntl(d.fd[0],F_DUPFD_CLOEXEC,0);if(fd<0){flCancelBuffer(channel,&b);return nullptr;}
        auto*allocator=gst_dmabuf_allocator_new();auto*mem=gst_dmabuf_allocator_alloc(allocator,fd,bytes);gst_object_unref(allocator);
        if(!mem){close(fd);flCancelBuffer(channel,&b);return nullptr;}
        uint64_t ref=0;if(flRetainBufferReference(channel,&b,&ref)!=FL_OK){gst_memory_unref(mem);flCancelBuffer(channel,&b);return nullptr;}
        auto*r=new FrameLinkDmabufReservation{shared_from_this(),b,ref,false,false,-1,bytes};
        gst_mini_object_set_qdata(GST_MINI_OBJECT(mem),key(),r,[](gpointer p){delete (FrameLinkDmabufReservation*)p;});
        auto*out=gst_buffer_new();gst_buffer_append_memory(out,mem);
        GstVideoFormat format=info.format==FL_FORMAT_RGBA8?GST_VIDEO_FORMAT_RGBA:info.format==FL_FORMAT_BGRA8?GST_VIDEO_FORMAT_BGRA:info.format==FL_FORMAT_NV12?GST_VIDEO_FORMAT_NV12:GST_VIDEO_FORMAT_RGB10A2_LE;
        gst_buffer_add_video_meta_full(out,GST_VIDEO_FRAME_FLAG_NONE,format,info.width,info.height,planes,offsets,strides);return out;
    }
    FrameLinkDmabufReservation* reservation(GstBuffer*b){
        if(!b||gst_buffer_n_memory(b)!=1)return nullptr;auto*m=gst_buffer_peek_memory(b,0);
        auto*r=(FrameLinkDmabufReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(m),key());
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
inline FrameLinkDmabufReservation::~FrameLinkDmabufReservation(){
    // Before abandoning an unpublished buffer caller must finish its GPU work.
    if(!published){std::lock_guard<std::mutex> lock(owner->mutex);flCancelBuffer(owner->channel,&buffer);}
    flReleaseBufferReference(owner->channel,reference);if(readyFd>=0)close(readyFd);
}

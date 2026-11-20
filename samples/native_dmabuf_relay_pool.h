#pragma once
#include "../gst/fl_gst_dmabuf_producer.hpp"
#include "framelink_session.h"
#include <array>

namespace flsample {
// The desktop Linux counterpart of native_d3d11_relay_pool.h, with the same rules.
// Explicit broker setup, before media/GPU writes or concurrent pool users.
// The active upstream pool is a trusted FrameLink producer allocation result
// (the native framelinksink/framelinkavsink pool), not arbitrary DMA-BUF memory.
// Pin ALL unpublished slots while importing the same allocations into a second
// immutable ring. No new allocations/copies/maps.
// The caller MUST connect a read-lease relay before publishing into this ring:
// independent writers to the two rings would violate ownership. Authentication,
// session/clock agreement and terminal supervision remain caller obligations.
// Output channels are owned by the caller only on success. Partial failures
// return both null and release every temporary upstream reservation. BUSY is a
// nonblocking setup failure, not permission to run with a partial pool.
// A single-attempt export guard. Retain it until the authenticated receiver has
// imported the fds and acknowledged attachment. Failure also releases all partial
// reservations; never use the borrowed images after destroying this guard.
class NativeDmabufRelayPoolExport {
  std::array<GstBuffer*,FL_MAX_POOL_SIZE> items{};
  std::array<bool,FL_MAX_POOL_SIZE> seen{};
  std::shared_ptr<FrameLinkDmabufProducer> owner;
  bool attempted=false;
public:
  // Borrowed from the pinned producer ring: valid while this guard lives.
  std::array<flDmabuf,FL_MAX_POOL_SIZE> images{};
  flChannelInfo first{};
  NativeDmabufRelayPoolExport()=default;
  NativeDmabufRelayPoolExport(const NativeDmabufRelayPoolExport&)=delete;
  NativeDmabufRelayPoolExport& operator=(const NativeDmabufRelayPoolExport&)=delete;
  ~NativeDmabufRelayPoolExport(){for(auto*b:items)if(b)gst_buffer_unref(b);}
  flResult prepare(GstBufferPool*pool,const flSessionConfig&config,unsigned capacity){
  if(attempted)return FL_INVALID;attempted=true;
  if(!GST_IS_BUFFER_POOL(pool)||!gst_buffer_pool_is_active(pool)||capacity<2||
     capacity>FL_MAX_POOL_SIZE||flSessionValidate(&config)!=FL_SESSION_OK)return FL_INVALID;
  GstBufferPoolAcquireParams nonblocking{};nonblocking.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  for(unsigned n=0;n<capacity;++n){
    if(gst_buffer_pool_acquire_buffer(pool,&items[n],&nonblocking)!=GST_FLOW_OK)return FL_BUSY;
    auto*b=items[n];
    if(!b||gst_buffer_n_memory(b)!=1)return FL_FORMAT_UNSUPPORTED;
    auto*m=gst_buffer_peek_memory(b,0);
    if(!gst_is_dmabuf_memory(m))return FL_FORMAT_UNSUPPORTED;
    auto*r=static_cast<FrameLinkDmabufReservation*>(gst_mini_object_get_qdata(GST_MINI_OBJECT(m),FrameLinkDmabufProducer::key()));
    if(!r||!r->owner||!r->owner->active.load()||r->published||r->readySet||!r->reference||
       m->offset!=0||m->size!=r->size)return FL_FORMAT_UNSUPPORTED;
    flDmabuf image{};if(flImageDmabuf(r->buffer.image,&image)!=FL_OK||!image.planes)return FL_FORMAT_UNSUPPORTED;
    // The pool memory wraps a duplicate of the ring image's own fd.
    struct stat wrapped{},ring{};
    if(fstat(gst_dmabuf_memory_get_fd(m),&wrapped)||fstat(image.fd[0],&ring)||
       wrapped.st_dev!=ring.st_dev||wrapped.st_ino!=ring.st_ino)return FL_FORMAT_UNSUPPORTED;
    flChannelInfo info{};
    auto result=flQuery(r->owner->channel,&info);if(result!=FL_OK)return result;
    if(info.poolSize!=capacity||info.width!=config.video.width||info.height!=config.video.height||
       info.format!=config.video.format)return FL_FORMAT_UNSUPPORTED;
    if(!owner){owner=r->owner;first=info;}
    if(r->owner!=owner||info.generation!=first.generation||r->buffer.slot>=capacity||seen[r->buffer.slot])return FL_INVALID;
    seen[r->buffer.slot]=true;
    // Deliberately private slot ordering: downstream slot index is NOT an
    // upstream identity. The relay must compare actual allocations (inodes).
    images[capacity-1-r->buffer.slot]=image;
  }
  flChannelInfo now{};auto result=flQuery(owner->channel,&now);
  if(result!=FL_OK)return result;
  if(now.generation!=first.generation)return FL_DISCONNECTED;
  return FL_OK;
  }
};
inline flResult createNativeDmabufRelayPool(GstBufferPool* pool,const char* name,
    const flSessionConfig& config,unsigned capacity,bool cpuAccess,
    flChannel** reader,flChannel** writer){
  if(reader)*reader=nullptr;if(writer)*writer=nullptr;
  if(!reader||!writer||reader==writer||!name||!*name)return FL_INVALID;
  NativeDmabufRelayPoolExport reservations;
  auto result=reservations.prepare(pool,config,capacity);if(result!=FL_OK)return result;
  flChannel*consumer=nullptr;flChannel*producer=nullptr;
  result=flCreateChannelFromDmabufs(name,config.video.width,config.video.height,static_cast<flFormat>(config.video.format),capacity,
      FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(cpuAccess?FL_MAP_CPU:0),reservations.images.data(),&consumer);
  if(result!=FL_OK)return result;
  result=flOpenProducer(name,&producer);
  if(result!=FL_OK){flClose(consumer);return result;}
  *reader=consumer;*writer=producer;return FL_OK;
}
}

#pragma once
// The allocator type is the plugin's (this sample links against it).
#ifndef FL_GST_AHB_MEMORY_EXTERN
#define FL_GST_AHB_MEMORY_EXTERN 1
#endif
#include "../gst/fl_gst_ahb_producer.hpp"
#include "framelink_session.h"
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <array>
#include <memory>

namespace flsample {
// The Android counterpart of native_d3d11_relay_pool.h and
// native_dmabuf_relay_pool.h, with the same rules.
// Explicit broker setup, before media/GPU writes or concurrent pool users.
// The active upstream pool is a trusted FrameLink producer allocation result
// (the native framelinksink/framelinkavsink pool), not arbitrary AHardwareBuffers.
// Pin ALL unpublished slots while adopting the same allocations into a second
// immutable ring. No new allocations/copies/maps.
// The caller MUST connect a read-lease relay before publishing into this ring:
// independent writers to the two rings would violate ownership. Authentication,
// session/clock agreement and terminal supervision remain caller obligations.
// Output channels are owned by the caller only on success. Partial failures
// return both null and release every temporary upstream reservation. BUSY is a
// nonblocking setup failure, not permission to run with a partial pool.
// A single-attempt export guard. Retain it until the authenticated receiver has
// adopted the buffers and acknowledged attachment. Failure also releases all
// partial reservations; never use the borrowed buffers after destroying it.
// Identity is the allocation ID (AHardwareBuffer_getId, API 31+).
inline uint64_t ahbAllocationId(const AHardwareBuffer*b){
  using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
  static auto getId=reinterpret_cast<GetId>(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));
  uint64_t id=0;return b&&getId&&!getId(b,&id)?id:0;
}
class NativeAhbRelayPoolExport {
  std::array<GstBuffer*,FL_MAX_POOL_SIZE> items{};
  std::array<bool,FL_MAX_POOL_SIZE> seen{};
  std::shared_ptr<FrameLinkAhbProducer> owner;
  bool attempted=false;
public:
  // Borrowed from the pinned producer ring: valid while this guard lives.
  std::array<AHardwareBuffer*,FL_MAX_POOL_SIZE> images{};
  flChannelInfo first{};
  NativeAhbRelayPoolExport()=default;
  NativeAhbRelayPoolExport(const NativeAhbRelayPoolExport&)=delete;
  NativeAhbRelayPoolExport& operator=(const NativeAhbRelayPoolExport&)=delete;
  ~NativeAhbRelayPoolExport(){for(auto*b:items)if(b)gst_buffer_unref(b);}
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
    if(!gst_memory_is_type(m,"FrameLinkAHardwareBuffer"))return FL_FORMAT_UNSUPPORTED;
    auto*r=static_cast<FrameLinkAhbReservation*>(gst_mini_object_get_qdata(GST_MINI_OBJECT(m),FrameLinkAhbProducer::key()));
    if(!r||!r->owner||!r->owner->active.load()||r->published||r->readySet||!r->reference||
       m->offset!=0||m->size!=r->size)return FL_FORMAT_UNSUPPORTED;
    auto*image=flImageAHardwareBuffer(r->buffer.image);
    // The pool memory wraps the ring image's own allocation.
    auto*wrapped=static_cast<AHardwareBuffer*>(gst_mini_object_get_qdata(GST_MINI_OBJECT(m),g_quark_from_static_string("framelink-ahb")));
    const uint64_t id=ahbAllocationId(image);
    if(!image||!id||ahbAllocationId(wrapped)!=id)return FL_FORMAT_UNSUPPORTED;
    flChannelInfo info{};
    auto result=flQuery(r->owner->channel,&info);if(result!=FL_OK)return result;
    if(info.poolSize!=capacity||info.width!=config.video.width||info.height!=config.video.height||
       info.format!=config.video.format)return FL_FORMAT_UNSUPPORTED;
    if(!owner){owner=r->owner;first=info;}
    if(r->owner!=owner||info.generation!=first.generation||r->buffer.slot>=capacity||seen[r->buffer.slot])return FL_INVALID;
    seen[r->buffer.slot]=true;
    // Deliberately private slot ordering: downstream slot index is NOT an
    // upstream identity. The relay must compare actual allocations (IDs).
    images[capacity-1-r->buffer.slot]=image;
  }
  flChannelInfo now{};auto result=flQuery(owner->channel,&now);
  if(result!=FL_OK)return result;
  if(now.generation!=first.generation)return FL_DISCONNECTED;
  return FL_OK;
  }
};
inline flResult createNativeAhbRelayPool(GstBufferPool* pool,const char* name,
    const flSessionConfig& config,unsigned capacity,bool cpuAccess,
    flChannel** reader,flChannel** writer){
  if(reader)*reader=nullptr;if(writer)*writer=nullptr;
  if(!reader||!writer||reader==writer||!name||!*name)return FL_INVALID;
  NativeAhbRelayPoolExport reservations;
  auto result=reservations.prepare(pool,config,capacity);if(result!=FL_OK)return result;
  flChannel*consumer=nullptr;flChannel*producer=nullptr;
  result=flCreateChannelFromHardwareBuffers(name,config.video.width,config.video.height,static_cast<flFormat>(config.video.format),capacity,
      FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(cpuAccess?FL_MAP_CPU:0),reservations.images.data(),&consumer);
  if(result!=FL_OK)return result;
  result=flOpenProducer(name,&producer);
  if(result!=FL_OK){flClose(consumer);return result;}
  *reader=consumer;*writer=producer;return FL_OK;
}
}

#pragma once
#include "../gst/fl_gst_d3d11_producer.hpp"
#include "framelink_session.h"
#include <array>

namespace flsample {
// Explicit broker setup, before media/GPU writes or concurrent pool users.
// The active upstream pool is a trusted FrameLink producer allocation result,
// not arbitrary decoder memory. Pin ALL unpublished slots while importing the
// same allocations into a second immutable ring. No new textures/copies/maps.
// The caller MUST connect a read-lease relay before publishing into this ring:
// independent writers to the two rings would violate ownership. Authentication,
// session/clock agreement and terminal supervision remain caller obligations.
// Output channels are owned by the caller only on success. Partial failures
// return both null and release every temporary upstream reservation. BUSY is a
// nonblocking setup failure, not permission to run with a partial pool.
inline flResult createNativeD3D11RelayPool(GstBufferPool* pool,const char* name,
    const flSessionConfig& config,unsigned capacity,bool cpuAccess,
    flChannel** reader,flChannel** writer){
  if(reader)*reader=nullptr;if(writer)*writer=nullptr;
  if(!reader||!writer||reader==writer||!name||!*name||!GST_IS_BUFFER_POOL(pool)||
     !gst_buffer_pool_is_active(pool)||capacity<2||capacity>FL_MAX_POOL_SIZE||
     flSessionValidate(&config)!=FL_SESSION_OK)return FL_INVALID;
  struct Reservations {
    std::array<GstBuffer*,FL_MAX_POOL_SIZE> items{};
    ~Reservations(){for(auto*b:items)if(b)gst_buffer_unref(b);}
  } reservations;
  std::array<void*,FL_MAX_POOL_SIZE> handles{};
  std::array<bool,FL_MAX_POOL_SIZE> seen{};
  std::shared_ptr<FrameLinkD3DProducer> owner;
  flChannelInfo first{};
  GstBufferPoolAcquireParams nonblocking{};nonblocking.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  for(unsigned n=0;n<capacity;++n){
    if(gst_buffer_pool_acquire_buffer(pool,&reservations.items[n],&nonblocking)!=GST_FLOW_OK)return FL_BUSY;
    auto*b=reservations.items[n];
    if(!b||gst_buffer_n_memory(b)!=1)return FL_FORMAT_UNSUPPORTED;
    auto*m=gst_buffer_peek_memory(b,0);
    if(!gst_is_d3d11_memory(m))return FL_FORMAT_UNSUPPORTED;
    auto*r=static_cast<FrameLinkD3DReservation*>(gst_mini_object_get_qdata(GST_MINI_OBJECT(m),FrameLinkD3DProducer::key()));
    if(!r||!r->owner||!r->owner->active.load()||r->published||!r->reference||
       m->offset!=r->memoryOffset||m->size!=r->memorySize||
       GST_MINI_OBJECT_FLAG_IS_SET(m,GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD)||
       gst_d3d11_memory_get_subresource_index(GST_D3D11_MEMORY_CAST(m))!=0||
       gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(m))!=
         reinterpret_cast<ID3D11Resource*>(flImageD3D11(r->buffer.image)))return FL_FORMAT_UNSUPPORTED;
    flChannelInfo info{};
    auto result=flQuery(r->owner->channel,&info);if(result!=FL_OK)return result;
    if(info.poolSize!=capacity||info.width!=config.video.width||info.height!=config.video.height||
       info.format!=config.video.format)return FL_FORMAT_UNSUPPORTED;
    if(!owner){owner=r->owner;first=info;}
    if(r->owner!=owner||info.generation!=first.generation||r->buffer.slot>=capacity||seen[r->buffer.slot])return FL_INVALID;
    seen[r->buffer.slot]=true;
    // Deliberately private slot ordering: downstream slot index is NOT an
    // upstream identity. The relay must compare actual allocation handles.
    handles[capacity-1-r->buffer.slot]=flImageSharedHandle(r->buffer.image);
    if(!handles[capacity-1-r->buffer.slot])return FL_FORMAT_UNSUPPORTED;
  }
  flChannelInfo now{};auto result=flQuery(owner->channel,&now);
  if(result!=FL_OK)return result;
  if(now.generation!=first.generation)return FL_DISCONNECTED;
  flChannel*consumer=nullptr;flChannel*producer=nullptr;
  result=flCreateChannelFromD3D11Handles(name,config.video.width,config.video.height,static_cast<flFormat>(config.video.format),capacity,
      FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(cpuAccess?FL_MAP_CPU:0),first.adapterLUID,handles.data(),&consumer);
  if(result!=FL_OK)return result;
  result=flOpenProducer(name,&producer);
  if(result!=FL_OK){flClose(consumer);return result;}
  *reader=consumer;*writer=producer;return FL_OK;
}
}

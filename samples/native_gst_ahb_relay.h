#pragma once
#include "framelink_ahb.h"
#include "../gst/framelink_meta.h"
#include "../gst/framelink_native.h"
#include "native_ahb_relay_pool.h"
#include <array>

namespace flsample {
// The Android counterpart of native_gst_d3d11_relay.h and
// native_gst_dmabuf_relay.h, with the same rules.
// Native VIDEO-only adapter for a trusted FrameLink appsink. Not authentication,
// a session/timeline adapter, or an arbitrary AHardwareBuffer importer: the
// input must be an intact FrameLink native reader buffer
// (gst_framelink_memory_get_hardware_buffer, not a producer reservation), and
// its GstMemory is the upstream lease. The broker must compose immutable
// same-allocation pools and retain channels. Identity is the allocation ID
// (AHardwareBuffer_getId), never a slot index, pointer or size. No writes/copies/maps; input buffer
// timestamps must already be in the desired output timeline. Session/PCM/event
// composition is the caller's responsibility.
// Single owner. MUST finish before destruction. On downstream death preserve
// this object and its references until the whole GPU graph is safely discarded;
// never release a lease merely because cancellation was requested.
class NativeGstAhbRelay {
 public:
  NativeGstAhbRelay(flChannel* producer,uint64_t generation):down(producer),mediaGeneration(generation){}
  NativeGstAhbRelay(const NativeGstAhbRelay&)=delete;
  NativeGstAhbRelay& operator=(const NativeGstAhbRelay&)=delete;
  flResult initialize(){
    if(attempted||!down||!mediaGeneration)return FL_INVALID;attempted=true;
    if(flQuery(down,&info)!=FL_OK||!info.poolSize||info.poolSize>slots.size())return FL_INVALID;
    initialized=true;auto r=collect();if(r!=FL_OK)return r;
    for(unsigned n=0;n<info.poolSize;++n)if(!slots[n].reserved){finish();return FL_BUSY;}
    ready=true;return FL_OK;
  }
  flResult collect(){
    if(!initialized||failed)return FL_INVALID;
    flChannelInfo now{};if(flQuery(down,&now)!=FL_OK||now.generation!=info.generation){failed=true;return FL_DISCONNECTED;}
    // Never probe beyond the unreserved slots. Once the final reader has
    // completed and closed its channel, an extra acquire would report peer
    // disconnection despite every outstanding lease already being retired.
    unsigned missing=0;for(unsigned n=0;n<info.poolSize;++n)if(!slots[n].reserved)++missing;
    for(unsigned n=0;n<missing;++n){
      flBuffer b{};auto r=flAcquireBuffer(down,&b,0);if(r==FL_TIMEOUT)return FL_OK;
      if(r!=FL_OK){failed=true;return r;}
      if(b.slot>=info.poolSize||slots[b.slot].reserved){failed=true;return FL_INVALID;}
      auto&s=slots[b.slot];s.buffer=b;s.reserved=true;
      s.id=ahbAllocationId(flImageAHardwareBuffer(b.image));
      if(!s.id){failed=true;return FL_INVALID;}
      if(s.memory){gst_memory_unref(s.memory);s.memory=nullptr;--inFlight;}
    }
    return FL_OK;
  }
  flResult forward(GstBuffer* input){
    if(!ready||!input||gst_buffer_n_memory(input)!=1||!GST_BUFFER_PTS_IS_VALID(input)||GST_BUFFER_PTS(input)>G_MAXINT64)return FL_INVALID;
    auto*m=gst_buffer_peek_memory(input,0);auto*meta=gst_buffer_get_framelink_frame_meta(input);
    if(!meta||meta->frame.version!=FL_FRAME_METADATA_VERSION||meta->frame.streamGeneration!=mediaGeneration)return FL_INVALID;
    auto r=collect();if(r!=FL_OK)return r;
    // Reader memory only: a producer reservation is not an upstream lease.
    auto*native=gst_framelink_memory_get_hardware_buffer(m);
    if(!native||gst_mini_object_get_qdata(GST_MINI_OBJECT(m),FrameLinkAhbProducer::key()))return FL_FORMAT_UNSUPPORTED;
    const uint64_t id=ahbAllocationId(native);if(!id)return FL_FORMAT_UNSUPPORTED;
    Slot*target=nullptr;bool duplicate=false;
    for(auto&s:slots)if(s.reserved&&s.id==id){if(target)duplicate=true;target=&s;}
    if(!target||duplicate)return FL_FORMAT_UNSUPPORTED;
    flFrameMetadata metadata=meta->frame;
    // The frame meta carries source generation; actual buffer times may have
    // been mapped by the source. Never silently mix original and mapped DTS.
    metadata.flags=0;metadata.dtsNs=0;metadata.durationNs=0;
    if(GST_BUFFER_DTS_IS_VALID(input)){if(GST_BUFFER_DTS(input)>G_MAXINT64)return FL_INVALID;metadata.flags|=FL_FRAME_HAS_DTS;metadata.dtsNs=GST_BUFFER_DTS(input);}
    if(GST_BUFFER_DURATION_IS_VALID(input)){metadata.flags|=FL_FRAME_HAS_DURATION;metadata.durationNs=GST_BUFFER_DURATION(input);}
    if(GST_BUFFER_FLAG_IS_SET(input,GST_BUFFER_FLAG_DISCONT))metadata.flags|=FL_FRAME_DISCONT;
    r=flSetFrameMetadata(down,&target->buffer,&metadata);if(r!=FL_OK)return r;
    target->memory=gst_memory_ref(m);++inFlight;target->reserved=false;
    r=flSubmit(down,&target->buffer,GST_BUFFER_PTS(input));if(r!=FL_OK){failed=true;return r;}
    ++forwarded;return FL_OK;
  }
  flResult finish(){
    auto r=collect();if(r!=FL_OK)return r;if(inFlight)return FL_BUSY;
    for(auto&s:slots)if(s.reserved){r=flCancelBuffer(down,&s.buffer);if(r!=FL_OK)return r;s.reserved=false;}
    initialized=false;ready=false;return FL_OK;
  }
  unsigned pending()const{return inFlight;}
  uint64_t count()const{return forwarded;}
 private:
  struct Slot{flBuffer buffer{};GstMemory*memory=nullptr;bool reserved=false;uint64_t id=0;};
  flChannel*down;uint64_t mediaGeneration;flChannelInfo info{};std::array<Slot,16>slots{};
  bool attempted=false,initialized=false,ready=false,failed=false;unsigned inFlight=0;uint64_t forwarded=0;
};
}

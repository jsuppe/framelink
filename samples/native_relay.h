#pragma once
// Read-only same-allocation relay building block. Borrowed channels; one owner.
// The trusted setup must supply immutable pools referring to the SAME allocations
// and an exact native-identity matcher (never dimensions or handle numbers alone).
// No writer other than the original producer may modify shared payloads.
// Initialize only after BOTH peer handshakes complete and memory is agreed;
// first producer attachment starts a new generation on supported backends.
#include "framelink.h"
#include <array>
#include <functional>
#include <utility>
namespace flsample {
class NativeRelay {
 public:
  using SameAllocation=std::function<bool(flImage*,flImage*)>;
  NativeRelay(flChannel* upstreamConsumer,flChannel* downstreamProducer,SameAllocation same)
      :up(upstreamConsumer),down(downstreamProducer),sameAllocation(std::move(same)){}
  NativeRelay(const NativeRelay&)=delete;
  NativeRelay&operator=(const NativeRelay&)=delete;
  flResult initialize(){
    if(initialized||!up||!down||!sameAllocation)return FL_INVALID;
    uint32_t flags=0;if(flQueryConsumerFlags(up,&flags)!=FL_OK||!(flags&FL_DEFER_RELEASE)||(flags&FL_GPU_SYNC))return FL_INVALID;
    if(!(flags&FL_REQUIRE_FRAME_METADATA))return FL_FORMAT_UNSUPPORTED;
    if(flQuery(up,&upInfo)!=FL_OK||flQuery(down,&downInfo)!=FL_OK)return FL_INVALID;
    if(upInfo.width!=downInfo.width||upInfo.height!=downInfo.height||upInfo.format!=downInfo.format||
       !downInfo.poolSize||downInfo.poolSize>slots.size()||upInfo.poolSize!=downInfo.poolSize)return FL_FORMAT_UNSUPPORTED;
    initialized=true;auto r=collect();if(r!=FL_OK)return r;
    for(unsigned n=0;n<downInfo.poolSize;++n)if(!slots[n].reserved){
      for(auto&slot:slots)if(slot.reserved){flCancelBuffer(down,&slot.buffer);slot.reserved=false;}
      initialized=false;return FL_BUSY; // only a fresh, exclusively owned downstream ring
    }
    return FL_OK;
  }
  // A reservation becoming reusable is proof that the downstream reader's
  // consumed fence/lease completed. Only THEN retire the upstream read lease.
  flResult collect(){
    if(!initialized||failed)return FL_INVALID;
    flChannelInfo a{},b{};
    if(flQuery(up,&a)!=FL_OK||flQuery(down,&b)!=FL_OK||a.generation!=upInfo.generation||b.generation!=downInfo.generation){failed=true;return FL_DISCONNECTED;}
    for(unsigned n=0;n<downInfo.poolSize;++n){
      flBuffer buffer{};auto r=flAcquireBuffer(down,&buffer,0);
      if(r==FL_TIMEOUT)return FL_OK;
      if(r!=FL_OK){failed=true;return r;}
      if(buffer.slot>=downInfo.poolSize||slots[buffer.slot].reserved){failed=true;return FL_INVALID;}
      auto&slot=slots[buffer.slot];slot.buffer=buffer;slot.reserved=true;
      if(slot.lease){
        r=flCompleteReadLease(up,slot.lease);if(r!=FL_OK){failed=true;return r;}
        slot.lease=0;--inFlight;
        r=flCollectReadLeases(up);if(r!=FL_OK&&r!=FL_BUSY){failed=true;return r;}
      }
    }
    return FL_OK;
  }
  // On an identity/metadata rejection the input frame remains held.
  // After leasing/submission failure the relay is poisoned and retains ownership:
  // stop the whole pipeline and drain GPU work before destroying/reusing pools.
  flResult forward(const flFrame&frame){
    auto r=collect();if(r!=FL_OK)return r;
    Slot*target=nullptr;
    for(auto&slot:slots)if(slot.reserved&&sameAllocation(frame.image,slot.buffer.image)){if(target)return FL_INVALID;target=&slot;}
    if(!target)return FL_FORMAT_UNSUPPORTED; // unknown allocation: never copy/fabricate identity
    flFrameMetadata metadata{};r=flGetFrameMetadata(up,&frame,&metadata);
    if(r==FL_OK){r=flSetFrameMetadata(down,&target->buffer,&metadata);if(r!=FL_OK)return r;}
    else return r;
    uint64_t lease=0;r=flLeaseFrame(up,&frame,&lease);if(r!=FL_OK)return r;
    target->lease=lease;++inFlight;target->reserved=false;
    r=flSubmit(down,&target->buffer,frame.ptsNs);
    if(r!=FL_OK){failed=true;return r;}
    ++forwarded;return FL_OK;
  }
  // Never abandons in-flight upstream leases, including on downstream death.
  flResult finish(){
    auto r=collect();if(r!=FL_OK)return r;if(inFlight)return FL_BUSY;
    for(auto&slot:slots)if(slot.reserved){r=flCancelBuffer(down,&slot.buffer);if(r!=FL_OK)return r;slot.reserved=false;}
    initialized=false;return FL_OK;
  }
  unsigned pending()const{return inFlight;}
  uint64_t count()const{return forwarded;}
 private:
  struct Slot{flBuffer buffer{};uint64_t lease=0;bool reserved=false;};
  flChannel*up;flChannel*down;SameAllocation sameAllocation;
  flChannelInfo upInfo{},downInfo{};std::array<Slot,16>slots{};
  bool initialized=false,failed=false;unsigned inFlight=0;uint64_t forwarded=0;
};
}

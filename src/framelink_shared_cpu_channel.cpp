#define FL_EXPORTS
#include "framelink_shared_cpu_channel.h"
#include "fl_shared_cpu_control.h"
#include <limits>
namespace {
struct State {
 bool producer=false,alive=true,eosEnabled=false;
 size_t count=0;
 std::unique_ptr<fl::SharedCpuControl> control;
 fl::SharedCpuImportedPool imported;
 std::array<fl::SharedCpuAllocation,FL_MAX_POOL_SIZE> owners;
 std::array<bool,FL_MAX_POOL_SIZE> reserved{};
 void close(){alive=false;control.reset();imported=fl::SharedCpuImportedPool{};for(auto&o:owners)o.reset();}
 bool live()const{return alive&&control&&control->poolAccepted();}
};
struct Fds {std::array<int,FL_MAX_POOL_SIZE> values;Fds(){values.fill(-1);}~Fds(){for(auto fd:values)if(fd>=0)::close(fd);}};
}
struct flSharedCpuChannel {std::shared_ptr<State> state;};
struct flSharedCpuLease {
 std::shared_ptr<State> state;
 fl::SharedCpuAllocation::Mapping mapping;
 fl::SharedCpuControl::Frame frame;
 size_t refs=1;
 bool write=false;
};
extern "C" {
flResult flSharedCpuOpenConsumerFdEx(int fd,uint32_t timeout,const flSessionConfig*s,const flMemoryRequirements*r,uint32_t ops,const flSharedCpuDescriptor*d,size_t count,uint32_t flags,flSharedCpuChannel**out){
 // Adopt first: even invalid caller parameters must consume the delegated FD.
 auto link=fl::Channel::adoptConnectedFd(fd,timeout);
 if(!link.connected()||!s||!r||!d||!out||count<2||count>FL_MAX_POOL_SIZE||(flags&~(FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS)))return FL_INVALID;
 flMemoryRequirements preflight[2]{};flMemoryRequirementsInit(&preflight[0]);preflight[0].allowedOperations=FL_MEMORY_COPY;preflight[1]=*r;
 uint32_t preOps[2]={0,ops};flMemoryDecision decisions[2]{};
 if(fl::validateSharedCpuPool(s,d,count,preflight,preOps,decisions)!=FL_MEMORY_OK)return FL_INVALID;
 try{
  auto state=std::make_shared<State>();state->count=count;state->eosEnabled=flags&FL_SHARED_CPU_ORDERED_EOS;
  state->control=std::make_unique<fl::SharedCpuControl>(std::move(link));
  auto result=state->control->negotiate(false,*s,*r,ops,flags&FL_SHARED_CPU_FRAME_METADATA,flags&FL_SHARED_CPU_ORDERED_EOS);if(result!=FL_OK)return result;
  Fds fds;
  for(size_t i=0;i<count;i++){result=fl::SharedCpuAllocation::create(d[i],state->owners[i]);if(result!=FL_OK)return result;
   result=state->owners[i].exportFd(true,fds.values[i]);if(result!=FL_OK)return result;}
  result=state->control->sendPool(d,fds.values.data(),count);if(result!=FL_OK)return result;
  auto channel=std::make_unique<flSharedCpuChannel>();channel->state=std::move(state);*out=channel.release();return FL_OK;
 }catch(const std::bad_alloc&){return FL_OOM;}
}
flResult flSharedCpuOpenProducerFdEx(int fd,uint32_t timeout,const flSessionConfig*s,const flMemoryRequirements*r,uint32_t ops,uint32_t flags,flSharedCpuChannel**out){
 auto link=fl::Channel::adoptConnectedFd(fd,timeout);if(!link.connected()||!s||!r||!out||(flags&~(FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS)))return FL_INVALID;
 try{
  auto state=std::make_shared<State>();state->producer=true;state->eosEnabled=flags&FL_SHARED_CPU_ORDERED_EOS;state->control=std::make_unique<fl::SharedCpuControl>(std::move(link));
  auto result=state->control->negotiate(true,*s,*r,ops,flags&FL_SHARED_CPU_FRAME_METADATA,flags&FL_SHARED_CPU_ORDERED_EOS);if(result!=FL_OK)return result;
  result=state->control->receivePool(state->imported);if(result!=FL_OK)return result;state->count=state->imported.size();
  auto channel=std::make_unique<flSharedCpuChannel>();channel->state=std::move(state);*out=channel.release();return FL_OK;
 }catch(const std::bad_alloc&){return FL_OOM;}
}
flResult flSharedCpuOpenConsumerFd(int fd,uint32_t timeout,const flSessionConfig*s,const flMemoryRequirements*r,uint32_t ops,const flSharedCpuDescriptor*d,size_t count,flSharedCpuChannel**out){return flSharedCpuOpenConsumerFdEx(fd,timeout,s,r,ops,d,count,0,out);}
flResult flSharedCpuOpenProducerFd(int fd,uint32_t timeout,const flSessionConfig*s,const flMemoryRequirements*r,uint32_t ops,flSharedCpuChannel**out){return flSharedCpuOpenProducerFdEx(fd,timeout,s,r,ops,0,out);}
void flSharedCpuClose(flSharedCpuChannel*c){if(!c)return;c->state->close();delete c;}
flResult flSharedCpuAcquireWrite(flSharedCpuChannel*c,uint32_t slot,flSharedCpuLease**out,flMappedImage*view){
 if(!c||!out||!view||!c->state->producer||slot>=c->state->count)return FL_INVALID;
 auto s=c->state;if(!s->live())return FL_DISCONNECTED;
 if(s->reserved[slot]||!s->control->writableSlot(slot))return FL_BUSY;
 try{
  auto lease=std::make_unique<flSharedCpuLease>();lease->state=s;lease->write=true;lease->frame.slot=slot;
  auto r=s->imported.map(slot,lease->mapping);if(r!=FL_OK)return r;
  s->reserved[slot]=true;*view=lease->mapping.view();*out=lease.release();return FL_OK;
 }catch(const std::bad_alloc&){return FL_OOM;}
}
flResult flSharedCpuSubmitWithMetadata(flSharedCpuLease*l,uint64_t sequence,int64_t pts,const flFrameMetadata*metadata){
 if(!l||!l->write)return FL_INVALID;
 std::unique_ptr<flSharedCpuLease> lease(l);auto s=l->state;auto slot=l->frame.slot;
 l->mapping.reset();s->reserved[slot]=false;if(!s->live())return FL_DISCONNECTED;
 fl::SharedCpuControl::Frame frame{sequence,pts,slot};if(metadata)frame.metadata=*metadata;
 return s->control->submit(frame);
}
flResult flSharedCpuSubmit(flSharedCpuLease*l,uint64_t sequence,int64_t pts){return flSharedCpuSubmitWithMetadata(l,sequence,pts,nullptr);}
flResult flSharedCpuReadMetadata(const flSharedCpuLease*l,flFrameMetadata*out){if(!l||l->write||!out)return FL_INVALID;*out=l->frame.metadata;return FL_OK;}
flResult flSharedCpuCancelWrite(flSharedCpuLease*l){
 if(!l||!l->write)return FL_INVALID;
 auto s=l->state;s->reserved[l->frame.slot]=false;delete l;return s->live()?FL_OK:FL_DISCONNECTED;
}
flResult flSharedCpuReceiveRelease(flSharedCpuChannel*c){
 if(!c||!c->state->producer)return FL_INVALID;auto s=c->state;if(!s->live())return FL_DISCONNECTED;
 try{fl::SharedCpuControl::Frame frame;return s->control->receiveRelease(frame);}catch(const std::bad_alloc&){s->close();return FL_OOM;}
}
flResult flSharedCpuSendEos(flSharedCpuChannel*c){
 if(!c||!c->state->producer)return FL_INVALID;auto s=c->state;if(!s->live())return FL_DISCONNECTED;
 for(bool reserved:s->reserved)if(reserved)return FL_BUSY;
 return s->control->sendEos();
}
flResult flSharedCpuReceive(flSharedCpuChannel*c,flSharedCpuLease**out,flMappedImage*view,uint64_t*seq,int64_t*pts){
 if(c&&c->state->eosEnabled)return FL_INVALID;
 int eos=0;return flSharedCpuReceiveEvent(c,out,view,seq,pts,&eos);
}
flResult flSharedCpuReceiveEvent(flSharedCpuChannel*c,flSharedCpuLease**out,flMappedImage*view,uint64_t*seq,int64_t*pts,int*eos){
 if(!c||c->state->producer||!out||!view||!seq||!pts||!eos)return FL_INVALID;auto s=c->state;if(!s->live())return FL_DISCONNECTED;
 try{
  auto lease=std::make_unique<flSharedCpuLease>();lease->state=s;
  bool end=false;auto r=s->control->receiveFrame(lease->frame,&end);if(r!=FL_OK)return r;
  if(end){*eos=1;return FL_OK;}
  r=s->owners[lease->frame.slot].map(false,lease->mapping);if(r!=FL_OK){s->close();return r;}
  *view=lease->mapping.view();*seq=lease->frame.sequence;*pts=lease->frame.ptsNs;*out=lease.release();*eos=0;return FL_OK;
 }catch(const std::bad_alloc&){s->close();return FL_OOM;}
}
flResult flSharedCpuRetainRead(flSharedCpuLease*l){
 if(!l||l->write||l->refs==std::numeric_limits<size_t>::max())return FL_INVALID;
 ++l->refs;return FL_OK;
}
flResult flSharedCpuReleaseRead(flSharedCpuLease*l){
 if(!l||l->write)return FL_INVALID;
 if(--l->refs)return l->state->live()?FL_OK:FL_DISCONNECTED;
 std::unique_ptr<flSharedCpuLease> lease(l);auto s=l->state;auto f=l->frame;
 l->mapping.reset();if(!s->live())return FL_DISCONNECTED;
 return s->control->release(f.slot,f.sequence);
}
}

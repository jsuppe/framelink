#pragma once
// Internal explicit native relay bootstrap. No discovery/default/API changes.
// The caller supplies an OVERLAPPED pipe and pinned kernel peer; this function
// authenticates THAT pipe under a distinct domain before reading any handles.
#include "fl_gst_cpu_pipe_auth_win.h"
#include "framelink_d3d11.h"

namespace flnative {
struct Handle {
 HANDLE value=nullptr;
 ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
// Consumes input on every path, borrows peer/cancel/key. Producer exports are
// borrowed from a pinned, unpublished upstream pool until this function returns.
// Receiver PULLS duplicates from the pinned producer; wire integers are never
// closed or interpreted as local handles. Both sides must agree on config,
// capacity, CPU access and secret-derived channel name. Output owned on success.
// Native D3D allocation and legacy flOpenProducer remain synchronous; all pipe
// I/O uses the remaining whole-call budget. Caller serializes pool/session use.
// This only creates the ring: caller MUST install the read-lease relay and
// cancellation supervision before any source writes. No independent writers.
inline flResult transferRelayPool(HANDLE input,HANDLE peer,HANDLE cancel,guint timeout,
 const flSessionConfig&config,bool producer,GBytes*key,const char*name,unsigned count,
 bool cpuAccess,const void*adapter,void*const*exports,flChannel**out){
 Handle owned;owned.value=input;if(out)*out=nullptr;
 if(!out||!name||!*name||strlen(name)>240||count<2||count>FL_MAX_POOL_SIZE||
    !timeout||timeout>60000||!cancel||!config.videoTrackId||
    flSessionValidate(&config)!=FL_SESSION_OK||(producer&&(!adapter||!exports))||
    (!producer&&(adapter||exports)))return FL_INVALID;
 const auto end=GetTickCount64()+timeout;
 auto remaining=[&]()->guint{auto now=GetTickCount64();return now<end?guint(end-now):0;};
 auto result=flAuthenticatePipeProfile(input,peer,cancel,timeout,config,producer,key,
     "FrameLink authenticated Windows native relay pool v1");
 if(result!=FL_OK)return result;
 fl::SharedCpuPipeWin pipe;
 auto ms=remaining();if(!ms)return FL_TIMEOUT;
 owned.value=nullptr;result=fl::SharedCpuPipeWin::adoptConnected(input,peer,cancel,ms,pipe);
 if(result!=FL_OK)return result;
 // Dedicated private message types; never accepted by the CPU pool protocol.
 constexpr auto offerType=static_cast<fl::MsgType>(0x4e520001u);
 constexpr auto readyType=static_cast<fl::MsgType>(0x4e520002u);
 constexpr auto attachedType=static_cast<fl::MsgType>(0x4e520003u);
 std::array<guint8,56+8*FL_MAX_POOL_SIZE> packet{};
 memcpy(packet.data(),"FLNRP\0\0\1",8);
 GST_WRITE_UINT32_LE(packet.data()+8,count);GST_WRITE_UINT32_LE(packet.data()+12,cpuAccess?1:0);
 auto*hash=g_checksum_new(G_CHECKSUM_SHA256);
 g_checksum_update(hash,reinterpret_cast<const guchar*>(name),strlen(name)+1);
 gsize hashSize=32;g_checksum_get_digest(hash,packet.data()+24,&hashSize);g_checksum_free(hash);
 auto send=[&](fl::MsgType type,const void*data,gsize size){
  auto budget=remaining();if(!budget||!pipe.setIoTimeout(budget))return FL_TIMEOUT;
  return pipe.send(type,data,size);
 };
 auto receive=[&](fl::Message&message){
  auto budget=remaining();if(!budget||!pipe.setIoTimeout(budget))return FL_TIMEOUT;
  return pipe.receive(message);
 };
 struct Channel {flChannel*value=nullptr;~Channel(){if(value)flClose(value);}} channel;
 auto validateRing=[&]()->flResult{
  flChannelInfo info{};auto r=flQuery(channel.value,&info);if(r!=FL_OK)return r;
  // Backend device creation may otherwise select its default when an explicit
  // LUID is unavailable. A relay must reject that substitution before ACK.
  return info.width==config.video.width&&info.height==config.video.height&&
   info.format==config.video.format&&info.poolSize==count&&
   !memcmp(info.adapterLUID,packet.data()+16,8)?FL_OK:FL_FORMAT_UNSUPPORTED;
 };
 fl::Message message;
 if(producer){
  memcpy(packet.data()+16,adapter,8);
  for(unsigned n=0;n<count;++n){
   const auto handle=reinterpret_cast<uintptr_t>(exports[n]);
   if(!handle||handle>uintptr_t(INTPTR_MAX))return FL_INVALID;
   GST_WRITE_UINT64_LE(packet.data()+56+8*n,handle);
  }
  result=send(offerType,packet.data(),packet.size());if(result!=FL_OK)return result;
  result=receive(message);if(result!=FL_OK)return result;
  if(message.type!=readyType||message.payload.size()!=packet.size()||
     memcmp(message.payload.data(),packet.data(),packet.size()))return FL_INVALID;
  if(!remaining())return FL_TIMEOUT;
  if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return FL_DISCONNECTED;
  result=flOpenProducer(name,&channel.value);if(result!=FL_OK)return result;
  result=validateRing();if(result!=FL_OK)return result;
  result=send(attachedType,packet.data(),packet.size());if(result!=FL_OK)return result;
 }else{
  result=receive(message);if(result!=FL_OK)return result;
  if(message.type!=offerType||message.payload.size()!=packet.size()||
     memcmp(message.payload.data(),packet.data(),16)||
     memcmp(message.payload.data()+24,packet.data()+24,32))return FL_INVALID;
  memcpy(packet.data(),message.payload.data(),packet.size());
  for(unsigned n=count;n<FL_MAX_POOL_SIZE;++n)
   if(GST_READ_UINT64_LE(packet.data()+56+8*n))return FL_INVALID;
  std::array<Handle,FL_MAX_POOL_SIZE> handles;
  std::array<void*,FL_MAX_POOL_SIZE> imports{};
  using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
  auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));
  if(!compare)return FL_FORMAT_UNSUPPORTED;
  for(unsigned n=0;n<count;++n){
   const auto value=GST_READ_UINT64_LE(packet.data()+56+8*n);
   if(!value||value>uint64_t(INTPTR_MAX))return FL_INVALID;
   if(!remaining())return FL_TIMEOUT;
   if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT||WaitForSingleObject(peer,0)!=WAIT_TIMEOUT)return FL_DISCONNECTED;
   if(!DuplicateHandle(peer,reinterpret_cast<HANDLE>(uintptr_t(value)),GetCurrentProcess(),
       &handles[n].value,0,FALSE,DUPLICATE_SAME_ACCESS))return FL_ACCESS_DENIED;
   for(unsigned previous=0;previous<n;++previous)
    if(compare(handles[n].value,handles[previous].value))return FL_INVALID;
   imports[n]=handles[n].value;
  }
  result=flCreateChannelFromD3D11Handles(name,config.video.width,config.video.height,
   static_cast<flFormat>(config.video.format),count,
   FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(cpuAccess?FL_MAP_CPU:0),
   packet.data()+16,imports.data(),&channel.value);
  if(result!=FL_OK)return result;
  result=validateRing();if(result!=FL_OK)return result;
  result=send(readyType,packet.data(),packet.size());if(result!=FL_OK)return result;
  result=receive(message);if(result!=FL_OK)return result;
  if(message.type!=attachedType||message.payload.size()!=packet.size()||
     memcmp(message.payload.data(),packet.data(),packet.size()))return FL_INVALID;
 }
 if(!remaining())return FL_TIMEOUT;
 if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT||WaitForSingleObject(peer,0)!=WAIT_TIMEOUT)return FL_DISCONNECTED;
 *out=channel.value;channel.value=nullptr;return FL_OK;
}
}

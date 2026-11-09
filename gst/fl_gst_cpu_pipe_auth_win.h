#pragma once
// Internal authenticated profile: the proof travels on the same connected pipe
// later used for pool exchange. A separate authenticated STREAM is insufficient.
#include "../src/fl_shared_cpu_pipe_win.h"
#include "../runtime/av_auth.h"
#include <array>
#include <algorithm>

inline flResult flAuthenticateCpuPipe(HANDLE input,HANDLE peer,HANDLE cancel,
 guint timeout,const flSessionConfig& config,bool producer,GBytes* secret){
 gsize size=0;const auto* key=secret?static_cast<const guint8*>(g_bytes_get_data(secret,&size)):nullptr;
 if(!key||size!=32)return FL_INVALID;
 guint nonzero=0;for(gsize i=0;i<size;i++)nonzero|=key[i];if(!nonzero)return FL_INVALID;
 std::array<guint8,FL_SESSION_WIRE_SIZE> wire{};
 if(flSessionEncode(&config,wire.data(),wire.size())!=FL_SESSION_OK)return FL_INVALID;
 HANDLE duplicate=nullptr;
 if(!DuplicateHandle(GetCurrentProcess(),input,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS))return FL_ACCESS_DENIED;
 fl::SharedCpuPipeWin pipe;
 auto result=fl::SharedCpuPipeWin::adoptConnected(duplicate,peer,cancel,timeout,pipe);
 if(result!=FL_OK)return result;
 const auto end=GetTickCount64()+timeout;
 auto budget=[&]{auto now=GetTickCount64();return now<end&&pipe.setIoTimeout(uint32_t(end-now));};
 std::array<guint8,32> own{},other{};std::string reason;
 if(!av::secureRandom(own.data(),own.size(),reason))return FL_INTERNAL;
 if(!budget())return FL_TIMEOUT;
 result=pipe.send(fl::MsgType::SharedCpuAuthNonce,own.data(),own.size());if(result!=FL_OK)return result;
 fl::Message message;
 if(!budget())return FL_TIMEOUT;
 result=pipe.receive(message);if(result!=FL_OK)return result;
 if(message.type!=fl::MsgType::SharedCpuAuthNonce||message.payload.size()!=other.size())return FL_INVALID;
 memcpy(other.data(),message.payload.data(),other.size());
 const auto& pn=producer?own:other;const auto& cn=producer?other:own;
 // PID equality alone is never authorization. The already pinned process
 // handle is checked against the kernel pipe peer by adoptConnected; bind its
 // identity into the authenticated transcript to reject cross-process relays.
 const DWORD localPid=GetCurrentProcessId(),peerPid=GetProcessId(peer);
 guint8 identities[8]{};GST_WRITE_UINT32_LE(identities,producer?localPid:peerPid);
 GST_WRITE_UINT32_LE(identities+4,producer?peerPid:localPid);
 auto proof=[&](guint8 direction){
  auto*h=g_hmac_new(G_CHECKSUM_SHA256,key,size);
  constexpr char label[]="FrameLink authenticated Windows CPU pipe v1";
  g_hmac_update(h,reinterpret_cast<const guint8*>(label),sizeof label);
  g_hmac_update(h,&direction,1);g_hmac_update(h,wire.data(),wire.size());
  g_hmac_update(h,identities,sizeof identities);g_hmac_update(h,pn.data(),pn.size());g_hmac_update(h,cn.data(),cn.size());
  std::array<guint8,32> digest{};gsize n=digest.size();g_hmac_get_digest(h,digest.data(),&n);g_hmac_unref(h);return digest;
 };
 auto ownProof=proof(producer?'P':'C');auto expected=proof(producer?'C':'P');
 if(!budget())return FL_TIMEOUT;
 result=pipe.send(fl::MsgType::SharedCpuAuthProof,ownProof.data(),ownProof.size());if(result!=FL_OK)return result;
 if(!budget())return FL_TIMEOUT;
 result=pipe.receive(message);if(result!=FL_OK)return result;
 if(message.type!=fl::MsgType::SharedCpuAuthProof||message.payload.size()!=expected.size())return FL_INVALID;
 guint delta=0;for(gsize i=0;i<expected.size();i++)delta|=expected[i]^message.payload[i];
 if(delta)return FL_ACCESS_DENIED;
 if(GetTickCount64()>=end)return FL_TIMEOUT;
 if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT||WaitForSingleObject(peer,0)!=WAIT_TIMEOUT)return FL_DISCONNECTED;
 return FL_OK;
}

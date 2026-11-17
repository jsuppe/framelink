#pragma once
#include "native_d3d11_relay_pool.h"
#include "../gst/fl_gst_native_relay_pool_win.h"
#include "../src/fl_shared_cpu_endpoint_win.h"

namespace flsample {
// Explicit broker-supplied session/key/name. Not yet automatic discovery.
// Current-user-only named pipe with pinned peer and distinct native HMAC domain.
// Source pool must stay alive and serialized after this call; the read-lease
// adapter, not this setup helper, owns per-frame upstream completion. Each side
// receives one owned channel. No source writes until both adapters are bound.
inline flResult createAuthenticatedNativeD3D11RelayPool(bool producer,GstBufferPool*pool,
 const std::wstring&pipeName,const char*ringName,const flSessionConfig&config,
 unsigned capacity,bool cpuAccess,GBytes*secret,HANDLE cancel,guint timeout,flChannel**out){
 if(out)*out=nullptr;
 if(!out||!cancel||!timeout||timeout>60000||(!producer&&pool))return FL_INVALID;
 const auto end=GetTickCount64()+timeout;
 auto remaining=[&]()->guint{const auto now=GetTickCount64();return now<end?guint(end-now):0;};
 NativeD3D11RelayPoolExport reservations;
 if(producer){auto result=reservations.prepare(pool,config,capacity);if(result!=FL_OK)return result;}
 flnative::Handle pipe,peer;
 auto budget=remaining();if(!budget)return FL_TIMEOUT;
 flResult result;
 if(producer)result=fl::SharedCpuEndpointWin::connect(pipeName,cancel,budget,pipe.value,peer.value);
 else{
  result=fl::SharedCpuEndpointWin::listen(pipeName,pipe.value);
  if(result==FL_OK){budget=remaining();result=budget?
   fl::SharedCpuEndpointWin::accept(pipe.value,cancel,budget,peer.value):FL_TIMEOUT;}
 }
 if(result!=FL_OK)return result;
 budget=remaining();if(!budget)return FL_TIMEOUT;
 auto input=pipe.value;pipe.value=nullptr;
 return flnative::transferRelayPool(input,peer.value,cancel,budget,config,producer,secret,ringName,
   capacity,cpuAccess,producer?reservations.first.adapterLUID:nullptr,
   producer?reservations.handles.data():nullptr,out);
}
}

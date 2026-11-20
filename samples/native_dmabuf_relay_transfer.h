#pragma once
#include "native_dmabuf_relay_pool.h"
#include "../gst/fl_gst_native_relay_pool_linux.h"
#include <string>

namespace flsample {
// The desktop Linux counterpart of native_d3d11_relay_transfer.h.
// Explicit broker-supplied session/key/address. Not automatic discovery (the
// session's FAR5 path derives the address from its own secret instead).
// SO_PEERCRED-pinned peer of the same user, DMA-BUF fds passed with SCM_RIGHTS,
// and the distinct native relay HMAC domain. The source pool must stay alive and
// serialized after this call; the read-lease adapter, not this setup helper,
// owns per-frame upstream completion. Each side receives one owned channel. No
// source writes until both adapters are bound. `address` is an abstract socket
// name (leading NUL) agreed by both sides.
inline flResult createAuthenticatedNativeDmabufRelayPool(bool producer,GstBufferPool*pool,
 const std::string&address,const char*ringName,const flSessionConfig&config,
 unsigned capacity,bool cpuAccess,GBytes*secret,GCancellable*cancel,guint timeout,flChannel**out){
 if(out)*out=nullptr;
 if(!out||!cancel||!timeout||timeout>60000||address.size()<2||address[0]||(!producer&&pool))return FL_INVALID;
 const gint64 end=g_get_monotonic_time()+gint64(timeout)*1000;
 auto remaining=[&]()->guint{const gint64 left=end-g_get_monotonic_time();return left>0?guint((left+999)/1000):0;};
 NativeDmabufRelayPoolExport reservations;
 if(producer){auto result=reservations.prepare(pool,config,capacity);if(result!=FL_OK)return result;}
 if(g_cancellable_is_cancelled(cancel))return FL_DISCONNECTED;
 int socket=-1;
 auto result=producer?flnative::connectRelayPool(address,cancel,end,&socket):flnative::acceptRelayPool(address,cancel,end,&socket);
 if(result!=FL_OK)return result;
 const auto budget=remaining();if(!budget){close(socket);return FL_TIMEOUT;}
 return flnative::transferRelayPool(socket,cancel,budget,config,producer,secret,ringName,
   capacity,cpuAccess,producer?reservations.images.data():nullptr,out);
}
}

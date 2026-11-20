#pragma once
#include "../gst/framelink_session_transport.h"
#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
#ifdef _WIN32
#include "framelink_d3d11.h"
#else
#include "framelink_dmabuf.h"
#endif
#include <array>
#include <cstring>

namespace flsample {
// The platform's consumer-pool snapshot: D3D11 shared handles on Windows,
// DMA-BUF fds on desktop Linux. Same pinning contract on both.
#ifdef _WIN32
using LocalPoolSnapshot=flD3D11PoolSnapshot;
inline flResult snapshotLocalPool(flChannel*c,LocalPoolSnapshot**s){return flSnapshotD3D11ConsumerPool(c,s);}
inline flResult queryLocalPoolSnapshot(const LocalPoolSnapshot*s,flChannelInfo*i){return flQueryD3D11PoolSnapshot(s,i);}
inline void releaseLocalPoolSnapshot(LocalPoolSnapshot*s){flReleaseD3D11PoolSnapshot(s);}
#else
using LocalPoolSnapshot=flDmabufPoolSnapshot;
inline flResult snapshotLocalPool(flChannel*c,LocalPoolSnapshot**s){return flSnapshotDmabufConsumerPool(c,s);}
inline flResult queryLocalPoolSnapshot(const LocalPoolSnapshot*s,flChannelInfo*i){return flQueryDmabufPoolSnapshot(s,i);}
inline void releaseLocalPoolSnapshot(LocalPoolSnapshot*s){flReleaseDmabufPoolSnapshot(s);}
#endif
// Explicit ownership transfer, deliberately NOT a running-graph RAII owner.
// The caller must stop writers and retire actual downstream readers before
// closing channels. Keep snapshot for the ENTIRE graph lifetime: duplicate
// texture handles or fds alone do not pin the original source pool/epoch.
struct LocalNativeRelaySetup {
  flChannel* input=nullptr;
  flChannel* output=nullptr;
  LocalPoolSnapshot* snapshot=nullptr;
  // Both policies are producer-ordered. Swap endpoints for receiver binding.
  GstFrameLinkMemoryPolicy inputPolicy{},outputPolicy{};
};

// Worker-thread pre-publication transaction only, not a media relay or broker.
// Borrow fresh, distinct, unconfigured sessions and arguments for the complete
// call; serialize their configuration/close against this call. out must not own
// a prior graph (nonempty resource pointers reject unchanged; an empty output
// is cleared, never implicitly destroyed). The source MUST
// obey local_ready and remain unpublished until the caller finishes setup.
// No local_ready is sent here: install native/PCM/event adapters and supervision,
// wait for downstream readiness, then release the upstream producer explicitly.
// Success transfers both channels and snapshot to out atomically. Failure
// cancels both sessions and destroys only this unpublished transaction's objects.
// One total monotonic setup-I/O budget spans both sessions and snapshot work.
// Native driver calls remain synchronous/noninterruptible and can exceed that
// budget; observed expiry/cancellation prevents publication of successful outputs.
// No fallback, new wire protocol, helper-owned worker or changed API defaults.
inline gboolean setupLocalNativeRelay(FlGstSession* up,FlGstSession* down,
 const gchar* inputPath,const gchar* outputPath,const flSessionCapabilities* caps,
 const GstFrameLinkMemoryPolicy* candidate,guint videoQueue,guint videoPool,
 guint audioQueueMs,guint timeoutMs,guint idleTimeoutMs,GCancellable* cancel,
 LocalNativeRelaySetup* out,GError** error){
  const gint64 began=g_get_monotonic_time();
  struct Pending {
    LocalNativeRelaySetup value;
    ~Pending(){
      if(value.output)flClose(value.output);
      if(value.input)flClose(value.input);
      if(value.snapshot)releaseLocalPoolSnapshot(value.snapshot);
    }
  } pending;
  auto fail=[&](GIOErrorEnum code,const char* message)->gboolean{
    if(up)fl_gst_session_cancel(up);
    if(down&&down!=up)fl_gst_session_cancel(down);
    if(!error||!*error)g_set_error_literal(error,G_IO_ERROR,code,message);
    return FALSE;
  };
  if(out&&(out->input||out->output||out->snapshot))
    return fail(G_IO_ERROR_INVALID_ARGUMENT,"relay output still owns a prior graph");
  if(out)*out={};
  flSessionConfig config{};
  if(!up||!down||up==down||!out||!inputPath||!*inputPath||!outputPath||!*outputPath||
     !std::strcmp(inputPath,outputPath)||!caps||!candidate||
     !caps->videoCount||!caps->audioCount||!videoQueue||videoQueue>16||
     videoPool<2||videoPool>FL_MAX_POOL_SIZE||
     !audioQueueMs||audioQueueMs>2000||!timeoutMs||timeoutMs>60000||
     !idleTimeoutMs||idleTimeoutMs>60000||
     fl_gst_session_get_config(up,&config)!=FL_GST_SESSION_STALE||
     fl_gst_session_get_config(down,&config)!=FL_GST_SESSION_STALE)
    return fail(G_IO_ERROR_INVALID_ARGUMENT,"fresh distinct A/V sessions, paths and valid explicit relay budgets required");
  const gint64 deadline=began+gint64(timeoutMs)*1000;
  // Round down: never grant either nested call a new or extended deadline.
  auto remaining=[&]()->guint{
    const gint64 left=deadline-g_get_monotonic_time();
    return left>=1000?guint(left/1000):0;
  };
  auto check=[&]()->gboolean{
    if(cancel&&g_cancellable_is_cancelled(cancel))return fail(G_IO_ERROR_CANCELLED,"local native relay setup cancelled");
    if(!remaining())return fail(G_IO_ERROR_TIMED_OUT,"local native relay setup deadline expired");
    return TRUE;
  };
  if(!check())return FALSE;
  auto& value=pending.value;
  if(!fl_gst_session_open_local_native_relay_input(up,inputPath,caps,videoQueue,videoPool,
      audioQueueMs,remaining(),idleTimeoutMs,candidate,&value.inputPolicy,&value.input,cancel,error))
    return fail(G_IO_ERROR_FAILED,"local native relay input setup failed");
  if(!check())return FALSE;
  if(snapshotLocalPool(value.input,&value.snapshot)!=FL_OK)
    return fail(G_IO_ERROR_FAILED,"local native relay consumer snapshot failed");
  flChannelInfo info{};
  if(queryLocalPoolSnapshot(value.snapshot,&info)!=FL_OK||info.poolSize!=videoPool)
    return fail(G_IO_ERROR_INVALID_DATA,"local native relay snapshot capacity mismatch");
  // Deliberately do not preserve slot order: forwarding must match the actual
  // allocation identity, never confuse input and output ring slot indices.
#ifdef _WIN32
  std::array<void*,FL_MAX_POOL_SIZE> handles{};
  for(guint n=0;n<videoPool;++n){
    handles[videoPool-1-n]=flD3D11PoolSnapshotHandle(value.snapshot,n);
    if(!handles[videoPool-1-n])return fail(G_IO_ERROR_INVALID_DATA,"local native relay snapshot contains no handle");
  }
  if(!check())return FALSE;
  if(!fl_gst_session_open_local_native_relay_pool(down,up,outputPath,caps,videoQueue,videoPool,
      audioQueueMs,remaining(),idleTimeoutMs,candidate,info.adapterLUID,handles.data(),
      &value.outputPolicy,&value.output,cancel,error))
    return fail(G_IO_ERROR_FAILED,"local native relay output setup failed");
#else
  std::array<flDmabuf,FL_MAX_POOL_SIZE> images{};
  for(guint n=0;n<videoPool;++n)
    if(flDmabufPoolSnapshotImage(value.snapshot,n,&images[videoPool-1-n])!=FL_OK)
      return fail(G_IO_ERROR_INVALID_DATA,"local native relay snapshot contains no image");
  if(!check())return FALSE;
  if(!fl_gst_session_open_local_native_relay_pool_dmabuf(down,up,outputPath,caps,videoQueue,videoPool,
      audioQueueMs,remaining(),idleTimeoutMs,candidate,images.data(),
      &value.outputPolicy,&value.output,cancel,error))
    return fail(G_IO_ERROR_FAILED,"local native relay output setup failed");
#endif
  if(!check())return FALSE;
  *out=value;
  value={};
  return TRUE;
}
}
#endif

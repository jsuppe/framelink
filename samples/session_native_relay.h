#pragma once
#include "native_relay.h"
#include "session_relay_contract.h"
#include "../gst/framelink_session_transport.h"

namespace flsample {
// Checked VIDEO leg of a broker-composed coordinated relay. All objects are
// borrowed; the owner serializes configuration/publication and owns PCM.
// This does not authenticate peers or infer PCM terminal accounting.
// No hidden copies. Preflight/metadata rejection leaves input with the caller.
// Native submission failure may already own a lease (see NativeRelay); never
// release it speculatively. Every error permanently stops publication, while
// collect/finish still permit safe native drain.
class SessionNativeRelay {
 public:
  SessionNativeRelay(FlGstSession* upstreamSession,FlGstSession* downstreamSession,
                     flChannel* upstream,flChannel* downstream,NativeRelay::SameAllocation same)
      :session(upstreamSession),destination(downstreamSession),up(upstream),contract(upstreamSession,downstreamSession),
       native(upstream,downstream,std::move(same)){}
  SessionNativeRelay(const SessionNativeRelay&)=delete;
  SessionNativeRelay& operator=(const SessionNativeRelay&)=delete;
  flResult initialize(){
    if(attempted)return FL_INVALID;
    attempted=true;status=contract.initialize();
    if(status!=FL_GST_SESSION_READY)return stop(FL_INVALID);
    flSessionConfig config{};status=fl_gst_session_get_config(session,&config);
    if(status!=FL_GST_SESSION_READY)return stop(FL_INVALID);
    flChannelInfo info{};auto r=flQuery(up,&info);
    if(r!=FL_OK)return stop(r);
    if(info.width!=config.video.width||info.height!=config.video.height||info.format!=config.video.format)
      return stop(FL_FORMAT_UNSUPPORTED);
    generation=config.generation;r=native.initialize();
    if(r!=FL_OK)return stop(r);
    initialized=true;return FL_OK;
  }
  flResult forward(const flFrame&frame){
    if(!initialized||stopped)return FL_INVALID;
    status=contract.check();
    if(status!=FL_GST_SESSION_READY)return stop(FL_DISCONNECTED);
    flFrameMetadata metadata{};auto r=flGetFrameMetadata(up,&frame,&metadata);
    if(r!=FL_OK)return stop(r);
    if(frame.ptsNs<0||metadata.version!=FL_FRAME_METADATA_VERSION||metadata.streamGeneration!=generation)
      return stop(FL_INVALID);
    r=native.forward(frame);if(r==FL_OK)lastPts=frame.ptsNs;
    return r==FL_OK?r:stop(r);
  }
  // Call at the exact event boundary, after forwarding afterFrames frames.
  // No arbitrary caller-supplied event is trusted: consume the authenticated
  // upstream stream. Timeout consumes nothing and may be retried. Any other
  // failure cancels downstream and poisons publication, without retiring leases.
  // The transport reports FLUSH as cancellation, not as a forwardable record.
  // END closes video publication only; PCM/aggregate EOS remain owner-managed.
  FlGstSessionResult forwardEvent(guint timeoutMs){
    if(!initialized||stopped)return FL_GST_SESSION_INVALID;
    auto fail=[&](FlGstSessionResult r){
      status=r;stopped=true;fl_gst_session_cancel(destination);return r;
    };
    status=contract.check();if(status!=FL_GST_SESSION_READY)return fail(status);
    FlGstVideoEvent event{};
    status=fl_gst_session_receive_video_event(session,generation,timeoutMs,&event);
    if(status==FL_GST_SESSION_TIMEOUT)return status;
    if(status!=FL_GST_SESSION_READY)return fail(status);
    status=contract.check();if(status!=FL_GST_SESSION_READY)return fail(status);
    if(event.afterFrames!=count()||
       (event.kind!=FL_GST_VIDEO_GAP&&event.kind!=FL_GST_VIDEO_BARRIER&&event.kind!=FL_GST_VIDEO_END)||
       (event.kind==FL_GST_VIDEO_END&&event.ptsNs!=(lastPts<0?0:guint64(lastPts))))
      return fail(FL_GST_SESSION_INVALID);
    status=fl_gst_session_send_video_event(destination,generation,&event);
    if(status!=FL_GST_SESSION_READY)return fail(status);
    if(event.kind==FL_GST_VIDEO_END){stopped=true;ended=true;}
    return status;
  }
  // Session cancellation must NOT force-complete a downstream reader's lease.
  // Drain ignores the session gate, but NativeRelay still checks ring generation
  // and requires actual downstream release before returning upstream ownership.
  flResult collect(){return initialized?native.collect():FL_INVALID;}
  flResult finish(){
    if(!initialized)return FL_INVALID;
    stopped=true;auto r=native.finish();if(r==FL_OK)initialized=false;return r;
  }
  bool videoEnded()const{return ended;}
  gint64 finalPts()const{return lastPts;}
  unsigned pending()const{return native.pending();}
  uint64_t count()const{return native.count();}
  FlGstSessionResult sessionStatus()const{return status;}
 private:
  flResult stop(flResult result){stopped=true;return result;}
  FlGstSession* session;FlGstSession* destination;flChannel*up;SessionRelayContract contract;NativeRelay native;
  gint64 lastPts=-1;
  guint64 generation=0;bool attempted=false,initialized=false,stopped=false,ended=false;
  FlGstSessionResult status=FL_GST_SESSION_INVALID;
};
}

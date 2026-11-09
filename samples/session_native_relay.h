#pragma once
#include "native_relay.h"
#include "session_relay_contract.h"

namespace flsample {
// Checked VIDEO leg of a broker-composed coordinated relay. All objects are
// borrowed; the owner serializes configuration/publication and owns PCM/events.
// This does not authenticate peers, forward events, or infer terminal accounting.
// No hidden copies. Preflight/metadata rejection leaves input with the caller.
// Native submission failure may already own a lease (see NativeRelay); never
// release it speculatively. Every error permanently stops publication, while
// collect/finish still permit safe native drain.
class SessionNativeRelay {
 public:
  SessionNativeRelay(FlGstSession* upstreamSession,FlGstSession* downstreamSession,
                     flChannel* upstream,flChannel* downstream,NativeRelay::SameAllocation same)
      :session(upstreamSession),up(upstream),contract(upstreamSession,downstreamSession),
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
    r=native.forward(frame);return r==FL_OK?r:stop(r);
  }
  // Session cancellation must NOT force-complete a downstream reader's lease.
  // Drain ignores the session gate, but NativeRelay still checks ring generation
  // and requires actual downstream release before returning upstream ownership.
  flResult collect(){return initialized?native.collect():FL_INVALID;}
  flResult finish(){
    if(!initialized)return FL_INVALID;
    stopped=true;auto r=native.finish();if(r==FL_OK)initialized=false;return r;
  }
  unsigned pending()const{return native.pending();}
  uint64_t count()const{return native.count();}
  FlGstSessionResult sessionStatus()const{return status;}
 private:
  flResult stop(flResult result){stopped=true;return result;}
  FlGstSession* session;flChannel*up;SessionRelayContract contract;NativeRelay native;
  guint64 generation=0;bool attempted=false,initialized=false,stopped=false;
  FlGstSessionResult status=FL_GST_SESSION_INVALID;
};
}

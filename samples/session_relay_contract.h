#pragma once
#include "../gst/framelink_session_context.h"
#include <array>
#include <cstring>

namespace flsample {
// Preflight for forwarding raw source-time video and already-decoded PCM between
// two authenticated sessions. This is NOT authentication or an automatic relay.
// Sessions are borrowed. One owner must serialize configuration, checks and
// publication; callers must stop on any failure, never retry with unchecked data.
// Nonidentity intermediate PCM mapping needs an explicit inverse transform and
// is deliberately unsupported here. Final-receiver mapping remains independent.
class SessionRelayContract {
 public:
  SessionRelayContract(FlGstSession* upstreamReceiver,FlGstSession* downstreamProducer)
      :up(upstreamReceiver),down(downstreamProducer){}
  static bool compatible(const flSessionConfig&a,const flSessionConfig&b){
    return flSessionValidate(&a)==FL_SESSION_OK&&flSessionValidate(&b)==FL_SESSION_OK&&
      a.version==b.version&&a.generation==b.generation&&a.epochNs==b.epochNs&&
      std::memcmp(&a.sessionId,&b.sessionId,sizeof a.sessionId)!=0&&
      std::memcmp(&a.clockId,&b.clockId,sizeof a.clockId)==0&&
      a.videoTrackId&&a.audioTrackId&&a.videoTrackId==b.videoTrackId&&a.audioTrackId==b.audioTrackId&&
      a.video.format==b.video.format&&a.video.width==b.video.width&&a.video.height==b.video.height&&
      a.video.fpsNum==b.video.fpsNum&&a.video.fpsDen==b.video.fpsDen&&
      a.audio.pcm==b.audio.pcm&&a.audio.rate==b.audio.rate&&a.audio.layout==b.audio.layout;
  }
  FlGstSessionResult initialize(){
    if(initialized)return FL_GST_SESSION_INVALID;
    flSessionConfig a{},b{};auto r=read(a,b);if(r!=FL_GST_SESSION_READY)return r;
    if(!compatible(a,b))return FL_GST_SESSION_INVALID;
    r=identity(a.generation);if(r!=FL_GST_SESSION_READY)return r;
    if(flSessionEncode(&a,upWire.data(),upWire.size())!=FL_SESSION_OK||
       flSessionEncode(&b,downWire.data(),downWire.size())!=FL_SESSION_OK)return FL_GST_SESSION_INVALID;
    initialized=true;return FL_GST_SESSION_READY;
  }
  FlGstSessionResult check()const{
    if(!initialized)return FL_GST_SESSION_INVALID;
    flSessionConfig a{},b{};auto r=read(a,b);if(r!=FL_GST_SESSION_READY)return r;
    std::array<unsigned char,FL_SESSION_WIRE_SIZE>x{},y{};
    if(flSessionEncode(&a,x.data(),x.size())!=FL_SESSION_OK||
       flSessionEncode(&b,y.data(),y.size())!=FL_SESSION_OK)return FL_GST_SESSION_INVALID;
    if(x!=upWire||y!=downWire)return FL_GST_SESSION_STALE;
    return identity(a.generation);
  }
 private:
  FlGstSessionResult read(flSessionConfig&a,flSessionConfig&b)const{
    auto r=fl_gst_session_get_config(up,&a);
    return r==FL_GST_SESSION_READY?fl_gst_session_get_config(down,&b):r;
  }
  FlGstSessionResult identity(guint64 generation)const{
    // Current mappings are translations, not rate transforms. Require a mapping
    // and exact source-relative running time: no implicit identity fallback.
    gint64 mapped=-1;auto r=fl_gst_session_map_running_time(up,generation,0,TRUE,&mapped);
    return r!=FL_GST_SESSION_READY?r:(mapped==0?FL_GST_SESSION_READY:FL_GST_SESSION_INVALID);
  }
  FlGstSession*up;FlGstSession*down;bool initialized=false;
  std::array<unsigned char,FL_SESSION_WIRE_SIZE>upWire{},downWire{};
};
}

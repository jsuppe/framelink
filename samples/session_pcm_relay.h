#pragma once
#include "session_native_relay.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

namespace flsample {
// Broker-composed PCM leg. Borrowed sessions/ports must be bound to the matching
// authenticated audio elements by the owner. This is not port authentication.
// One serialized reader/writer only; do not change ports/properties while active.
// A bounded, non-dropping input appsink is required. A single pending sample
// plus the destination audioQueueMs byte budget bounds
// this adapter's buffering (transport/in-flight buffers are separately bounded).
// No payload copy, timestamp rebasing, leaky queue or silent PCM loss.
// Serialized media events (including GAP) are currently unsupported and fail
// closed. Never use sample-only pulls here: they silently discard these events.
class SessionPcmRelay {
 public:
  SessionPcmRelay(FlGstSession* up,FlGstSession* down,GstAppSink* input,GstAppSrc* output)
      :session(up),destination(down),source(input),sink(output),contract(up,down){}
  ~SessionPcmRelay(){if(pending)gst_sample_unref(pending);}
  SessionPcmRelay(const SessionPcmRelay&)=delete;
  SessionPcmRelay& operator=(const SessionPcmRelay&)=delete;
  FlGstSessionResult initialize(){
    if(attempted)return FL_GST_SESSION_INVALID;
    attempted=true;
    auto r=contract.initialize();if(r!=FL_GST_SESSION_READY)return fail(r);
    if(!GST_IS_APP_SINK(source)||!GST_IS_APP_SRC(sink))return fail(FL_GST_SESSION_INVALID);
    if(fl_gst_session_get_config(destination,&config)!=FL_GST_SESSION_READY)return fail(FL_GST_SESSION_INVALID);
    gboolean block=FALSE;gint leaky=0;GstFormat format=GST_FORMAT_UNDEFINED;
    g_object_get(sink,"block",&block,"leaky-type",&leaky,"format",&format,nullptr);
    if(block||leaky||format!=GST_FORMAT_TIME||gst_app_sink_get_drop(source)||!gst_app_sink_get_max_buffers(source))return fail(FL_GST_SESSION_INVALID);
    auto*caps=gst_app_src_get_caps(sink);bool valid=validCaps(caps);if(caps)gst_caps_unref(caps);
    if(!valid)return fail(FL_GST_SESSION_INVALID);
    bytesPerFrame=(config.audio.pcm==FL_PCM_S16?2u:4u)*config.audio.layout;
    budget=((guint64(config.audio.rate)*config.audioQueueMs+999)/1000)*bytesPerFrame;
    initialized=true;return FL_GST_SESSION_READY;
  }
  // READY means exactly one sample queued. TIMEOUT preserves any pending sample
  // for retry; it also denotes no sample available (including input EOS).
  FlGstSessionResult forward(guint timeoutMs){
    if(!initialized||failed||ended)return FL_GST_SESSION_INVALID;
    auto r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
    if(!pending){
      // appsink has no knowledge of session cancellation/replacement. Poll in
      // bounded slices against one monotonic deadline, never the full timeout.
      const gint64 deadline=g_get_monotonic_time()+gint64(timeoutMs)*1000;
      do{
        const gint64 remaining=deadline-g_get_monotonic_time();
        const guint64 slice=remaining<=0?0:guint64(remaining<10000?remaining:10000)*1000;
        auto*object=gst_app_sink_try_pull_object(source,slice);
        if(object){r=acceptObject(object);if(r!=FL_GST_SESSION_READY)return r;}
        r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
        if(pending||gst_app_sink_is_eos(source)||g_get_monotonic_time()>=deadline)break;
      }while(true);
    }
    if(!pending){r=contract.check();return r==FL_GST_SESSION_READY?FL_GST_SESSION_TIMEOUT:fail(r);}
    r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
    auto*b=gst_sample_get_buffer(pending);auto*segment=gst_sample_get_segment(pending);
    GstClockTime running=GST_CLOCK_TIME_NONE;
    if(!b||!validCaps(gst_sample_get_caps(pending))||!segment||
       !fl_gst_segment_running_time(segment,GST_BUFFER_PTS(b),&running)||running!=GST_BUFFER_PTS(b)||
       running>G_MAXINT64||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(b))||
       GST_BUFFER_DURATION(b)>G_MAXINT64-running)return fail(FL_GST_SESSION_INVALID);
    const guint64 size=gst_buffer_get_size(b),frames=size/bytesPerFrame;
    if(!size||size%bytesPerFrame||size>budget)return fail(FL_GST_SESSION_INVALID);
    guint64 base=origin,samples=continuousFrames;
    if(GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT)){base=GST_CLOCK_TIME_NONE;samples=0;}
    if(base==GST_CLOCK_TIME_NONE)base=running;
    gint64 first=0,last=0;
    if(frames>G_MAXUINT64-samples||frames>G_MAXUINT64-totalFrames||size>G_MAXUINT64-totalBytes||
       flSessionSampleOffset(samples,config.audio.rate,&first)!=FL_SESSION_OK||
       flSessionSampleOffset(samples+frames,config.audio.rate,&last)!=FL_SESSION_OK||
       guint64(first)>G_MAXINT64-base||difference(running,base+first)>1||
       difference(GST_BUFFER_DURATION(b),last-first)>1)return fail(FL_GST_SESSION_INVALID);
    const auto queued=gst_app_src_get_current_level_bytes(sink);
    if(queued>budget-size)return FL_GST_SESSION_TIMEOUT;
    if(gst_app_src_push_buffer(sink,gst_buffer_ref(b))!=GST_FLOW_OK)return fail(FL_GST_SESSION_CANCELLED);
    origin=base;continuousFrames=samples+frames;totalFrames+=frames;totalBytes+=size;++blocks;
    gst_sample_unref(pending);pending=nullptr;return FL_GST_SESSION_READY;
  }
  // Join the authenticated audio terminal with the actual forwarded native END.
  // Input appsink must be fully drained; no queue discard is allowed. READY means
  // downstream EOS accepted, not downstream playback/native completion proof.
  FlGstSessionResult finish(const SessionNativeRelay&video,guint timeoutMs){
    if(!initialized||failed||ended)return FL_GST_SESSION_INVALID;
    auto r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
    if(pending||!gst_app_sink_is_eos(source)||!video.videoEnded()||video.pending())return FL_GST_SESSION_TIMEOUT;
    // is_eos() only guarantees no queued samples; serialized events can
    // remain. Inspect them before publishing a successful terminal. Bound the
    // work per call; TIMEOUT retries never discard uninspected events.
    for(unsigned n=0;;++n){
      if(n==32)return FL_GST_SESSION_TIMEOUT;
      auto*object=gst_app_sink_try_pull_object(source,0);
      if(!object)break;
      r=acceptObject(object);if(r!=FL_GST_SESSION_READY)return r;
      if(pending)return FL_GST_SESSION_TIMEOUT;
    }
    guint64 frames=0;gint64 last=-1;
    r=fl_gst_session_wait_video_eos(session,config.generation,timeoutMs,&frames,&last);
    if(r==FL_GST_SESSION_TIMEOUT)return r;
    if(r!=FL_GST_SESSION_READY)return fail(r);
    r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
    if(frames!=video.count()||last!=video.finalPts())return fail(FL_GST_SESSION_INVALID);
    r=fl_gst_session_video_eos(destination,config.generation,frames,last);
    if(r!=FL_GST_SESSION_READY)return fail(r);
    if(gst_app_src_end_of_stream(sink)!=GST_FLOW_OK)return fail(FL_GST_SESSION_CANCELLED);
    ended=true;return FL_GST_SESSION_READY;
  }
  guint64 frameCount()const{return totalFrames;}
  guint64 byteCount()const{return totalBytes;}
  guint64 blockCount()const{return blocks;}
 private:
  FlGstSessionResult acceptObject(GstMiniObject*object){
    if(GST_IS_SAMPLE(object)){pending=GST_SAMPLE(object);return FL_GST_SESSION_READY;}
    // Caps/segment are validated again on every sample. Stream setup and tags
    // carry no missing PCM. Unsupported events must never vanish from a relay.
    const bool setup=GST_IS_EVENT(object)&&
      (GST_EVENT_TYPE(object)==GST_EVENT_STREAM_START||
       GST_EVENT_TYPE(object)==GST_EVENT_CAPS||
       GST_EVENT_TYPE(object)==GST_EVENT_SEGMENT||
       GST_EVENT_TYPE(object)==GST_EVENT_TAG);
    gst_mini_object_unref(object);
    return setup?FL_GST_SESSION_READY:fail(FL_GST_SESSION_INVALID);
  }
  static guint64 difference(guint64 a,guint64 b){return a>b?a-b:b-a;}
  bool validCaps(GstCaps*caps)const{
    if(!caps||!gst_caps_is_fixed(caps)||gst_caps_get_size(caps)!=1)return false;
    auto*s=gst_caps_get_structure(caps,0);gint rate=0,channels=0;
    return gst_structure_has_name(s,"audio/x-raw")&&
      g_strcmp0(gst_structure_get_string(s,"format"),config.audio.pcm==FL_PCM_S16?"S16LE":"F32LE")==0&&
      g_strcmp0(gst_structure_get_string(s,"layout"),"interleaved")==0&&
      gst_structure_get_int(s,"rate",&rate)&&guint(rate)==config.audio.rate&&
      gst_structure_get_int(s,"channels",&channels)&&guint(channels)==config.audio.layout;
  }
  FlGstSessionResult fail(FlGstSessionResult r){failed=true;fl_gst_session_cancel(destination);return r;}
  FlGstSession*session;FlGstSession*destination;GstAppSink*source;GstAppSrc*sink;
  SessionRelayContract contract;flSessionConfig config{};GstSample*pending=nullptr;
  guint64 budget=0,bytesPerFrame=0,origin=GST_CLOCK_TIME_NONE,continuousFrames=0,totalFrames=0,totalBytes=0,blocks=0;
  bool attempted=false,initialized=false,failed=false,ended=false;
};
}

#pragma once
#include "session_native_relay.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <atomic>
#include <memory>

namespace flsample {
// Broker-composed PCM leg. Borrowed sessions/ports must be bound to the matching
// authenticated audio elements by the owner. This is not port authentication.
// One serialized reader/writer only; do not change ports/properties while active.
// A bounded, non-dropping input appsink is required. A single pending sample
// plus the destination audioQueueMs byte budget bounds
// this adapter's buffering (transport/in-flight buffers are separately bounded).
// No payload copy, timestamp rebasing, leaky queue or silent PCM loss.
// GAP forwarding is explicit opt-in; other serialized media events fail closed.
// At most one GAP is queued in appsrc plus one in flight and one pending here.
// The broker must also bound its input event producer (appsink max-buffers only
// bounds samples). Never use sample-only pulls: they silently discard events.
class SessionPcmRelay {
 public:
  SessionPcmRelay(FlGstSession* up,FlGstSession* down,GstAppSink* input,GstAppSrc* output,bool forwardGaps=false)
      :session(up),destination(down),source(input),sink(output),contract(up,down),allowGaps(forwardGaps){}
  ~SessionPcmRelay(){
    if(gapPad){gst_pad_remove_probe(gapPad,gapProbe);gst_object_unref(gapPad);}
    if(pending)gst_sample_unref(pending);
    if(pendingGap)gst_event_unref(pendingGap);
  }
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
    if(allowGaps){
      gapPad=gst_element_get_static_pad(GST_ELEMENT(sink),"src");
      if(!gapPad)return fail(FL_GST_SESSION_INVALID);
      auto*state=new std::shared_ptr<GapQueue>(gapQueue);
      gapProbe=gst_pad_add_probe(gapPad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        [](GstPad*,GstPadProbeInfo*info,gpointer data)->GstPadProbeReturn{
          auto state=*static_cast<std::shared_ptr<GapQueue>*>(data);
          auto*event=GST_PAD_PROBE_INFO_EVENT(info);
          auto*owner=static_cast<std::shared_ptr<GapQueue>*>(
            gst_mini_object_get_qdata(GST_MINI_OBJECT(event),gapTag()));
          if(owner&&owner->get()==state.get())state->queued.store(false);
          return GST_PAD_PROBE_OK;
        },state,[](gpointer data){delete static_cast<std::shared_ptr<GapQueue>*>(data);});
    }
    initialized=true;return FL_GST_SESSION_READY;
  }
  // READY means exactly one sample or opted-in GAP queued. TIMEOUT preserves pending data
  // for retry; it also denotes no sample available (including input EOS).
  FlGstSessionResult forward(guint timeoutMs){
    if(!initialized||failed||ended)return FL_GST_SESSION_INVALID;
    auto r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
    if(!pending&&!pendingGap){
      // appsink has no knowledge of session cancellation/replacement. Poll in
      // bounded slices against one monotonic deadline, never the full timeout.
      const gint64 deadline=g_get_monotonic_time()+gint64(timeoutMs)*1000;
      do{
        const gint64 remaining=deadline-g_get_monotonic_time();
        const guint64 slice=remaining<=0?0:guint64(remaining<10000?remaining:10000)*1000;
        auto*object=gst_app_sink_try_pull_object(source,slice);
        if(object){r=acceptObject(object);if(r!=FL_GST_SESSION_READY)return r;}
        r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
        if(pending||pendingGap||gst_app_sink_is_eos(source)||g_get_monotonic_time()>=deadline)break;
      }while(true);
    }
    if(pendingGap)return forwardGap();
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
    if(pendingGap){r=forwardGap();if(r!=FL_GST_SESSION_READY)return r;}
    // is_eos() only guarantees no queued samples; serialized events can
    // remain. Inspect them before publishing a successful terminal. Bound the
    // work per call; TIMEOUT retries never discard uninspected events.
    for(unsigned n=0;;++n){
      if(n==32)return FL_GST_SESSION_TIMEOUT;
      auto*object=gst_app_sink_try_pull_object(source,0);
      if(!object)break;
      r=acceptObject(object);if(r!=FL_GST_SESSION_READY)return r;
      if(pending)return FL_GST_SESSION_TIMEOUT;
      if(pendingGap){r=forwardGap();if(r!=FL_GST_SESSION_READY)return r;}
    }
    guint64 frames=0;gint64 last=-1;
    // The upstream condition variable cannot wake for downstream cancellation
    // or replacement. Observe both sessions between bounded waits against one
    // deadline, just as forward() does for the independent appsink queue.
    if(timeoutMs>60000)return fail(FL_GST_SESSION_INVALID);
    const gint64 deadline=g_get_monotonic_time()+gint64(timeoutMs)*1000;
    do{
      r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
      const gint64 remaining=deadline-g_get_monotonic_time();
      const guint slice=remaining<=0?0:guint(remaining>=10000?10:(remaining+999)/1000);
      r=fl_gst_session_wait_video_eos(session,config.generation,slice,&frames,&last);
      auto state=contract.check();if(state!=FL_GST_SESSION_READY)return fail(state);
      if(r==FL_GST_SESSION_READY)break;
      if(r!=FL_GST_SESSION_TIMEOUT)return fail(r);
      if(g_get_monotonic_time()>=deadline)return FL_GST_SESSION_TIMEOUT;
    }while(true);
    if(frames!=video.count()||last!=video.finalPts())return fail(FL_GST_SESSION_INVALID);
    r=fl_gst_session_video_eos(destination,config.generation,frames,last);
    if(r!=FL_GST_SESSION_READY)return fail(r);
    if(gst_app_src_end_of_stream(sink)!=GST_FLOW_OK)return fail(FL_GST_SESSION_CANCELLED);
    ended=true;return FL_GST_SESSION_READY;
  }
  guint64 gapCount()const{return gaps;}
  guint64 frameCount()const{return totalFrames;}
  guint64 byteCount()const{return totalBytes;}
  guint64 blockCount()const{return blocks;}
 private:
  struct GapQueue {std::atomic<bool> queued{false};};
  static GQuark gapTag(){return g_quark_from_static_string("framelink-pcm-relay-gap-owner");}
  FlGstSessionResult forwardGap(){
    auto r=contract.check();if(r!=FL_GST_SESSION_READY)return fail(r);
    if(gapQueue->queued.load())return FL_GST_SESSION_TIMEOUT;
    GstClockTime pts=0,duration=0,running=0;gst_event_parse_gap(pendingGap,&pts,&duration);
    if(!haveSegment||!fl_gst_segment_running_time(&segment,pts,&running)||running!=pts||
       running>G_MAXINT64||!GST_CLOCK_TIME_IS_VALID(duration)||duration>G_MAXINT64-running)
      return fail(FL_GST_SESSION_INVALID);
    gint64 offset=0;
    if(origin!=GST_CLOCK_TIME_NONE&&
       (flSessionSampleOffset(continuousFrames,config.audio.rate,&offset)!=FL_SESSION_OK||
        guint64(offset)>G_MAXINT64-origin||difference(running,origin+offset)>1))
      return fail(FL_GST_SESSION_INVALID);
    // appsrc queues serialized events in order with its buffers. A private
    // qdata tag retires this queue credit at the src pad, not at enqueue time.
    auto*event=gst_event_make_writable(pendingGap);pendingGap=nullptr;
    // Event owns its credit identity even if the adapter is destroyed before
    // appsrc releases a queued event. Never retain a pointer into the adapter.
    gst_mini_object_set_qdata(GST_MINI_OBJECT(event),gapTag(),
      new std::shared_ptr<GapQueue>(gapQueue),
      [](gpointer data){delete static_cast<std::shared_ptr<GapQueue>*>(data);});
    gapQueue->queued.store(true);
    if(!gst_element_send_event(GST_ELEMENT(sink),event)){
      gapQueue->queued.store(false);return fail(FL_GST_SESSION_CANCELLED);
    }
    origin=running+duration;continuousFrames=0;++gaps;return FL_GST_SESSION_READY;
  }
  FlGstSessionResult acceptObject(GstMiniObject*object){
    if(GST_IS_SAMPLE(object)){pending=GST_SAMPLE(object);return FL_GST_SESSION_READY;}
    if(GST_IS_EVENT(object)&&GST_EVENT_TYPE(object)==GST_EVENT_GAP&&allowGaps){
      pendingGap=GST_EVENT(object);return FL_GST_SESSION_READY;
    }
    if(allowGaps&&GST_IS_EVENT(object)&&GST_EVENT_TYPE(object)==GST_EVENT_CAPS){
      GstCaps*caps=nullptr;gst_event_parse_caps(GST_EVENT(object),&caps);
      if(!validCaps(caps)){gst_mini_object_unref(object);return fail(FL_GST_SESSION_INVALID);}
    }
    if(GST_IS_EVENT(object)&&GST_EVENT_TYPE(object)==GST_EVENT_SEGMENT){
      const GstSegment*received=nullptr;gst_event_parse_segment(GST_EVENT(object),&received);
      segment=*received;haveSegment=true;
    }
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
  std::shared_ptr<GapQueue> gapQueue=std::make_shared<GapQueue>();
  GstPad*gapPad=nullptr;gulong gapProbe=0;GstEvent*pendingGap=nullptr;
  GstSegment segment{};guint64 gaps=0;bool haveSegment=false,allowGaps=false;
  bool attempted=false,initialized=false,failed=false,ended=false;
};
}

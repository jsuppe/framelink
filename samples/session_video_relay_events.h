#pragma once
#include "../gst/framelink_session_context.h"
#include <array>
#include <mutex>

namespace flsample {
// Bounded serialized-event adapter for an already-authenticated avsrc output.
// This is sample C++ code, not a new public ABI or discovery/relay factory.
// Attach probe before the first segment/buffer. One broker owns drain(); observations may arrive
// on a streaming thread. Quiesce/remove the probe before destroying this object.
// The broker validates SessionRelayContract and the native lease separately,
// drains before each frame and after upstream terminal confirmation, and sends
// barriers/END itself. No native lease is ever released by this adapter.
class SessionVideoRelayEvents {
 public:
  static GstPadProbeReturn probe(GstPad*,GstPadProbeInfo* info,gpointer data){
    auto& self=*static_cast<SessionVideoRelayEvents*>(data);
    if(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_BUFFER)
      self.observeBuffer(GST_PAD_PROBE_INFO_BUFFER(info));
    else if(GST_PAD_PROBE_INFO_TYPE(info)&(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM|GST_PAD_PROBE_TYPE_EVENT_FLUSH))
      self.observeEvent(GST_PAD_PROBE_INFO_EVENT(info));
    return GST_PAD_PROBE_OK; // broker observes sticky failure; never hides it as EOS
  }
  FlGstSessionResult observeBuffer(GstBuffer* buffer){
    std::lock_guard<std::mutex> lock(mutex);
    if(state!=FL_GST_SESSION_READY)return state;
    GstClockTime running=GST_CLOCK_TIME_NONE;
    if(!buffer||!haveSegment||ended||frames==G_MAXUINT64||
       !fl_gst_segment_running_time(&segment,GST_BUFFER_PTS(buffer),&running)||
       running!=GST_BUFFER_PTS(buffer)||running>guint64(G_MAXINT64))return fail();
    ++frames;return state;
  }
  FlGstSessionResult observeEvent(GstEvent* event){
    std::lock_guard<std::mutex> lock(mutex);
    if(state!=FL_GST_SESSION_READY)return state;
    if(!event)return fail();
    const auto type=GST_EVENT_TYPE(event);
    if(type==GST_EVENT_FLUSH_START||type==GST_EVENT_FLUSH_STOP){state=FL_GST_SESSION_CANCELLED;return state;}
    if(ended)return fail();
    switch(type){
      case GST_EVENT_STREAM_START:case GST_EVENT_CAPS:
        if(frames||size)return fail();
        return state; // immutable caps/session agreement belongs to broker
      case GST_EVENT_TAG:return state; // informational, not a timeline event
      case GST_EVENT_SEGMENT:{
        const GstSegment* value=nullptr;gst_event_parse_segment(event,&value);
        // Only canonical identity TIME is supported; no seek/rate/rebase fallback.
        if(!value||value->format!=GST_FORMAT_TIME||value->rate!=1.0||value->applied_rate!=1.0||
           value->start||value->time||value->base||value->offset||value->flags||
           value->stop!=GST_CLOCK_TIME_NONE)return fail();
        segment=*value;haveSegment=true;return state;
      }
      case GST_EVENT_GAP:{
        GstClockTime pts=0,duration=0;gst_event_parse_gap(event,&pts,&duration);
        GstClockTime running=GST_CLOCK_TIME_NONE;
        if(!haveSegment||!GST_CLOCK_TIME_IS_VALID(duration)||!duration||
           !fl_gst_segment_running_time(&segment,pts,&running)||running!=pts||
           pts>guint64(G_MAXINT64)||duration>guint64(G_MAXINT64)-pts||size==queue.size())return fail();
        queue[(head+size)%queue.size()]={FL_GST_VIDEO_GAP,frames,pts,duration};++size;return state;
      }
      case GST_EVENT_EOS:ended=true;return state;
      default:return fail(); // unknown serialized timeline semantics are not ignored
    }
  }
  // Called by the sole broker. Callback must return the actual send result.
  // Never hold the observation mutex during potentially blocking publication.
  template<class Send> FlGstSessionResult drain(guint64 forwardedFrames,Send send){
    for(;;){
      FlGstVideoEvent event{};
      {
        std::lock_guard<std::mutex> lock(mutex);
        if(state!=FL_GST_SESSION_READY)return state;
        if(forwardedFrames>frames)return fail();
        if(!size)return state;
        event=queue[head];
        if(event.afterFrames<forwardedFrames)return fail();
        if(event.afterFrames>forwardedFrames)return state;
        head=(head+1)%queue.size();--size;
      }
      auto result=send(event);
      if(result!=FL_GST_SESSION_READY){std::lock_guard<std::mutex> lock(mutex);state=result;return state;}
    }
  }
  FlGstSessionResult status(){std::lock_guard<std::mutex> lock(mutex);return state;}
  void cancel(){std::lock_guard<std::mutex> lock(mutex);state=FL_GST_SESSION_CANCELLED;}
 private:
  FlGstSessionResult fail(){state=FL_GST_SESSION_INVALID;return state;}
  std::mutex mutex;
  std::array<FlGstVideoEvent,32> queue{};
  size_t head=0,size=0;
  guint64 frames=0;
  GstSegment segment{};
  bool haveSegment=false,ended=false;
  FlGstSessionResult state=FL_GST_SESSION_READY;
};
}

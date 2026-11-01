#pragma once
#include <gst/audio/audio.h>
#include <algorithm>

namespace av {
// GstAudioBaseSink RESAMPLE aligns a buffer's start with next_sample but not
// its stop. A large clock-calibration jump can therefore make out_samples
// negative during forward playback (observed with OpenSLES/GStreamer1.24).
// Treat phase corrections larger than half a PCM block as discontinuities
// immediately, instead of waiting the default second and reversing/expanding
// blocks. Small drift still uses RESAMPLE. No PTS, duration or payload changes.
inline bool protectAudioAlignment(GstElement* sink) {
  auto* klass=G_OBJECT_GET_CLASS(sink);
  if(!g_object_class_find_property(klass,"alignment-threshold") ||
     !g_object_class_find_property(klass,"discont-wait"))return false;
  if(g_object_get_data(G_OBJECT(sink),"fl-alignment-guard"))return true;
  auto* pad=gst_element_get_static_pad(sink,"sink");if(!pad)return false;
  struct State {GstElement* sink;GstAudioInfo info{};guint64 limit=0,last=GST_CLOCK_TIME_NONE;};
  auto* state=new State{sink};gst_audio_info_init(&state->info);
  g_object_get(sink,"alignment-threshold",&state->limit,nullptr);
  auto* caps=gst_pad_get_current_caps(pad);
  if(caps){gst_audio_info_from_caps(&state->info,caps);gst_caps_unref(caps);}
  g_object_set(sink,"discont-wait",guint64(0),nullptr);
  auto id=gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
    [](GstPad*,GstPadProbeInfo* probe,gpointer data) {
      auto& s=*static_cast<State*>(data);
      if(GST_PAD_PROBE_INFO_TYPE(probe)&GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        auto* event=GST_PAD_PROBE_INFO_EVENT(probe);
        if(GST_EVENT_TYPE(event)==GST_EVENT_CAPS){GstCaps* caps=nullptr;gst_event_parse_caps(event,&caps);gst_audio_info_from_caps(&s.info,caps);}
      } else if(auto* buffer=GST_PAD_PROBE_INFO_BUFFER(probe)) {
        const auto bpf=GST_AUDIO_INFO_BPF(&s.info),rate=GST_AUDIO_INFO_RATE(&s.info);
        if(bpf>0 && rate>0) {
          const auto samples=gst_buffer_get_size(buffer)/bpf;
          // Infer from negotiated raw frames, not a possibly rounded/missing
          // buffer duration. The resulting threshold is never > half a block.
          auto threshold=std::min(s.limit,gst_util_uint64_scale(samples,GST_SECOND,guint64(rate)*2));
          if(threshold!=s.last){g_object_set(s.sink,"alignment-threshold",threshold,nullptr);s.last=threshold;}
        }
      }
      return GST_PAD_PROBE_OK;
    },state,[](gpointer p){delete static_cast<State*>(p);});
  gst_object_unref(pad);
  if(!id)return false;
  g_object_set_data(G_OBJECT(sink),"fl-alignment-guard",GINT_TO_POINTER(1));
  return true;
}
}

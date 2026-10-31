#pragma once
#include "framelink_session.h"
#include <gst/gst.h>
#include <atomic>

namespace av {
struct AudioTimeline {
  GstElement* pipeline=nullptr;
  GstClock* playbackClock=nullptr;
  std::atomic<uint64_t> deliveryBase{0}, lateBlocks{0}, consecutiveLate{0};
  std::atomic<bool> exhausted{false};
  std::atomic<int64_t> arrivalDrift{0};
  std::atomic<bool> resetArrival{false};
  uint64_t firstArrival=0, firstMedia=0;
  std::atomic<uint64_t> emptyEvents{0}, fullEvents{0};
  ~AudioTimeline() {
    // Probes/signals carry this object's address: stop before its destruction,
    // including exceptions. Keep selected clock alive until streaming stops.
    if(pipeline) gst_element_set_state(pipeline,GST_STATE_NULL);
    if(playbackClock) gst_object_unref(playbackClock);
  }
  std::atomic<uint64_t> blocks{0}, samplesEnd{0}, originNs{GST_CLOCK_TIME_NONE};
  std::atomic<bool> invalid{false};
  unsigned rate=48000, channels=2;
  GstSegment segment{};
  bool haveSegment=false;
};
inline GstPadProbeReturn trackAudio(GstPad*, GstPadProbeInfo* info, gpointer data) {
  auto &t=*static_cast<AudioTimeline*>(data);
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    auto *event=GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event)==GST_EVENT_SEGMENT) {
      const GstSegment* segment=nullptr;
      gst_event_parse_segment(event,&segment);
      // PCM sample duration is defined at negotiated rate. Trick modes would
      // require coordinated video/audio rate conversion, not a local rewrite.
      if (segment->format!=GST_FORMAT_TIME || segment->rate!=1.0 || segment->applied_rate!=1.0) {
        t.invalid=true; return GST_PAD_PROBE_DROP;
      }
      t.segment=*segment; t.haveSegment=true;
    }
    return GST_PAD_PROBE_OK;
  }
  auto *b=GST_PAD_PROBE_INFO_BUFFER(info);
  if (!b) return GST_PAD_PROBE_OK;
  const auto media=t.haveSegment ? gst_segment_to_running_time(&t.segment,GST_FORMAT_TIME,GST_BUFFER_PTS(b)) : GST_CLOCK_TIME_NONE;
  if (!GST_CLOCK_TIME_IS_VALID(media)) {t.invalid=true;return GST_PAD_PROBE_DROP;}
  auto bytes=gst_buffer_get_size(b);
  uint64_t first=t.samplesEnd.load(), count=bytes/(2*t.channels);
  int64_t expected=0,next=0;
  // Segment running-time preserves the producer media offset, not arrival time.
  // Remember it for continuity checks; NEVER rewrite the downstream timestamp.
  if (!first && t.originNs.load()==GST_CLOCK_TIME_NONE && GST_CLOCK_TIME_IS_VALID(media))
    t.originNs=media;
  const auto origin=t.originNs.load();
  if (!GST_CLOCK_TIME_IS_VALID(origin) || bytes%(2*t.channels) || flSessionSampleOffset(first,t.rate,&expected)!=0 ||
      flSessionSampleOffset(first+count,t.rate,&next)!=0 || uint64_t(next)>GST_CLOCK_TIME_NONE-1-origin || media!=origin+uint64_t(expected) ||
      GST_BUFFER_DURATION(b)!=uint64_t(next-expected)) t.invalid=true;
  if (t.invalid.load()) return GST_PAD_PROBE_DROP; // never schedule corrupt PCM
  const auto arrival=gst_util_get_timestamp();
  if(t.resetArrival.exchange(false) || !t.firstArrival){t.firstArrival=arrival;t.firstMedia=media;}
  t.arrivalDrift=int64_t(arrival-t.firstArrival)-int64_t(media-t.firstMedia);
  // Acquire the published base before reading playbackClock (set before base).
  const auto base=t.deliveryBase.load();
  if(base) {
    if(media>GST_CLOCK_TIME_NONE-1-base) {t.invalid=true;return GST_PAD_PROBE_DROP;}
    const auto due=base+media;
    const auto now=gst_clock_get_time(t.playbackClock);
    if(now>due && now-due>100*GST_MSECOND) {
      ++t.lateBlocks;
      if(++t.consecutiveLate>=3) t.exhausted=true;
    } else t.consecutiveLate=0;
  }
  t.samplesEnd=first+count; ++t.blocks;
  return GST_PAD_PROBE_OK;
}
} // namespace av

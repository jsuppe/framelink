#include <initializer_list>
#include "../samples/audio_timeline.h"
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do {if(!(x)){std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static GstPadProbeReturn segment(av::AudioTimeline& t, GstSegment& s) {
  auto* e=gst_event_new_segment(&s);
  GstPadProbeInfo i{};i.type=GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM;i.data=e;
  auto r=av::trackAudio(nullptr,&i,&t);gst_event_unref(e);return r;
}
static GstPadProbeReturn buffer(av::AudioTimeline& t,guint64 pts) {
  auto* b=gst_buffer_new_allocate(nullptr,1920,nullptr); // 480 stereo samples = 10 ms
  GST_BUFFER_PTS(b)=pts;GST_BUFFER_DURATION(b)=10*GST_MSECOND;
  GstPadProbeInfo i{};i.type=GST_PAD_PROBE_TYPE_BUFFER;i.data=b;
  auto r=av::trackAudio(nullptr,&i,&t);
  REQUIRE(GST_BUFFER_PTS(b)==pts); // observation must never retimestamp playback
  gst_buffer_unref(b);return r;
}
int main(int argc,char**argv){
  gst_init(&argc,&argv);
  GstSegment s;gst_segment_init(&s,GST_FORMAT_TIME);
  s.start=5*GST_SECOND;s.time=3*GST_SECOND;s.base=2*GST_SECOND;
  av::AudioTimeline t;
  REQUIRE(segment(t,s)==GST_PAD_PROBE_OK);
  REQUIRE(buffer(t,s.start+37*GST_MSECOND)==GST_PAD_PROBE_OK);
  REQUIRE(buffer(t,s.start+47*GST_MSECOND)==GST_PAD_PROBE_OK);
  REQUIRE(t.originNs==s.base+37*GST_MSECOND);
  REQUIRE(t.samplesEnd==960 && !t.invalid);
  // Same common epoch as video, retaining the intentional 37-ms audio lag.
  auto video=gst_segment_to_running_time(&s,GST_FORMAT_TIME,s.start);
  REQUIRE(t.originNs-video==37*GST_MSECOND);
  // Negative control: raw PTS would falsely add start-base = three seconds.
  REQUIRE((s.start+37*GST_MSECOND)-video!=37*GST_MSECOND);
  // Delivery deadlines must use running-time, not raw PTS. Force >100-ms late.
  t.playbackClock=gst_system_clock_obtain();
  t.deliveryBase=gst_clock_get_time(t.playbackClock)-(s.base+57*GST_MSECOND)-200*GST_MSECOND;
  REQUIRE(buffer(t,s.start+57*GST_MSECOND)==GST_PAD_PROBE_OK);
  REQUIRE(t.lateBlocks==1);
  // Rebased segments may change raw PTS while maintaining running-time continuity.
  av::AudioTimeline rebase;segment(rebase,s);buffer(rebase,s.start);
  GstSegment next=s;next.start=9*GST_SECOND;next.base=s.base+10*GST_MSECOND;
  segment(rebase,next);REQUIRE(buffer(rebase,next.start)==GST_PAD_PROBE_OK);
  next.base=0;segment(rebase,next);REQUIRE(buffer(rebase,next.start)==GST_PAD_PROBE_DROP);
  av::AudioTimeline missing;REQUIRE(buffer(missing,0)==GST_PAD_PROBE_DROP);
  av::AudioTimeline before;segment(before,s);REQUIRE(buffer(before,s.start-1)==GST_PAD_PROBE_DROP);
  av::AudioTimeline reset;segment(reset,s);buffer(reset,s.start);REQUIRE(buffer(reset,s.start)==GST_PAD_PROBE_DROP);
  for(double rate: {0.5,2.0,-1.0}) {av::AudioTimeline bad;s.rate=rate;REQUIRE(segment(bad,s)==GST_PAD_PROBE_DROP && bad.invalid);}
  s.rate=1.0;s.applied_rate=2.0;av::AudioTimeline applied;REQUIRE(segment(applied,s)==GST_PAD_PROBE_DROP);
  gst_segment_init(&s,GST_FORMAT_BYTES);av::AudioTimeline bytes;REQUIRE(segment(bytes,s)==GST_PAD_PROBE_DROP);
  std::puts("audio segment normalization, preserved offset, deadline and rejection tests passed");
}

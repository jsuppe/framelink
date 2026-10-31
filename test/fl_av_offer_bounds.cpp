// Include sample-local encoder so malformed local capabilities are checked
// before touching a socket; no GPU or GStreamer device required.
#define AV_PLAYBACK_LIBRARY 1
#include "../samples/av_apps.cpp"
int main() {
  av::PlaybackOptions badClock;badClock.clockPolicy=static_cast<av::PlaybackClockPolicy>(99);
  av::PlaybackCallbacks callbacks;std::atomic<bool> stopped{false};std::string clockError;
  if(av::runPlayback(badClock,callbacks,stopped,clockError) || clockError!="invalid playback clock policy")return 7;
  Conn unconnected;
  for(unsigned field=0;field<3;++field) for(unsigned bad : {0u,9u,0xffffffffu}) {
    flSessionCapabilities c{};c.version=1;c.videoCount=1;c.audioCount=1;
    if(field==0)c.version=bad;else if(field==1)c.videoCount=bad;else c.audioCount=bad;
    try {sendCaps(unconnected,c);return 1;}
    catch(const std::runtime_error& e) {
      if(std::string(e.what())!="invalid outgoing offer counts/version")return 2;
    }
  }
  gst_init(nullptr,nullptr);
  GstBus* bus=gst_bus_new();
  GstClock* clock=gst_system_clock_obtain();
  gst_bus_post(bus,gst_message_new_clock_lost(nullptr,clock));
  bool rejected=false;
  try {checkPlaybackBus(bus);} catch(const std::runtime_error& e) {
    rejected=std::string(e.what())=="audio clock lost: renegotiate both tracks";
  }
  gst_object_unref(clock);gst_object_unref(bus);
  if(!rejected)return 3;
  AudioTimeline timeline;timeline.rate=48000;timeline.channels=2;
  GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);
  auto* event=gst_event_new_segment(&segment);
  GstPadProbeInfo segmentInfo{};segmentInfo.type=GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM;segmentInfo.data=event;
  const auto segmentResult=trackAudio(nullptr,&segmentInfo,&timeline);
  gst_event_unref(event);
  if(segmentResult!=GST_PAD_PROBE_OK)return 6;
  for(unsigned i=0;i<3;++i) {
    auto* b=gst_buffer_new_allocate(nullptr,480*4,nullptr);
    GST_BUFFER_PTS(b)=37*GST_MSECOND+i*10*GST_MSECOND;
    GST_BUFFER_DURATION(b)=10*GST_MSECOND;
    GstPadProbeInfo info{};info.type=GST_PAD_PROBE_TYPE_BUFFER;info.data=b;
    if(trackAudio(nullptr,&info,&timeline)!=GST_PAD_PROBE_OK ||
       GST_BUFFER_PTS(b)!=37*GST_MSECOND+i*10*GST_MSECOND)return 4;
    gst_buffer_unref(b);
  }
  return timeline.invalid.load()?5:0;
}

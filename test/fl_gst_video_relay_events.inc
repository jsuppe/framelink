// The independent boundary oracle in the bin fixture checks actual transport.
// These controls qualify bounded storage and fail-closed observer semantics.
static void videoRelayEventControls(){
 using Adapter=flsample::SessionVideoRelayEvents;
 auto event=[](Adapter& a,GstEvent* e){auto r=a.observeEvent(e);gst_event_unref(e);return r;};
 auto start=[&](Adapter& a){GstSegment s;gst_segment_init(&s,GST_FORMAT_TIME);
  CHECK(event(a,gst_event_new_segment(&s))==FL_GST_SESSION_READY);};
 auto frame=[](Adapter& a,guint64 pts){auto*b=gst_buffer_new();GST_BUFFER_PTS(b)=pts;
  auto r=a.observeBuffer(b);gst_buffer_unref(b);return r;};
 // Ring storage is recycled: the bound is pending events, not stream length.
 Adapter stream;start(stream);unsigned sent=0;
 for(unsigned n=0;n<100;++n){
  CHECK(frame(stream,n*1000)==FL_GST_SESSION_READY);
  CHECK(event(stream,gst_event_new_gap(n*1000+1,1))==FL_GST_SESSION_READY);
  CHECK(stream.drain(n,[&](const FlGstVideoEvent&){CHECK(false);return FL_GST_SESSION_READY;})==FL_GST_SESSION_READY);
  CHECK(stream.drain(n+1,[&](const FlGstVideoEvent& e){
   CHECK(e.afterFrames==n+1&&e.ptsNs==n*1000+1&&e.durationNs==1);
   CHECK(stream.status()==FL_GST_SESSION_READY); // callback not under observer lock
   ++sent;return FL_GST_SESSION_READY;
  })==FL_GST_SESSION_READY);
 }
 CHECK(sent==100);CHECK(event(stream,gst_event_new_eos())==FL_GST_SESSION_READY);
 CHECK(frame(stream,100001)==FL_GST_SESSION_INVALID);
 for(unsigned mode=0;mode<10;++mode){
  Adapter a;start(a);FlGstSessionResult r=FL_GST_SESSION_READY;
  if(mode==0)r=event(a,gst_event_new_gap(1,0));
  if(mode==1)r=event(a,gst_event_new_gap(1,GST_CLOCK_TIME_NONE));
  if(mode==2)r=event(a,gst_event_new_gap(G_MAXINT64,1));
  if(mode==3){GstSegment s;gst_segment_init(&s,GST_FORMAT_TIME);s.base=1;r=event(a,gst_event_new_segment(&s));}
  if(mode==4){GstSegment s;gst_segment_init(&s,GST_FORMAT_TIME);s.rate=2;r=event(a,gst_event_new_segment(&s));}
  if(mode==5)r=event(a,gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,gst_structure_new_empty("unknown-timeline")));
  if(mode==6){for(unsigned i=0;i<32;++i)CHECK(event(a,gst_event_new_gap(i,1))==FL_GST_SESSION_READY);
   r=event(a,gst_event_new_gap(32,1));}
  if(mode==7){CHECK(event(a,gst_event_new_gap(0,1))==FL_GST_SESSION_READY);CHECK(frame(a,1)==FL_GST_SESSION_READY);
   r=a.drain(1,[](const FlGstVideoEvent&){CHECK(false);return FL_GST_SESSION_READY;});}
  if(mode==8)r=frame(a,GST_CLOCK_TIME_NONE);
  if(mode==9){CHECK(frame(a,0)==FL_GST_SESSION_READY);r=event(a,gst_event_new_stream_start("replacement"));}
  CHECK(r==FL_GST_SESSION_INVALID);
  CHECK(a.drain(0,[](const FlGstVideoEvent&){CHECK(false);return FL_GST_SESSION_READY;})==FL_GST_SESSION_INVALID);
 }
 Adapter missing;CHECK(frame(missing,0)==FL_GST_SESSION_INVALID);
 for(unsigned mode=0;mode<4;++mode){
  Adapter a;start(a);CHECK(event(a,gst_event_new_gap(0,1))==FL_GST_SESSION_READY);
  unsigned calls=0;
  if(mode==0)CHECK(event(a,gst_event_new_flush_start())==FL_GST_SESSION_CANCELLED);
  if(mode==1)CHECK(event(a,gst_event_new_flush_stop(TRUE))==FL_GST_SESSION_CANCELLED);
  if(mode==2)a.cancel();
  auto send=[&](const FlGstVideoEvent&){++calls;return FL_GST_SESSION_CANCELLED;};
  CHECK(a.drain(0,send)==FL_GST_SESSION_CANCELLED);
  CHECK(a.drain(0,send)==FL_GST_SESSION_CANCELLED);
  CHECK(calls==(mode==3?1u:0u));
 }
 puts("video event adapter: 100 recycled boundary entries; ten invalid controls, missing segment, flush/explicit/send cancellation sticky; no locked network callback");
}

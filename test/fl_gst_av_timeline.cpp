#include "framelink_session_transport.h"
#include "framelink_meta.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(coreelements);GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);
}
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
struct Pair {
 GSocketConnection *client=nullptr,*server=nullptr;
 Pair(){auto*l=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(l,nullptr,nullptr);CHECK(port);
  std::thread accept([&]{server=g_socket_listener_accept(l,nullptr,nullptr,nullptr);});auto*c=g_socket_client_new();client=g_socket_client_connect_to_host(c,"127.0.0.1",port,nullptr,nullptr);accept.join();g_object_unref(c);g_object_unref(l);CHECK(client&&server);}
 ~Pair(){g_io_stream_close(G_IO_STREAM(client),nullptr,nullptr);g_io_stream_close(G_IO_STREAM(server),nullptr,nullptr);g_object_unref(client);g_object_unref(server);}
};
static GstElement* parse(const char*text){GError*error=nullptr;auto*p=gst_parse_launch(text,&error);if(error)fprintf(stderr,"parse: %s\n",error->message);CHECK(p&&!error);return p;}
static void push(GstElement*source,guint size,guint8 value,GstClockTime pts,GstClockTime duration,GstClockTime start,double rate=1.0,bool resuming=false){
 auto*b=gst_buffer_new_allocate(nullptr,size,nullptr);gst_buffer_memset(b,0,value,size);GST_BUFFER_PTS(b)=GST_BUFFER_DTS(b)=pts;GST_BUFFER_DURATION(b)=duration;
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);segment.rate=rate;segment.start=start;segment.position=start;segment.base=500000000;
 auto*caps=gst_app_src_get_caps(GST_APP_SRC(source));auto*sample=gst_sample_new(b,caps,&segment,nullptr);auto flow=gst_app_src_push_sample(GST_APP_SRC(source),sample);auto retryUntil=g_get_monotonic_time()+100000;while(resuming&&flow==GST_FLOW_FLUSHING&&g_get_monotonic_time()<retryUntil){g_usleep(1000);flow=gst_app_src_push_sample(GST_APP_SRC(source),sample);}if(flow!=GST_FLOW_OK)fprintf(stderr,"push %s pts=%" G_GUINT64_FORMAT " flow=%s\n",GST_OBJECT_NAME(source),pts,gst_flow_get_name(flow));CHECK(flow==GST_FLOW_OK);gst_sample_unref(sample);gst_caps_unref(caps);gst_buffer_unref(b);
}
static void run(bool audioLeads,int negative=0,bool gap=false,bool empty=false,int pause=0){
 fprintf(stderr,"CASE lead=%d negative=%d gap=%d empty=%d pause=%d\n",audioLeads,negative,gap,empty,pause);
 Pair audio,events;auto*sender=fl_gst_session_new(3);auto*receiver=fl_gst_session_new(3);
 flSessionConfig c{};c.version=2;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;c.videoTrackId=1;c.audioTrackId=2;c.video={FL_FORMAT_RGBA8,32,16,50,1};c.videoQueueFrames=2;c.audio={FL_PCM_S16,48000,2};c.audioQueueMs=80;c.connectTimeoutMs=2000;c.idleTimeoutMs=2000;
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=caps.audioCount=1;caps.video[0]=c.video;caps.audio[0]=c.audio;
 CHECK(fl_gst_session_configure(sender,&c,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(receiver,&c,&caps,&caps)==FL_GST_SESSION_READY);
 flClockMapping mapping{};mapping.sourceClockId=c.clockId;mapping.targetClockId.bytes[0]=3;mapping.targetAnchorNs=negative==2?G_MAXINT64-510000000:100000000;if(negative!=1)CHECK(fl_gst_session_set_mapping(receiver,1,&mapping,0)==FL_GST_SESSION_READY);
 guint8 secret[32];memset(secret,6,32);auto*key=g_bytes_new(secret,32);gboolean ok=FALSE;std::thread auth([&]{ok=fl_gst_session_authenticate_audio_stream(sender,audio.server,TRUE,key,1000,nullptr,nullptr);});CHECK(fl_gst_session_authenticate_audio_stream(receiver,audio.client,FALSE,key,1000,nullptr,nullptr));auth.join();CHECK(ok);
 if(gap){std::thread authEvents([&]{ok=fl_gst_session_authenticate_event_stream(sender,events.server,TRUE,key,1000,nullptr,nullptr);});CHECK(fl_gst_session_authenticate_event_stream(receiver,events.client,FALSE,key,1000,nullptr,nullptr));authEvents.join();CHECK(ok);}
 g_bytes_unref(key);
 std::string name="m5b-timeline-"+std::to_string(g_get_monotonic_time());flChannel*consumer=nullptr,*producer=nullptr;CHECK(flCreateChannelEx(name.c_str(),32,16,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&consumer)==FL_OK);CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 auto*rx=parse("framelinksrc name=video width=32 height=16 format=RGBA ! appsink name=vout sync=false async=false framelinkaudiosrc name=audio tracks=av ! appsink name=aout sync=false async=false");
 auto*tx=parse("appsrc name=vin is-live=true format=time handle-segment-change=true caps=video/x-raw,format=RGBA,width=32,height=16,framerate=50/1 ! framelinksink name=video sync=false async=false appsrc name=ain is-live=true format=time handle-segment-change=true caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av");
 auto*vs=gst_bin_get_by_name(GST_BIN(tx),"video");auto*vr=gst_bin_get_by_name(GST_BIN(rx),"video");gboolean adopted=FALSE;g_signal_emit_by_name(vs,"adopt-channel",producer,&adopted);CHECK(adopted);g_signal_emit_by_name(vr,"adopt-channel",consumer,&adopted);CHECK(adopted);
 auto*context=fl_gst_session_create_context(sender);gst_element_set_context(tx,context);gst_context_unref(context);context=fl_gst_session_create_context(receiver);gst_element_set_context(rx,context);gst_context_unref(context);
 auto*vin=gst_bin_get_by_name(GST_BIN(tx),"vin");auto*ain=gst_bin_get_by_name(GST_BIN(tx),"ain");auto*vout=gst_bin_get_by_name(GST_BIN(rx),"vout");auto*aout=gst_bin_get_by_name(GST_BIN(rx),"aout");
 guint64 videoGap[2]={GST_CLOCK_TIME_NONE,0};auto*pad=gst_element_get_static_pad(vout,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,[](GstPad*,GstPadProbeInfo*i,gpointer data)->GstPadProbeReturn{if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(i))==GST_EVENT_GAP){auto*out=static_cast<guint64*>(data);gst_event_parse_gap(GST_PAD_PROBE_INFO_EVENT(i),&out[0],&out[1]);}return GST_PAD_PROBE_OK;},videoGap,nullptr);gst_object_unref(pad);
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 for(guint n=0;n<(empty?0u:10u);n++){
  auto voffset=audioLeads?37000000u:0u,aoffset=audioLeads?0u:37000000u;
  if(gap&&n==5){for(auto*in:{vin,ain}){auto*pad=gst_element_get_static_pad(in,"src");CHECK(gst_pad_push_event(pad,gst_event_new_gap((in==vin?5:2)*GST_SECOND+(in==vin?voffset:aoffset)+n*20000000,40000000)));gst_object_unref(pad);}}
  guint64 shift=gap&&n>=5?40000000:0;
  push(vin,32*16*4,guint8(n+10),5*GST_SECOND+voffset+n*20000000+shift,20000000,5*GST_SECOND,negative==3?2.0:1.0,pause==1&&n==5);
  if(!negative)push(ain,3840,guint8(n+20),2*GST_SECOND+aoffset+n*20000000+shift,20000000,2*GST_SECOND,1.0,pause==1&&n==5);
  if(pause==2&&n==4){
   auto begin=g_get_monotonic_time();

   CHECK(gst_element_set_state(rx,GST_STATE_PAUSED)!=GST_STATE_CHANGE_FAILURE);

   CHECK(gst_element_get_state(rx,nullptr,nullptr,GST_SECOND)!=GST_STATE_CHANGE_FAILURE);
   CHECK(g_get_monotonic_time()-begin<1500000);g_usleep(50000);
   CHECK(fl_gst_session_get_config(sender,&c)==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_get_config(receiver,&c)==FL_GST_SESSION_READY);
   CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);

   CHECK(gst_element_get_state(rx,nullptr,nullptr,GST_SECOND)==GST_STATE_CHANGE_SUCCESS);

  }
  if(negative){auto*bus=gst_element_get_bus(negative==3?tx:rx);auto*err=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GST_MESSAGE_ERROR);CHECK(err);gst_message_unref(err);gst_object_unref(bus);break;}
  auto*v=gst_app_sink_try_pull_sample(GST_APP_SINK(vout),2*GST_SECOND);auto*a=gst_app_sink_try_pull_sample(GST_APP_SINK(aout),2*GST_SECOND);
  if(!v||!a)for(auto*p:{tx,rx}){auto*bus=gst_element_get_bus(p);while(auto*m=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR)){GError*error=nullptr;gst_message_parse_error(m,&error,nullptr);fprintf(stderr,"%s: %s\n",GST_OBJECT_NAME(m->src),error->message);g_clear_error(&error);gst_message_unref(m);}gst_object_unref(bus);}
  if(!v||!a)fprintf(stderr,"delivery n=%u pause=%d video=%d audio=%d\n",n,pause,v!=nullptr,a!=nullptr);
  if(!v||!a){
   gst_app_src_end_of_stream(GST_APP_SRC(vin));auto*bus=gst_element_get_bus(tx);
   auto deadline=g_get_monotonic_time()+500000;
   while(g_get_monotonic_time()<deadline){auto*m=gst_bus_timed_pop_filtered(bus,50000000,GST_MESSAGE_ELEMENT);if(!m)continue;auto*st=gst_message_get_structure(m);if(st){auto*text=gst_structure_to_string(st);fprintf(stderr,"stats: %s\n",text);g_free(text);}gst_message_unref(m);}
   gst_object_unref(bus);
  }
  CHECK(v&&a);auto*vb=gst_sample_get_buffer(v);auto*ab=gst_sample_get_buffer(a);CHECK(GST_BUFFER_PTS(vb)==600000000+voffset+n*20000000+shift);CHECK(GST_BUFFER_PTS(ab)==600000000+aoffset+n*20000000+shift);CHECK(gint64(GST_BUFFER_PTS(ab))-gint64(GST_BUFFER_PTS(vb))==(audioLeads?-37000000:37000000));CHECK(GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb)&&GST_BUFFER_DTS(ab)==GST_BUFFER_PTS(ab));CHECK(GST_BUFFER_DURATION(vb)==20000000&&GST_BUFFER_DURATION(ab)==20000000);
  auto*meta=gst_buffer_get_framelink_frame_meta(vb);CHECK(meta&&meta->frame.streamGeneration==1&&meta->frame.dtsNs==gint64(GST_BUFFER_DTS(vb)));
  for(auto*b:{vb,ab}){GstMapInfo map{};CHECK(gst_buffer_map(b,&map,GST_MAP_READ));guint8 expected=b==vb?guint8(n+10):guint8(n+20);for(gsize i=0;i<map.size;i++)CHECK(map.data[i]==expected);gst_buffer_unmap(b,&map);}
  if(gap&&n==5)CHECK(videoGap[0]==600000000+voffset+n*20000000&&videoGap[1]==40000000);
  gst_sample_unref(v);gst_sample_unref(a);
  if(pause==1&&n==4){
   auto begin=g_get_monotonic_time();
   CHECK(gst_element_set_state(tx,GST_STATE_PAUSED)!=GST_STATE_CHANGE_FAILURE);
   CHECK(gst_element_set_state(rx,GST_STATE_PAUSED)!=GST_STATE_CHANGE_FAILURE);
   CHECK(gst_element_get_state(tx,nullptr,nullptr,GST_SECOND)!=GST_STATE_CHANGE_FAILURE);
   CHECK(gst_element_get_state(rx,nullptr,nullptr,GST_SECOND)!=GST_STATE_CHANGE_FAILURE);
   CHECK(g_get_monotonic_time()-begin<1500000);g_usleep(50000);
   CHECK(fl_gst_session_get_config(sender,&c)==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_get_config(receiver,&c)==FL_GST_SESSION_READY);
   CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
   CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
   CHECK(gst_element_get_state(rx,nullptr,nullptr,GST_SECOND)==GST_STATE_CHANGE_SUCCESS);
   CHECK(gst_element_get_state(tx,nullptr,nullptr,GST_SECOND)==GST_STATE_CHANGE_SUCCESS);
  }


 }
 if(!negative){CHECK(gst_app_src_end_of_stream(GST_APP_SRC(ain))==GST_FLOW_OK);CHECK(gst_app_src_end_of_stream(GST_APP_SRC(vin))==GST_FLOW_OK);
 auto*bus=gst_element_get_bus(rx);auto*done=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));if(done&&GST_MESSAGE_TYPE(done)==GST_MESSAGE_ERROR){GError*e=nullptr;gst_message_parse_error(done,&e,nullptr);fprintf(stderr,"EOS error: %s\n",e->message);g_clear_error(&e);}CHECK(done&&GST_MESSAGE_TYPE(done)==GST_MESSAGE_EOS);gst_message_unref(done);gst_object_unref(bus);}
 gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);for(auto*e:{vs,vr,vin,ain,vout,aout,tx,rx})gst_object_unref(e);g_object_unref(sender);g_object_unref(receiver);
}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
#else
 CHECK(argc>1);GError*error=nullptr;auto*p=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(p&&!error);gst_object_unref(p);
#endif
 for(const char*name:{"framelinksink","framelinksrc","framelinkaudiosink","framelinkaudiosrc"}){
  auto*element=gst_element_factory_make(name,nullptr);CHECK(element);
  auto*wrong=gst_context_new(FL_GST_SESSION_CONTEXT,TRUE);
  gst_structure_set(gst_context_writable_structure(wrong),"coordinator",G_TYPE_STRING,"invalid",nullptr);
  gst_element_set_context(element,wrong);gst_context_unref(wrong);
  CHECK(gst_element_set_state(element,GST_STATE_PAUSED)==GST_STATE_CHANGE_FAILURE);
  gst_element_set_state(element,GST_STATE_NULL);gst_object_unref(element);
 }
 run(false);run(true);run(false,1);run(false,2);run(false,3);run(false,0,true);run(true,0,true);run(false,0,true,true);run(false,0,true,false,1);run(false,0,true,false,2);puts("coordinated public A/V: distinct TIME segments, one receiver mapping, exact +/-37ms offset, pixels/PCM/DTS/duration/generation passed");return 0;
}

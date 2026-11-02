#include "framelink_session_transport.h"
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
static void run(guint format,guint channels,guint rate,int negative,bool segmented=false,bool empty=false){
 Pair pair;auto*sender=fl_gst_session_new(2);auto*receiver=fl_gst_session_new(2);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.audioTrackId=2;
 cfg.audio={format,rate,channels};cfg.audioQueueMs=80;cfg.connectTimeoutMs=1000;cfg.idleTimeoutMs=1000;
 flSessionCapabilities caps{};caps.version=2;caps.audioCount=1;caps.audio[0]=cfg.audio;
 CHECK(fl_gst_session_configure(sender,&cfg,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(receiver,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 if(segmented){flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=7;mapping.sourceAnchorNs=cfg.epochNs;mapping.targetAnchorNs=100000000;CHECK(fl_gst_session_set_mapping(receiver,1,&mapping,0)==FL_GST_SESSION_READY);}
 CHECK(!fl_gst_session_claim_audio_stream(sender,TRUE,1));
 guint8 secret[32];memset(secret,6,sizeof secret);auto*key=g_bytes_new(secret,sizeof secret);
 gboolean authenticated=FALSE;std::thread auth([&]{authenticated=fl_gst_session_authenticate_audio_stream(sender,pair.server,TRUE,key,1000,nullptr,nullptr);});
 CHECK(fl_gst_session_authenticate_audio_stream(receiver,pair.client,FALSE,key,1000,nullptr,nullptr));auth.join();CHECK(authenticated);g_bytes_unref(key);
 auto*rx=parse("framelinkaudiosrc name=input ! appsink name=output sync=false async=false");
 auto*input=gst_bin_get_by_name(GST_BIN(rx),"input");auto*output=gst_bin_get_by_name(GST_BIN(rx),"output");
 auto*rxContext=fl_gst_session_create_context(receiver);gst_element_set_context(rx,rxContext);gst_context_unref(rxContext);
 auto*tx=parse("appsrc name=input is-live=true format=time ! framelinkaudiosink name=transport");
 auto*source=gst_bin_get_by_name(GST_BIN(tx),"input");auto*transport=gst_bin_get_by_name(GST_BIN(tx),"transport");
 if(negative==1){auto*pad=gst_element_get_static_pad(source,"src");
  // GstBaseSrc supplies zero on an untimestamped first buffer; remove PTS at
  // the actual transport boundary so this tests the sink, not BaseSrc defaults.
  gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad*,GstPadProbeInfo*i,gpointer)->GstPadProbeReturn{
   auto*b=gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(i));GST_PAD_PROBE_INFO_DATA(i)=b;GST_BUFFER_PTS(b)=GST_CLOCK_TIME_NONE;return GST_PAD_PROBE_OK;
  },nullptr,nullptr);gst_object_unref(pad);
 }
 auto*context=fl_gst_session_create_context(sender);gst_element_set_context(tx,context);gst_context_unref(context);
 auto*pcm=gst_caps_new_simple("audio/x-raw","format",G_TYPE_STRING,negative==3?"U8":format==FL_PCM_S16?"S16LE":"F32LE","layout",G_TYPE_STRING,"interleaved","channels",G_TYPE_INT,int(channels),"rate",G_TYPE_INT,int(rate),nullptr);
 if(channels==2)gst_caps_set_simple(pcm,"channel-mask",GST_TYPE_BITMASK,guint64(3),nullptr);
 g_object_set(source,"caps",pcm,nullptr);gst_caps_unref(pcm);
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(!fl_gst_session_claim_audio_stream(sender,TRUE,1)); // authenticated stream cannot be shared by two sinks
 if(segmented)g_object_set(source,"handle-segment-change",TRUE,nullptr);
 guint64 observedGap[2]={GST_CLOCK_TIME_NONE,0};
 if(segmented){auto*pad=gst_element_get_static_pad(output,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,[](GstPad*,GstPadProbeInfo*i,gpointer p)->GstPadProbeReturn{if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(i))==GST_EVENT_GAP){auto*values=static_cast<guint64*>(p);gst_event_parse_gap(GST_PAD_PROBE_INFO_EVENT(i),&values[0],&values[1]);}return GST_PAD_PROBE_OK;},observedGap,nullptr);gst_object_unref(pad);}
 const guint samples=segmented?1000:rate/100,bytes=samples*channels*(format==FL_PCM_S16?2:4);
 std::vector<guint8> expected(bytes);for(guint i=0;i<bytes;i++)expected[i]=guint8(i*31);
 for(guint n=0;n<(empty?0u:negative?1u:5u);n++){
  guint size=negative==2?bytes*9:negative==4?bytes-1:bytes;auto*b=gst_buffer_new_allocate(nullptr,size,nullptr);
  if(size==bytes)gst_buffer_fill(b,0,expected.data(),bytes);
  guint64 delta=gst_util_uint64_scale(guint64(n)*samples,GST_SECOND,rate),next=gst_util_uint64_scale(guint64(n+1)*samples,GST_SECOND,rate);
  if(segmented&&n==2){auto*pad=gst_element_get_static_pad(source,"src");CHECK(gst_pad_push_event(pad,gst_event_new_gap(2*GST_SECOND+37000000+delta,50000000)));gst_object_unref(pad);}
  guint64 gapShift=segmented&&n>=2?50000000:0;
  GST_BUFFER_PTS(b)=negative==1?GST_CLOCK_TIME_NONE:(segmented?2*GST_SECOND:0)+37000000+delta+gapShift;
  GST_BUFFER_DURATION(b)=negative==2?90000000:next-delta;
  if(segmented){GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b);if(n==2)GST_BUFFER_FLAG_SET(b,GST_BUFFER_FLAG_DISCONT);GstSegment seg;gst_segment_init(&seg,GST_FORMAT_TIME);seg.start=2*GST_SECOND;seg.position=seg.start;seg.base=500000000;auto*caps=gst_app_src_get_caps(GST_APP_SRC(source));auto*sample=gst_sample_new(b,caps,&seg,nullptr);CHECK(gst_app_src_push_sample(GST_APP_SRC(source),sample)==GST_FLOW_OK);gst_sample_unref(sample);gst_caps_unref(caps);gst_buffer_unref(b);}
  else CHECK(gst_app_src_push_buffer(GST_APP_SRC(source),b)==GST_FLOW_OK);
  if(negative==5||negative==6){
   auto*valid=gst_app_sink_try_pull_sample(GST_APP_SINK(output),2*GST_SECOND);CHECK(valid);gst_sample_unref(valid);
   auto*pad=gst_element_get_static_pad(source,"src");GstSegment invalid;gst_segment_init(&invalid,GST_FORMAT_TIME);invalid.rate=negative==5?2.0:1.0;invalid.base=999;
   gst_pad_push_event(pad,gst_event_new_segment(&invalid));gst_object_unref(pad);
  }
  if(!negative){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(output),2*GST_SECOND);if(!sample){for(auto*p:{tx,rx}){auto*debugBus=gst_element_get_bus(p);while(auto*m=gst_bus_pop_filtered(debugBus,GST_MESSAGE_ERROR)){GError*e=nullptr;gchar*detail=nullptr;gst_message_parse_error(m,&e,&detail);fprintf(stderr,"%s: %s / %s\n",GST_OBJECT_NAME(m->src),e->message,detail?detail:"");g_clear_error(&e);g_free(detail);gst_message_unref(m);}gst_object_unref(debugBus);}}CHECK(sample);
   auto*got=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(got)==(segmented?600000000:0)+37000000+delta+gapShift);CHECK(GST_BUFFER_DURATION(got)==next-delta);if(segmented){CHECK(GST_BUFFER_DTS(got)==GST_BUFFER_PTS(got));if(n==2){CHECK(GST_BUFFER_FLAG_IS_SET(got,GST_BUFFER_FLAG_DISCONT));CHECK(observedGap[0]==637000000+delta&&observedGap[1]==50000000);}}CHECK(gst_buffer_get_size(got)==bytes);
   GstMapInfo map{};CHECK(gst_buffer_map(got,&map,GST_MAP_READ));CHECK(!memcmp(map.data,expected.data(),bytes));gst_buffer_unmap(got,&map);gst_sample_unref(sample);
  }
 }
 auto*bus=gst_element_get_bus(tx);
 if(negative){auto*error=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GST_MESSAGE_ERROR);CHECK(error);gst_message_unref(error);}
 else {CHECK(gst_app_src_end_of_stream(GST_APP_SRC(source))==GST_FLOW_OK);auto*done=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(done);if(GST_MESSAGE_TYPE(done)==GST_MESSAGE_ERROR){GError*e=nullptr;gst_message_parse_error(done,&e,nullptr);fprintf(stderr,"pipeline error %s\n",e->message);}CHECK(GST_MESSAGE_TYPE(done)==GST_MESSAGE_EOS);gst_message_unref(done);}
 if(empty)CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(output),0));
 if(!negative){auto*rb=gst_element_get_bus(rx);auto*m=gst_bus_timed_pop_filtered(rb,2*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS);gst_message_unref(m);gst_object_unref(rb);}
 gst_object_unref(bus);gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);
 gst_object_unref(source);gst_object_unref(transport);gst_object_unref(input);gst_object_unref(output);gst_object_unref(tx);gst_object_unref(rx);g_object_unref(sender);g_object_unref(receiver);
}
static void local(bool first,bool missing,bool cancel){
 auto*root=g_dir_make_tmp("fl-audio-src-XXXXXX",nullptr);CHECK(root);std::string path=std::string(root)+"/session";g_free(root);auto*quoted=g_strescape(path.c_str(),nullptr);
 std::string rxText="framelinkaudiosrc name=transport session=\""+std::string(quoted)+"\" connect-timeout-ms="+(missing?"50":"2000")+" idle-timeout-ms=1000 ! appsink name=out sync=true async=false";
 std::string txText="appsrc name=in is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink session=\""+std::string(quoted)+"\" connect-timeout-ms=2000 idle-timeout-ms=1000";g_free(quoted);
 auto*rx=parse(rxText.c_str());auto*tx=parse(txText.c_str());auto*in=gst_bin_get_by_name(GST_BIN(tx),"in");auto*out=gst_bin_get_by_name(GST_BIN(rx),"out");
 auto start=g_get_monotonic_time();
 if(!first&&!missing&&!cancel)CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(g_get_monotonic_time()-start<500000);
 if(missing){auto*bus=gst_element_get_bus(rx);auto*m=gst_bus_timed_pop_filtered(bus,GST_SECOND,GST_MESSAGE_ERROR);CHECK(m);gst_message_unref(m);gst_object_unref(bus);}
 else if(!cancel){if(first)CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  for(guint n=0;n<5;n++){auto*b=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(b,0,n+10,192);GST_BUFFER_PTS(b)=37000000+n*1000000;GST_BUFFER_DURATION(b)=1000000;
   CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),b)==GST_FLOW_OK);
   // Each sub-4096-byte packet must arrive before we send another packet/EOS.
   auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),GST_SECOND);CHECK(sample);auto*got=gst_sample_get_buffer(sample);auto*clock=gst_element_get_clock(rx);CHECK(clock);CHECK(gst_clock_get_time(clock)-gst_element_get_base_time(rx)+GST_MSECOND>=GST_BUFFER_PTS(got));gst_object_unref(clock);CHECK(gst_buffer_get_size(got)==192&&GST_BUFFER_PTS(got)==37000000+n*1000000&&GST_BUFFER_DURATION(got)==1000000);GstMapInfo map{};CHECK(gst_buffer_map(got,&map,GST_MAP_READ));for(gsize j=0;j<map.size;j++)CHECK(map.data[j]==n+10);gst_buffer_unmap(got,&map);gst_sample_unref(sample);
  }
  auto*transport=gst_bin_get_by_name(GST_BIN(rx),"transport");auto*q=gst_query_new_latency();CHECK(gst_element_query(transport,q));gboolean live=FALSE;GstClockTime lo=0,hi=0;gst_query_parse_latency(q,&live,&lo,&hi);CHECK(live);gst_query_unref(q);gst_object_unref(transport);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(in))==GST_FLOW_OK);auto*bus=gst_element_get_bus(rx);auto*m=gst_bus_timed_pop_filtered(bus,GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS);gst_message_unref(m);gst_object_unref(bus);
 }
 start=g_get_monotonic_time();gst_element_set_state(rx,GST_STATE_NULL);gst_element_set_state(tx,GST_STATE_NULL);CHECK(g_get_monotonic_time()-start<1000000);gst_object_unref(in);gst_object_unref(out);gst_object_unref(rx);gst_object_unref(tx);
}
static void hostile(int mode){
 Pair pair;auto*sender=fl_gst_session_new(2);auto*receiver=fl_gst_session_new(2);
 flSessionConfig c{};c.version=2;c.sessionId.bytes[0]=9;c.clockId.bytes[0]=2;c.generation=1;c.audioTrackId=2;c.audio={FL_PCM_S16,48000,2};c.audioQueueMs=80;c.connectTimeoutMs=1000;c.idleTimeoutMs=1000;
 flSessionCapabilities caps{};caps.version=2;caps.audioCount=1;caps.audio[0]=c.audio;
 CHECK(fl_gst_session_configure(sender,&c,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(receiver,&c,&caps,&caps)==FL_GST_SESSION_READY);
 guint8 bytes[32];memset(bytes,6,sizeof bytes);auto*key=g_bytes_new(bytes,32);gboolean ok=FALSE;std::thread auth([&]{ok=fl_gst_session_authenticate_audio_stream(sender,pair.server,TRUE,key,1000,nullptr,nullptr);});CHECK(fl_gst_session_authenticate_audio_stream(receiver,pair.client,FALSE,key,1000,nullptr,nullptr));auth.join();CHECK(ok);g_bytes_unref(key);
 auto*rx=parse("framelinkaudiosrc ! fakesink sync=false async=false");auto*context=fl_gst_session_create_context(receiver);gst_element_set_context(rx,context);gst_context_unref(context);CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstElement*tx=nullptr;GstElement*in=nullptr;
 if(mode==2){
  tx=parse("appsrc name=in is-live=true format=time caps=audio/x-raw,format=U8,layout=interleaved,rate=48000,channels=2 ! gdppay ! giostreamsink name=wire sync=false async=false");
  auto*wire=gst_bin_get_by_name(GST_BIN(tx),"wire");g_object_set(wire,"stream",g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),nullptr);
  auto*pad=gst_element_get_static_pad(wire,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,[](GstPad*,GstPadProbeInfo*i,gpointer)->GstPadProbeReturn{if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(i))==GST_EVENT_SEGMENT){GstSegment seg;gst_segment_init(&seg,GST_FORMAT_TIME);gst_event_unref(GST_PAD_PROBE_INFO_EVENT(i));GST_PAD_PROBE_INFO_DATA(i)=gst_event_new_segment(&seg);}return GST_PAD_PROBE_OK;},nullptr,nullptr);gst_object_unref(pad);gst_object_unref(wire);
  in=gst_bin_get_by_name(GST_BIN(tx),"in");CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);auto*b=gst_buffer_new_allocate(nullptr,192,nullptr);GST_BUFFER_PTS(b)=37000000;GST_BUFFER_DURATION(b)=1000000;CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),b)==GST_FLOW_OK);
 }else if(mode==0){guint8 header[62]{};header[0]=1;GST_WRITE_UINT16_BE(header+4,1);GST_WRITE_UINT32_BE(header+6,0x7fffffffu);CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),header,sizeof header,nullptr,nullptr,nullptr));}
 else if(mode==1){guint8 partial[10]={1};CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),partial,sizeof partial,nullptr,nullptr,nullptr));g_io_stream_close(G_IO_STREAM(pair.server),nullptr,nullptr);}
 // mode3 deliberately keeps the authenticated socket idle.
 auto*bus=gst_element_get_bus(rx);auto*m=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GST_MESSAGE_ERROR);CHECK(m);GError*error=nullptr;gst_message_parse_error(m,&error,nullptr);CHECK(error);if(mode==0)CHECK(strstr(error->message,"bounds"));if(mode==2)CHECK(strstr(error->message,"caps"));g_clear_error(&error);gst_message_unref(m);gst_object_unref(bus);
 auto start=g_get_monotonic_time();gst_element_set_state(rx,GST_STATE_NULL);if(tx){gst_element_set_state(tx,GST_STATE_NULL);gst_object_unref(in);gst_object_unref(tx);}CHECK(g_get_monotonic_time()-start<1000000);gst_object_unref(rx);g_object_unref(sender);g_object_unref(receiver);
}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
#else
 CHECK(argc>1);GError*error=nullptr;auto*p=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(p&&!error);gst_object_unref(p);
#endif
 for(guint format:{guint(FL_PCM_S16),guint(FL_PCM_F32)})for(guint channels:{1u,2u})run(format,channels,44100,0);
 run(FL_PCM_F32,2,44100,0,true);
 run(FL_PCM_S16,2,48000,0,false,true);
 run(FL_PCM_S16,2,48000,5,true);
 run(FL_PCM_S16,2,48000,6,true);
 local(true,false,false);local(false,false,false);local(true,true,false);local(true,false,true);
 for(int mode=0;mode<4;mode++)hostile(mode);
 puts("public audio source: exact four PCM formats/layouts, incremental packet delivery before EOS, both startup orders, live latency, missing-peer and cancellation passed");return 0;
}

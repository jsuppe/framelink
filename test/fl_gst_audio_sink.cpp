#include "framelink_session_transport.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <thread>
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
static void run(guint format,guint channels,guint rate,int negative){
 Pair pair;auto*sender=fl_gst_session_new(2);auto*receiver=fl_gst_session_new(2);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.audioTrackId=2;
 cfg.audio={format,rate,channels};cfg.audioQueueMs=80;cfg.connectTimeoutMs=1000;cfg.idleTimeoutMs=1000;
 flSessionCapabilities caps{};caps.version=2;caps.audioCount=1;caps.audio[0]=cfg.audio;
 CHECK(fl_gst_session_configure(sender,&cfg,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(receiver,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 CHECK(!fl_gst_session_claim_audio_stream(sender,TRUE,1));
 guint8 secret[32];memset(secret,6,sizeof secret);auto*key=g_bytes_new(secret,sizeof secret);
 gboolean authenticated=FALSE;std::thread auth([&]{authenticated=fl_gst_session_authenticate_audio_stream(sender,pair.server,TRUE,key,1000,nullptr,nullptr);});
 CHECK(fl_gst_session_authenticate_audio_stream(receiver,pair.client,FALSE,key,1000,nullptr,nullptr));auth.join();CHECK(authenticated);g_bytes_unref(key);
 auto*rx=parse("giostreamsrc name=input blocksize=1 ! gdpdepay ! appsink name=output sync=false async=false");
 auto*input=gst_bin_get_by_name(GST_BIN(rx),"input");auto*output=gst_bin_get_by_name(GST_BIN(rx),"output");
 auto*stream=fl_gst_session_claim_audio_stream(receiver,FALSE,1);CHECK(stream);g_object_set(input,"stream",g_io_stream_get_input_stream(G_IO_STREAM(stream)),nullptr);g_object_unref(stream);
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
 auto*pcm=gst_caps_new_simple("audio/x-raw","format",G_TYPE_STRING,format==FL_PCM_S16?"S16LE":"F32LE","layout",G_TYPE_STRING,"interleaved","channels",G_TYPE_INT,int(channels),"rate",G_TYPE_INT,int(rate),nullptr);
 if(channels==2)gst_caps_set_simple(pcm,"channel-mask",GST_TYPE_BITMASK,guint64(3),nullptr);
 g_object_set(source,"caps",pcm,nullptr);gst_caps_unref(pcm);
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(!fl_gst_session_claim_audio_stream(sender,TRUE,1)); // authenticated stream cannot be shared by two sinks
 const guint samples=rate/100,bytes=samples*channels*(format==FL_PCM_S16?2:4);
 std::vector<guint8> expected(bytes);for(guint i=0;i<bytes;i++)expected[i]=guint8(i*31);
 for(guint n=0;n<(negative?1u:5u);n++){
  guint size=negative==2?bytes*9:bytes;auto*b=gst_buffer_new_allocate(nullptr,size,nullptr);
  if(size==bytes)gst_buffer_fill(b,0,expected.data(),bytes);
  GST_BUFFER_PTS(b)=negative==1?GST_CLOCK_TIME_NONE:37000000+guint64(n)*10000000;
  GST_BUFFER_DURATION(b)=negative==2?90000000:10000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(source),b)==GST_FLOW_OK);
  if(!negative){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(output),2*GST_SECOND);if(!sample){for(auto*p:{tx,rx}){auto*debugBus=gst_element_get_bus(p);while(auto*m=gst_bus_pop_filtered(debugBus,GST_MESSAGE_ERROR)){GError*e=nullptr;gchar*detail=nullptr;gst_message_parse_error(m,&e,&detail);fprintf(stderr,"%s: %s / %s\n",GST_OBJECT_NAME(m->src),e->message,detail?detail:"");g_clear_error(&e);g_free(detail);gst_message_unref(m);}gst_object_unref(debugBus);}}CHECK(sample);
   auto*got=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(got)==37000000+guint64(n)*10000000);CHECK(GST_BUFFER_DURATION(got)==10000000);CHECK(gst_buffer_get_size(got)==bytes);
   GstMapInfo map{};CHECK(gst_buffer_map(got,&map,GST_MAP_READ));CHECK(!memcmp(map.data,expected.data(),bytes));gst_buffer_unmap(got,&map);gst_sample_unref(sample);
  }
 }
 auto*bus=gst_element_get_bus(tx);
 if(negative){auto*error=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GST_MESSAGE_ERROR);CHECK(error);gst_message_unref(error);}
 else {CHECK(gst_app_src_end_of_stream(GST_APP_SRC(source))==GST_FLOW_OK);auto*done=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(done);if(GST_MESSAGE_TYPE(done)==GST_MESSAGE_ERROR){GError*e=nullptr;gst_message_parse_error(done,&e,nullptr);fprintf(stderr,"pipeline error %s\n",e->message);}CHECK(GST_MESSAGE_TYPE(done)==GST_MESSAGE_EOS);gst_message_unref(done);}
 gst_object_unref(bus);gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);
 gst_object_unref(source);gst_object_unref(transport);gst_object_unref(input);gst_object_unref(output);gst_object_unref(tx);gst_object_unref(rx);g_object_unref(sender);g_object_unref(receiver);
}
int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
#else
 CHECK(argc==2);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif

 auto*unbound=gst_element_factory_make("framelinkaudiosink",nullptr);CHECK(unbound);CHECK(gst_element_set_state(unbound,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);gst_element_set_state(unbound,GST_STATE_NULL);gst_object_unref(unbound);
 run(FL_PCM_S16,1,48000,0);run(FL_PCM_S16,2,48000,0);run(FL_PCM_F32,1,44100,0);run(FL_PCM_F32,2,44100,0);
 run(FL_PCM_S16,2,48000,1);run(FL_PCM_S16,2,48000,2);
 puts("public audio sink: exact PCM/PTS/duration and missing-auth/invalid-PTS/oversized-block negatives passed");
}

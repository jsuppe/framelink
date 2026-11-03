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
static void run(int mode){
 Pair pair;auto*p=fl_gst_session_new(1);auto*c=fl_gst_session_new(1);flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.videoTrackId=1;cfg.video={FL_FORMAT_RGBA8,32,16,60,1};cfg.videoQueueFrames=2;cfg.connectTimeoutMs=1000;cfg.idleTimeoutMs=100;
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]=cfg.video;CHECK(fl_gst_session_configure(p,&cfg,&caps,&caps)==FL_GST_SESSION_READY);CHECK(fl_gst_session_configure(c,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 guint8 secret[32];memset(secret,6,32);auto*key=g_bytes_new(secret,32);gboolean ok=FALSE;GError*pe=nullptr,*ce=nullptr;
 std::thread server([&]{ok=fl_gst_session_authenticate_event_stream(p,pair.server,TRUE,key,1000,nullptr,&pe);});CHECK(fl_gst_session_authenticate_event_stream(c,pair.client,FALSE,key,1000,nullptr,&ce));server.join();if(pe)fprintf(stderr,"auth: %s\n",pe->message);CHECK(ok&&!pe&&!ce);g_bytes_unref(key);CHECK(fl_gst_session_has_event_stream(p,1)&&fl_gst_session_has_event_stream(c,1));
 FlGstVideoEvent event{FL_GST_VIDEO_GAP,4,500000000,20000000},out{};
 if(mode==0){
  // No periodic traffic/heartbeat is required for a quiet event channel.
  g_usleep(1100000);CHECK(fl_gst_session_receive_video_event(c,1,10,&out)==FL_GST_SESSION_TIMEOUT);
  CHECK(fl_gst_session_send_video_event(p,1,&event)==FL_GST_SESSION_READY);CHECK(fl_gst_session_receive_video_event(c,1,1000,&out)==FL_GST_SESSION_READY);CHECK(out.kind==event.kind&&out.afterFrames==4&&out.ptsNs==500000000&&out.durationNs==20000000);
  event.kind=FL_GST_VIDEO_BARRIER;event.durationNs=0;CHECK(fl_gst_session_send_video_event(p,1,&event)==FL_GST_SESSION_READY);CHECK(fl_gst_session_receive_video_event(c,1,1000,&out)==FL_GST_SESSION_READY&&out.kind==FL_GST_VIDEO_BARRIER);
  CHECK(fl_gst_session_send_video_event(c,1,&event)==FL_GST_SESSION_INVALID);event.kind=99;CHECK(fl_gst_session_send_video_event(p,1,&event)==FL_GST_SESSION_INVALID);
 }else if(mode==1){
  guint8 wire[64]{};memcpy(wire,"FLEVT001",8);GST_WRITE_UINT64_BE(wire+8,2);GST_WRITE_UINT32_BE(wire+16,FL_GST_VIDEO_GAP);GST_WRITE_UINT64_BE(wire+40,1);GST_WRITE_UINT64_BE(wire+48,1);CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),wire,sizeof wire,nullptr,nullptr,nullptr));CHECK(fl_gst_session_receive_video_event(c,1,1000,&out)==FL_GST_SESSION_CANCELLED);
 }else if(mode==2){
  for(int i=0;i<33;i++)CHECK(fl_gst_session_send_video_event(p,1,&event)==FL_GST_SESSION_READY);g_usleep(100000);int count=0;while(fl_gst_session_receive_video_event(c,1,1000,&out)==FL_GST_SESSION_READY)++count;CHECK(count==32);
 }else if(mode==3){
  FlGstSessionResult result=FL_GST_SESSION_READY;std::thread wait([&]{result=fl_gst_session_receive_video_event(c,1,5000,&out);});g_usleep(20000);auto start=g_get_monotonic_time();fl_gst_session_cancel(c);wait.join();CHECK(result==FL_GST_SESSION_CANCELLED&&g_get_monotonic_time()-start<1000000);
 }else if(mode==5){
  event.kind=FL_GST_VIDEO_FLUSH;event.durationNs=0;
  CHECK(fl_gst_session_send_video_event(p,1,&event)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_receive_video_event(c,1,1000,&out)==FL_GST_SESSION_CANCELLED);
  flSessionConfig stale{};CHECK(fl_gst_session_get_config(c,&stale)==FL_GST_SESSION_CANCELLED);
 }else{
  guint8 partial[8]{};CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),partial,sizeof partial,nullptr,nullptr,nullptr));CHECK(fl_gst_session_receive_video_event(c,1,2000,&out)==FL_GST_SESSION_CANCELLED);
 }
 auto start=g_get_monotonic_time();g_object_unref(p);g_object_unref(c);CHECK(g_get_monotonic_time()-start<1000000);
}
int main(int argc,char**argv){gst_init(&argc,&argv);for(int i=0;i<6;i++)run(i);puts("event channel: authenticated ordered bounded controls; quiet idle, stale generation, queue overflow, partial-record deadline and cancellation passed");}

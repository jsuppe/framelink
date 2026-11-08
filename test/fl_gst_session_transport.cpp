#include "framelink_session_transport.h"
#include "../runtime/av_auth.h"
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
struct Pair {
 GSocketConnection *client=nullptr,*server=nullptr;
 Pair(){auto*l=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(l,nullptr,nullptr);CHECK(port);
  std::thread accept([&]{server=g_socket_listener_accept(l,nullptr,nullptr,nullptr);});
  auto*c=g_socket_client_new();client=g_socket_client_connect_to_host(c,"127.0.0.1",port,nullptr,nullptr);
  accept.join();g_object_unref(c);g_object_unref(l);CHECK(client&&server);
 }
 ~Pair(){g_io_stream_close(G_IO_STREAM(client),nullptr,nullptr);g_io_stream_close(G_IO_STREAM(server),nullptr,nullptr);g_object_unref(client);g_object_unref(server);}
};
static flSessionConfig config(unsigned tracks){flSessionConfig c{};c.version=2;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;c.epochNs=37000000;c.connectTimeoutMs=1000;c.idleTimeoutMs=1000;
 if(tracks&1){c.videoTrackId=1;c.video={FL_FORMAT_RGBA8,16,16,60,1};c.videoQueueFrames=2;}
 if(tracks&2){c.audioTrackId=2;c.audio={FL_PCM_F32,44100,FL_LAYOUT_STEREO};c.audioQueueMs=80;}return c;}
static flSessionCapabilities offer(const flSessionConfig&c){flSessionCapabilities p{};p.version=2;if(c.videoTrackId){p.videoCount=1;p.video[0]=c.video;}if(c.audioTrackId){p.audioCount=1;p.audio[0]=c.audio;}return p;}
int main(int argc,char**argv){gst_init(&argc,&argv);guint8 raw[32];memset(raw,3,sizeof raw);auto*key=g_bytes_new(raw,sizeof raw);raw[0]=5;auto*wrong=g_bytes_new(raw,sizeof raw);
 for(unsigned tracks=1;tracks<=3;tracks++)for(unsigned mode=0;mode<4;mode++){
  Pair pair;auto*s=fl_gst_session_new(tracks);auto*c=fl_gst_session_new(tracks);auto proposal=config(tracks);auto a=offer(proposal),b=a;
  if(mode==2){b=offer(config(tracks==3?2:3));}
  if(mode==3)proposal.generation=0;
  gboolean sr=FALSE,cr=FALSE;GError*se=nullptr,*ce=nullptr;
  std::thread server([&]{sr=fl_gst_session_negotiate_stream(s,pair.server,TRUE,key,&a,&proposal,1000,nullptr,&se);});
  cr=fl_gst_session_negotiate_stream(c,pair.client,FALSE,mode==1?wrong:key,&b,nullptr,1000,nullptr,&ce);server.join();
  if(mode==0){CHECK(sr&&cr&&!se&&!ce);for(unsigned t=1;t<=2;t++)if(tracks&t){CHECK(fl_gst_session_register_track(c,t)==FL_GST_SESSION_READY);CHECK(fl_gst_session_track_ready(c,t,1)==FL_GST_SESSION_READY);}
   flSessionConfig result{};CHECK(fl_gst_session_wait(c,1,100,&result)==FL_GST_SESSION_READY);CHECK(result.epochNs==37000000&&result.audio.rate==proposal.audio.rate);
  }else{CHECK(!sr&&!cr&&se&&ce);flSessionConfig out{};CHECK(fl_gst_session_wait(c,1,1,&out)==FL_GST_SESSION_CANCELLED);}
  g_clear_error(&se);g_clear_error(&ce);g_object_unref(s);g_object_unref(c);
 }
 // Authenticated but hostile peers cannot reflect a producer proof as confirmation.
 {
  Pair pair;auto*s=fl_gst_session_new(2);auto cfg=config(2);auto caps=offer(cfg);
  gboolean result=TRUE;GError*error=nullptr;
  std::thread server([&]{result=fl_gst_session_negotiate_stream(s,pair.server,TRUE,key,&caps,&cfg,1000,nullptr,&error);});
  av::Endpoint ep{};ep.token.fill(3);std::string reason;
  CHECK(av::authenticateClient(pair.client,ep,'C',reason));
  guint8 request[308]{};memcpy(request,"FLGSTAV",7);request[7]=2;request[8]=9;
  auto*o=request+40;GST_WRITE_UINT32_LE(o,2);GST_WRITE_UINT32_LE(o+8,1);
  GST_WRITE_UINT32_LE(o+172,FL_PCM_F32);GST_WRITE_UINT32_LE(o+176,44100);GST_WRITE_UINT32_LE(o+180,FL_LAYOUT_STEREO);
  gsize done=0;CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.client)),request,sizeof request,&done,nullptr,nullptr));
  guint8 response[500]{};CHECK(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(pair.client)),response,sizeof response,&done,nullptr,nullptr)&&done==sizeof response);
  CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.client)),response+468,32,&done,nullptr,nullptr));
  server.join();CHECK(!result&&error);g_clear_error(&error);g_object_unref(s);
 }
 // A correct initial challenge does not authenticate an altered selection.
 {
  Pair pair;auto*c=fl_gst_session_new(2);auto cfg=config(2);auto caps=offer(cfg);
  std::thread server([&]{av::Endpoint ep{};ep.token.fill(3);std::string reason;
   CHECK(av::authenticateServer(pair.server,ep,'C',reason));guint8 request[308]{};gsize done=0;
   CHECK(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(pair.server)),request,sizeof request,&done,nullptr,nullptr)&&done==sizeof request);
   guint8 response[500]{};memcpy(response,request,308);response[8]=10;
   CHECK(flSessionEncode(&cfg,response+308,FL_SESSION_WIRE_SIZE)==FL_SESSION_OK);
   CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),response,sizeof response,&done,nullptr,nullptr));
  });GError*error=nullptr;
  CHECK(!fl_gst_session_negotiate_stream(c,pair.client,FALSE,key,&caps,nullptr,1000,nullptr,&error));
  server.join();CHECK(error);g_clear_error(&error);g_object_unref(c);
 }
 // PCM connection credentials bind the complete negotiated session generation.
 for(bool stale:{false,true}){
  Pair pair;auto*s=fl_gst_session_new(2);auto*c=fl_gst_session_new(2);auto cfg=config(2);auto caps=offer(cfg);
  CHECK(fl_gst_session_configure(c,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  if(stale)cfg.generation++;
  CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  gboolean sr=FALSE;GError*se=nullptr,*ce=nullptr;
  std::thread server([&]{sr=fl_gst_session_authenticate_audio_stream(s,pair.server,TRUE,key,1000,nullptr,&se);});
  auto cr=fl_gst_session_authenticate_audio_stream(c,pair.client,FALSE,key,1000,nullptr,&ce);server.join();
  CHECK(stale?(!sr&&!cr&&se&&ce):(sr&&cr&&!se&&!ce));
  g_clear_error(&se);g_clear_error(&ce);g_object_unref(s);g_object_unref(c);
 }
 // CPU bootstrap has a distinct credential domain and exact session binding.
 for(unsigned mode=0;mode<7;mode++){
  fprintf(stderr,"CPU bootstrap mode %u\n",mode);
  Pair pair;auto*s=fl_gst_session_new(3);auto*c=fl_gst_session_new(3);auto cfg=config(3);auto caps=offer(cfg);
  CHECK(fl_gst_session_configure(c,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  if(mode==2)cfg.generation++;if(mode==3)cfg.epochNs++;if(mode==4)cfg.clockId.bytes[0]++;
  CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  gboolean sr=FALSE;GError*se=nullptr,*ce=nullptr;
  std::thread server([&]{sr=fl_gst_session_authenticate_cpu_bootstrap(s,pair.server,TRUE,key,1000,nullptr,&se);});
  gboolean cr= mode==5?fl_gst_session_authenticate_event_stream(c,pair.client,FALSE,key,1000,nullptr,&ce):
   mode==6?fl_gst_session_authenticate_audio_stream(c,pair.client,FALSE,key,1000,nullptr,&ce):
   fl_gst_session_authenticate_cpu_bootstrap(c,pair.client,FALSE,mode==1?wrong:key,1000,nullptr,&ce);
  server.join();CHECK(mode?(!sr&&!cr&&se&&ce):(sr&&cr&&!se&&!ce));
  if(!mode){ // Successful auth consumes only its handshake, leaving stream usable.
   guint8 sent=73,received=0;gsize n=0;
   CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),&sent,1,&n,nullptr,nullptr)&&n==1);
   CHECK(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(pair.client)),&received,1,&n,nullptr,nullptr)&&n==1&&received==sent);
   CHECK(!fl_gst_session_has_event_stream(c,1));
  }else {flSessionConfig out{};CHECK(fl_gst_session_get_config(c,&out)==FL_GST_SESSION_CANCELLED);}
  g_clear_error(&se);g_clear_error(&ce);g_object_unref(s);g_object_unref(c);
 }
 for(bool cancel:{false,true}){
  Pair pair;auto*s=fl_gst_session_new(1);auto cfg=config(1);auto caps=offer(cfg);CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  auto*token=g_cancellable_new();GError*e=nullptr;std::thread interrupter;
  if(cancel)interrupter=std::thread([&]{g_usleep(20000);g_cancellable_cancel(token);});
  auto start=g_get_monotonic_time();CHECK(!fl_gst_session_authenticate_cpu_bootstrap(s,pair.server,TRUE,key,100,token,&e));
  CHECK(e&&g_get_monotonic_time()-start<1000000);if(interrupter.joinable())interrupter.join();
  g_clear_error(&e);g_object_unref(token);g_object_unref(s);
 }
 // A silent peer cannot keep a public element worker alive indefinitely.
 for(bool cancel:{false,true}){
  Pair pair;auto*s=fl_gst_session_new(2);auto cfg=config(2);auto caps=offer(cfg);auto*token=g_cancellable_new();GError*e=nullptr;
  std::thread interrupter;if(cancel)interrupter=std::thread([&]{g_usleep(20000);g_cancellable_cancel(token);});
  auto start=g_get_monotonic_time();CHECK(!fl_gst_session_negotiate_stream(s,pair.server,TRUE,key,&caps,&cfg,100,token,&e));
  CHECK(g_get_monotonic_time()-start<1000000);CHECK(e);
  if(interrupter.joinable())interrupter.join();g_clear_error(&e);g_object_unref(token);g_object_unref(s);
 }
 g_bytes_unref(key);g_bytes_unref(wrong);puts("authenticated session transport: all track modes, wrong key, mismatch, invalid config, timeout and cancel passed");
}

// Shared local native relay tests: one source for every platform with the
// relay (D3D11 on Windows, DMA-BUF on desktop Linux), selected by mode.
// Platform-specific parts are in fl_gst_native_relay_platform.h; CMake
// registers the same modes per platform under the platform suffix.
//   --relay-local-session         FAR4 relay setup: exact upstream media/clock/
//                                  epoch/generation/tracks, fresh hop identity,
//                                  mapping/media/cancel/alias negatives.
//   --relay-local-source-control  FAR4 relay_input against a separate source
//                                  process: source-relative mapping, exact frames
//                                  and metadata, real read leases.
#include "fl_gst_native_relay_platform.h"
#include "../samples/session_relay_contract.h"
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using relaytest::nativeAvPolicy;

// Local-discovery composition prerequisite only. No foreign-pool media forwarding.
static void nativeLocalRelaySession(){
 flSessionConfig original{};original.version=2;original.sessionId.bytes[0]=31;
 original.clockId.bytes[0]=73;original.generation=19;original.epochNs=123456789;
 original.videoTrackId=7;original.audioTrackId=8;
 original.video={FL_FORMAT_RGBA8,64,32,30,1};original.audio={FL_PCM_S16,48000,2};
 original.videoQueueFrames=1;original.audioQueueMs=80;
 original.connectTimeoutMs=original.idleTimeoutMs=1000;
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=caps.audioCount=1;
 caps.video[0]=original.video;caps.audio[0]=original.audio;
 flClockMapping mapping{};mapping.sourceClockId=original.clockId;mapping.targetClockId.bytes[0]=81;
 mapping.sourceAnchorNs=original.epochNs;mapping.targetAnchorNs=0;
 auto candidate=nativeAvPolicy(0,true);
 for(unsigned mode=0;mode<6;++mode){
  auto*up=fl_gst_session_new(3);
  CHECK(fl_gst_session_configure(up,&original,&caps,&caps)==FL_GST_SESSION_READY);
  mapping.targetAnchorNs=mode==1?1:0;
  CHECK(fl_gst_session_set_mapping(up,19,&mapping,0)==FL_GST_SESSION_READY);
  auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
  auto offered=caps;
  GstFrameLinkMemoryPolicy bound{},received{};bound.version=99;
  flChannel*p=nullptr,*c=nullptr;GError*error=nullptr,*remoteError=nullptr;
  gchar*dir=g_dir_make_tmp("framelink-relay-session-XXXXXX",nullptr);CHECK(dir);
  std::string path=std::string(dir)+"/session";g_free(dir);
  if(mode==2)offered.video[0].width=128;
  if(mode==3)fl_gst_session_cancel(up);
  if(mode<4){
   gboolean remote=FALSE;std::thread peer;
   if(!mode)peer=std::thread([&]{remote=fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,1,2,80,2000,1000,&candidate,&received,&c,nullptr,&remoteError);});
   auto ok=fl_gst_session_open_local_native_relay(s,up,path.c_str(),&offered,1,2,80,2000,1000,&candidate,&bound,&p,nullptr,&error);
   if(!mode){
    peer.join();CHECK(ok&&remote&&!error&&!remoteError&&p&&c);
    flSessionConfig a{},b{};CHECK(fl_gst_session_get_config(s,&a)==FL_GST_SESSION_READY);
    CHECK(fl_gst_session_get_config(r,&b)==FL_GST_SESSION_READY);
    CHECK(flsample::SessionRelayContract::compatible(original,a));
    guint8 x[FL_SESSION_WIRE_SIZE]{},y[FL_SESSION_WIRE_SIZE]{};
    CHECK(flSessionEncode(&a,x,sizeof x)==FL_SESSION_OK&&flSessionEncode(&b,y,sizeof y)==FL_SESSION_OK&&!memcmp(x,y,sizeof x));
    CHECK(a.generation==19&&a.epochNs==123456789&&a.videoTrackId==7&&a.audioTrackId==8);
    CHECK(bound.sessionGeneration==19&&received.sessionGeneration==19);
    flClose(p);flClose(c);
   }else CHECK(!ok&&error&&!p&&!bound.version);
  }else{
   // A cancelled setup must not claim or cancel the separately owned upstream.
   auto*cancel=g_cancellable_new();g_cancellable_cancel(cancel);
   auto ok=fl_gst_session_open_local_native_relay(mode==5?up:s,up,path.c_str(),&caps,1,2,80,2000,1000,&candidate,&bound,&p,cancel,&error);
   CHECK(!ok&&error&&!p&&!bound.version);
   flSessionConfig still{};CHECK(fl_gst_session_get_config(up,&still)==FL_GST_SESSION_READY);
   g_object_unref(cancel);
  }
  g_clear_error(&error);g_clear_error(&remoteError);g_object_unref(s);g_object_unref(r);g_object_unref(up);
 }
 puts("local relay session: exact media/clock/epoch/generation/tracks, fresh hop identity; mapping/media/cancel/alias negatives; independent pools only");
}

// Independent upstream source process for the relay fixtures. Only control
// gates are inherited, never native resources or credentials.
namespace localsource {
static constexpr unsigned Frames=8;
static gint64 pts(unsigned n){return 37000000+gint64(n)*1000000;}
static void verify(flChannel*channel,const flFrame&frame,unsigned n,guint64 generation){
 CHECK(frame.ptsNs==pts(n));
 flFrameMetadata meta{};CHECK(flGetFrameMetadata(channel,&frame,&meta)==FL_OK);
 CHECK(meta.version==FL_FRAME_METADATA_VERSION&&meta.streamGeneration==generation);
 CHECK(meta.flags==(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION)&&meta.dtsNs==pts(n)&&meta.durationNs==1000000);
 // Explicit test-only readback oracle, not a relay operation or zero-copy claim.
 flMappedImage map{};CHECK(flImageMapPlanes(frame.image,&map)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)
  CHECK(static_cast<unsigned char*>(map.plane[0].pixels)[y*map.plane[0].stride+x]==n+11);
 flImageUnmap(frame.image);
}
// Child: an ordinary FAR4 native producer, paced by the parent.
static int producer(const char*path,relaytest::Gate&advance,relaytest::Gate&published,relaytest::Gate&retired){
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native(session,TRUE,path,&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(bound.sessionGeneration==cfg.generation);
 CHECK(fl_gst_session_local_ready(session,TRUE,5000,nullptr));
 for(unsigned n=0;n<Frames;++n){
  advance.wait();
  flBuffer buffer{};CHECK(flAcquireBuffer(channel,&buffer,1000)==FL_OK);
  relaytest::fill(channel,buffer,static_cast<unsigned char>(n+11));
  flFrameMetadata meta{FL_FRAME_METADATA_VERSION,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts(n),1000000,cfg.generation,{}};
  CHECK(flSetFrameMetadata(channel,&buffer,&meta)==FL_OK);CHECK(flSubmit(channel,&buffer,pts(n))==FL_OK);
  published.signal();
  // A control gate bounds the fixture's outstanding frames; it is not proof
  // of native completion. The parent signals only after the real lease retires.
  retired.wait();
 }
 CHECK(fl_gst_session_finish_local(session,TRUE,5000,nullptr));
 flClose(channel);g_object_unref(session);return 0;
}
struct Peer {
 relaytest::Gate advance,published,retired;std::unique_ptr<relaytest::Child> child;
 Peer(const char*plugin,const std::string&path){
  child.reset(new relaytest::Child({plugin,"--relay-local-source",path,advance.arg(),published.arg(),retired.arg()},
   {&advance,&published,&retired}));
 }
 void next(){advance.signal();published.wait();}
 void release(){retired.signal();}
 void join(){child->join();}
};
// A focused upstream-only control for the separate source process.
static void control(const char*plugin){
 gchar*dir=g_dir_make_tmp("framelink-local-source-XXXXXX",nullptr);CHECK(dir);
 const std::string path=std::string(dir)+"/session";g_free(dir);
 Peer source(plugin,path);
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native_relay_input(session,path.c_str(),&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(cfg.generation&&bound.sessionGeneration==cfg.generation);
 for(gint64 input:{gint64(0),gint64(37000000)}){gint64 mapped=-1;
  CHECK(fl_gst_session_map_running_time(session,cfg.generation,input,TRUE,&mapped)==FL_GST_SESSION_READY&&mapped==input);}
 CHECK(fl_gst_session_local_ready(session,FALSE,1000,nullptr));
 for(unsigned n=0;n<Frames;++n){
  source.next();flFrame frame{};CHECK(flAcquireFrame(channel,&frame,1000)==FL_OK);
  verify(channel,frame,n,cfg.generation);
  uint64_t lease=0;CHECK(flLeaseFrame(channel,&frame,&lease)==FL_OK);
  CHECK(flCompleteReadLease(channel,lease)==FL_OK);CHECK(flCollectReadLeases(channel)==FL_OK);
  source.release();
 }
 CHECK(fl_gst_session_finish_local(session,FALSE,1000,nullptr));source.join();
 flClose(channel);g_object_unref(session);
 puts("separate local source control: authenticated FAR4, 8 exact frames/metadata, no inherited native resources, real read leases before control acknowledgment");
}
}

int main(int argc,char**argv){
 gst_init(&argc,&argv);CHECK(argc>=3);
 GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
 const std::string mode=argv[2];
 if(argc==7&&mode=="--relay-local-source"){
  relaytest::Gate advance(argv[4]),published(argv[5]),retired(argv[6]);
  return localsource::producer(argv[3],advance,published,retired);
 }
 CHECK(argc==3);
 if(mode=="--relay-local-session"){nativeLocalRelaySession();return 0;}
 if(mode=="--relay-local-source-control"){localsource::control(argv[1]);return 0;}
 fprintf(stderr,"unknown mode %s\n",mode.c_str());return 2;
}

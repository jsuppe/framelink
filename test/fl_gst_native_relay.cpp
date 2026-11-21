// Shared local native relay tests: one source for every platform with the
// relay (D3D11 on Windows, DMA-BUF on desktop Linux, AHardwareBuffer on
// Android), selected by mode. Android runs the FAR4 and portable control modes
// until its FAR5 parity work; the FAR5 modes need FL_GST_LOCAL_NATIVE_RELAY_POOL.
// Platform-specific parts are in fl_gst_native_relay_platform.h; CMake
// registers the same modes per platform under the platform suffix.
//   --relay-local-session         FAR4 relay setup: exact upstream media/clock/
//                                  epoch/generation/tracks, fresh hop identity,
//                                  mapping/media/cancel/alias negatives.
//   --relay-local-source-control  FAR4 relay_input against a separate source
//                                  process: source-relative mapping, exact frames
//                                  and metadata, real read leases.
//   --relay-local-process         FAR5 across a process boundary: authenticated
//                                  transfer of the source's original allocations,
//                                  8 frames forwarded by allocation identity, the
//                                  source lease held until the remote reader's.
//   --relay-local-pool-failures   FAR5 malformed exports, capacity and legacy
//                                  receiver rejection, upstream cancellation.
//   --relay-local-three-process   source process -> this relay (FAR4 input,
//                                  snapshot, FAR5 output) -> final reader process.
//   --relay-three-local-av        the same graph with authenticated PCM.
//   --relay-local-replacement     a fresh complete A/V graph while the old final
//                                  reader still holds its native lease.
//   --relay-local-recovery        source death: bounded failure, no synthetic
//                                  END/EOS, old held pixels intact.
//   --relay-local-bin             the final reader is a framelinkavsrc bin
//                                  selected by set-local-native-pool-candidate.
//   --relay-local-bin-negatives   FAR5 bin profile controls and FAR4 mismatch.
//   --relay-setup-negatives       transactional setup: absent/cancelled output.
//   --video-relay-event-controls  bounded video event storage, fail-closed
//                                  observer and sticky cancellation.
//   --relay-supervisor-controls   supervisor rejection, stop and single use.
//   --relay-local-bin-pause       FAR5 receiver bin PAUSED and resume: same
//                                  session/generation, held lease kept, real EOS.
//   --relay-local-bin-paused-cancel  source cancellation while the bin is
//                                  PAUSED then NULL: no EOS, held pixels intact.
//   In-process GStreamer bin relays (fl_gst_native_bin_relay.inc): a real
//   native producer bin, a first native reader, the read-lease adapter into a
//   same-allocation ring, and a final reader bin.
//   --bin-relay                   two hops: exact pixels/PCM/37 ms, terminal.
//   --lease-relay                 the adapter alone; final reader retained past
//                                  full bin teardown, retired only after it.
//   --bin-relay-cancel / -events / -supervised-downstream / -supervised-upstream
//                                  cancellation, serialized GAP/DISCONT, and the
//                                  relay supervisor at each end.
//   --relay-pool-lifetime         100 setup cycles, no handle or fd growth.
//   --relay-authenticated-pool    broker-keyed pool transfer and its negatives.
//   --relay-local-pool / -local-cancel-downstream / -local-cancel-upstream /
//   --relay-local-events          the same over local FAR4/FAR5 discovery.
//   --relay-pool-controls         pool export guard rejections.
#include "fl_gst_native_relay_platform.h"
#include "../samples/session_relay_contract.h"
#include "../samples/native_relay.h"
#include "../samples/session_native_relay.h"
#include "../samples/session_pcm_relay.h"
#include "../samples/session_relay_supervisor.h"
#include "../samples/session_video_relay_events.h"
#include "../gst/framelink_meta.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#ifdef __ANDROID__
#include <dlfcn.h>
#endif

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

static void avTerminal(GstElement*p,GstMessageType expected){
 auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
 if(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);fprintf(stderr,"native AV: %s %s\n",e->message,d?d:"");g_clear_error(&e);g_free(d);}
 CHECK(m&&GST_MESSAGE_TYPE(m)==expected);gst_message_unref(m);gst_object_unref(bus);
}
// The appsink consumer completes its synchronous pixel oracle before releasing
// memory. Real GPU consumers must retain it through their actual completion.
static gboolean nativeAvReaderAllocation(GstElement*,GstQuery*query,gpointer data){
 auto*policy=static_cast<GstFrameLinkMemoryPolicy*>(data);
 guint8 wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];
 CHECK(flMemoryRequirementsEncode(&policy->requirements[1],wire,sizeof wire)==FL_MEMORY_OK);
 auto*bytes=g_bytes_new(wire,sizeof wire);
 gst_structure_set(gst_query_writable_structure(query),
  "framelink-reader-completion-contract",G_TYPE_STRING,"retain-memory-until-gpu-complete-v1",
  "framelink-memory-requirements",G_TYPE_BYTES,bytes,nullptr);
 g_bytes_unref(bytes);return TRUE;
}
// Borrow the already authenticated audio connection from the local session.
struct LocalProcessAudio {
 GstElement*pipe=nullptr,*port=nullptr;
 LocalProcessAudio(FlGstSession*session,bool producer){
  CHECK(fl_gst_session_register_track(session,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
  flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
  GError*error=nullptr;
  pipe=gst_parse_launch(producer?
   "appsrc name=port is-live=true format=time max-bytes=15360 caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av":
   "framelinkaudiosrc tracks=av ! appsink name=port max-buffers=2 sync=false async=false enable-last-sample=false",&error);
  CHECK(pipe&&!error);port=gst_bin_get_by_name(GST_BIN(pipe),"port");CHECK(port);
  auto*context=fl_gst_session_create_context(session);gst_element_set_context(pipe,context);gst_context_unref(context);
  CHECK(fl_gst_session_track_ready(session,FL_GST_TRACK_VIDEO,cfg.generation)==FL_GST_SESSION_READY);
 }
 void start(){CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);}
 ~LocalProcessAudio(){CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(port);gst_object_unref(pipe);}
};

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
static int producer(const char*path,relaytest::Gate&advance,relaytest::Gate&published,relaytest::Gate&retired,bool audio=false){
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native(session,TRUE,path,&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(bound.sessionGeneration==cfg.generation);
 std::unique_ptr<LocalProcessAudio> pcm;
 if(audio){pcm.reset(new LocalProcessAudio(session,true));pcm->start();}
 // The relay parent signals readiness only after snapshot import and the
 // same-allocation read-lease adapter are bound.
 CHECK(fl_gst_session_local_ready(session,TRUE,5000,nullptr));
 for(unsigned n=0;n<Frames;++n){
  advance.wait();
  flBuffer buffer{};CHECK(flAcquireBuffer(channel,&buffer,1000)==FL_OK);
  relaytest::fill(channel,buffer,static_cast<unsigned char>(n+11));
  flFrameMetadata meta{FL_FRAME_METADATA_VERSION,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts(n),1000000,cfg.generation,{}};
  CHECK(flSetFrameMetadata(channel,&buffer,&meta)==FL_OK);CHECK(flSubmit(channel,&buffer,pts(n))==FL_OK);
  if(audio){
   auto*block=gst_buffer_new_allocate(nullptr,192,nullptr);guint8 bytes[192];
   for(unsigned i=0;i<sizeof bytes;++i)bytes[i]=guint8((n*29+i*7+31)&255);
   CHECK(gst_buffer_fill(block,0,bytes,sizeof bytes)==sizeof bytes);
   GST_BUFFER_PTS(block)=n*1000000;GST_BUFFER_DURATION(block)=1000000;
   CHECK(gst_app_src_push_buffer(GST_APP_SRC(pcm->port),block)==GST_FLOW_OK);
  }
  published.signal();
  // A control gate bounds the fixture's outstanding frames; it is not proof
  // of native completion. The parent signals only after the real lease retires.
  retired.wait();
 }
 if(audio){
  FlGstVideoEvent end{FL_GST_VIDEO_END,Frames,guint64(pts(Frames-1)),0};
  CHECK(fl_gst_session_send_video_event(session,cfg.generation,&end)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_video_eos(session,cfg.generation,Frames,pts(Frames-1))==FL_GST_SESSION_READY);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(pcm->port))==GST_FLOW_OK);
 }
 CHECK(fl_gst_session_finish_local(session,TRUE,5000,nullptr));pcm.reset();
 flClose(channel);g_object_unref(session);return 0;
}
struct Peer {
 relaytest::Gate advance,published,retired;std::unique_ptr<relaytest::Child> child;
 Peer(const char*plugin,const std::string&path,bool audio=false){
  child.reset(new relaytest::Child({plugin,audio?"--relay-local-source-av":"--relay-local-source",path,advance.arg(),published.arg(),retired.arg()},
   {&advance,&published,&retired}));
 }
 void next(){advance.signal();published.wait();}
 void release(){retired.signal();}
 void join(){child->join();}
 void fail(){child->terminate();}
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

#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
// FAR5 transfer across a real process boundary. Only control gates are
// inherited; native allocations reach the reader only through the
// authenticated, kernel-pinned transfer.
namespace localpool {
static flSessionConfig upstreamConfig(guint timeout){
 auto caps=relaytest::localCaps();
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=31;cfg.clockId.bytes[0]=73;cfg.generation=19;
 cfg.videoTrackId=7;cfg.audioTrackId=8;cfg.video=caps.video[0];cfg.audio=caps.audio[0];
 cfg.videoQueueFrames=1;cfg.audioQueueMs=80;cfg.connectTimeoutMs=cfg.idleTimeoutMs=timeout;
 auto*clock=gst_system_clock_obtain();cfg.epochNs=gst_clock_get_time(clock)-GST_SECOND;gst_object_unref(clock);
 return cfg;
}
static FlGstSession* identityMappedUpstream(const flSessionConfig&cfg){
 auto caps=relaytest::localCaps();auto*up=fl_gst_session_new(3);
 CHECK(fl_gst_session_configure(up,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=81;mapping.sourceAnchorNs=cfg.epochNs;
 CHECK(fl_gst_session_set_mapping(up,cfg.generation,&mapping,0)==FL_GST_SESSION_READY);
 return up;
}
// Child: the final FAR5 receiver. fixedFixture checks the process test's
// configured upstream; audio adds PCM, END/EOS and drain; failedSource expects
// the source to die before END.
static int reader(const char*path,relaytest::Gate&held,relaytest::Gate&release,relaytest::Gate&exit,bool fixedFixture=true,bool audio=false,bool failedSource=false){
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native_pool_receiver(session,path,&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(flSessionValidate(&cfg)==FL_SESSION_OK&&bound.sessionGeneration==cfg.generation);
 if(fixedFixture)CHECK(cfg.generation==19&&cfg.clockId.bytes[0]==73&&cfg.videoTrackId==7&&cfg.audioTrackId==8);
 std::unique_ptr<LocalProcessAudio> pcm;
 if(audio){pcm.reset(new LocalProcessAudio(session,false));pcm->start();}
 guint64 audioFrames=0,audioBytes=0;
 CHECK(fl_gst_session_local_ready(session,FALSE,1000,nullptr));
 for(unsigned n=0;n<8;++n){
  flFrame frame{};CHECK(flAcquireFrame(channel,&frame,5000)==FL_OK);
  CHECK(frame.ptsNs==37000000+n*1000000);
  flFrameMetadata meta{};CHECK(flGetFrameMetadata(channel,&frame,&meta)==FL_OK);
  CHECK(meta.streamGeneration==cfg.generation&&meta.durationNs==1000000&&meta.dtsNs==frame.ptsNs);
  flMappedImage map{};CHECK(flImageMapPlanes(frame.image,&map)==FL_OK);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)
   CHECK(static_cast<unsigned char*>(map.plane[0].pixels)[y*map.plane[0].stride+x]==n+11);
  flImageUnmap(frame.image);
  if(audio){
   auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(pcm->port),5*GST_SECOND);CHECK(sample);
   auto*buffer=gst_sample_get_buffer(sample);guint8 bytes[192]{};gint64 mapped=-1;
   CHECK(gst_buffer_get_size(buffer)==sizeof bytes&&gst_buffer_extract(buffer,0,bytes,sizeof bytes)==sizeof bytes);
   // Independent expected source sequence, not data reported by the relay.
   for(unsigned i=0;i<sizeof bytes;++i)CHECK(bytes[i]==guint8((n*29+i*7+31)&255));
   CHECK(GST_BUFFER_DURATION(buffer)==1000000);
   CHECK(fl_gst_session_map_running_time(session,cfg.generation,frame.ptsNs,TRUE,&mapped)==FL_GST_SESSION_READY);
   CHECK(mapped>=37000000&&guint64(mapped-37000000)==GST_BUFFER_PTS(buffer));
   CHECK(fl_gst_session_map_running_time(session,cfg.generation,n*1000000,TRUE,&mapped)==FL_GST_SESSION_READY);
   CHECK(GST_BUFFER_PTS(buffer)==guint64(mapped));
   audioFrames+=48;audioBytes+=sizeof bytes;gst_sample_unref(sample);
  }
  uint64_t lease=0;CHECK(flLeaseFrame(channel,&frame,&lease)==FL_OK);
  if(n==7){
   held.signal();release.wait();
   // Re-read through the still-live lease after an optional replacement graph.
   flFrame retained{};uint64_t generation=0;
   CHECK(flReadLeaseFrame(channel,lease,&retained,&generation)==FL_OK);
   CHECK(retained.ptsNs==44000000);
   flMappedImage stable{};CHECK(flImageMapPlanes(retained.image,&stable)==FL_OK);
   for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)
    CHECK(static_cast<unsigned char*>(stable.plane[0].pixels)[y*stable.plane[0].stride+x]==18);
   flImageUnmap(retained.image);
  }
  CHECK(flCompleteReadLease(channel,lease)==FL_OK);CHECK(flCollectReadLeases(channel)==FL_OK);
 }
 if(audio&&!failedSource){
  FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(session,cfg.generation,5000,&end)==FL_GST_SESSION_READY);
  CHECK(end.kind==FL_GST_VIDEO_END&&end.afterFrames==8&&end.ptsNs==44000000);
  CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(pcm->port),5*GST_SECOND));
  CHECK(gst_app_sink_is_eos(GST_APP_SINK(pcm->port)));avTerminal(pcm->pipe,GST_MESSAGE_EOS);
  guint64 count=0;gint64 last=-1;
  CHECK(fl_gst_session_wait_video_eos(session,cfg.generation,1000,&count,&last)==FL_GST_SESSION_READY);
  CHECK(count==8&&last==44000000&&audioFrames==384&&audioBytes==1536);
  flFrame extra{};CHECK(flAcquireFrame(channel,&extra,0)==FL_TIMEOUT);
 }
 exit.wait();
 if(!failedSource)CHECK(fl_gst_session_finish_local(session,FALSE,1000,nullptr));
 else{fl_gst_session_cancel(session);CHECK(!gst_app_sink_is_eos(GST_APP_SINK(pcm->port)));}
 pcm.reset();flClose(channel);g_object_unref(session);return 0;
}
// Child: the final receiver as an automatic FAR5 framelinkavsrc bin.
// read is signalled after each video sample is pulled: the parent forwards the
// next frame only then, so a coordinated frame is never replaced unread.
static int binReader(const char*path,relaytest::Gate&held,relaytest::Gate&release,relaytest::Gate&exit,relaytest::Gate&read){
 GError*error=nullptr;
 auto*pipe=gst_parse_launch("framelinkavsrc name=transport native-memory=true video-format=RGBA width=64 height=32 framerate=30/1 video-pool=2 video-queue-frames=1 transport.video_src ! appsink name=video max-buffers=1 sync=false async=false enable-last-sample=false transport.audio_src ! appsink name=audio max-buffers=1 sync=false async=false enable-last-sample=false",&error);CHECK(pipe&&!error);
 auto*src=gst_bin_get_by_name(GST_BIN(pipe),"transport");auto*video=gst_bin_get_by_name(GST_BIN(pipe),"video");auto*audio=gst_bin_get_by_name(GST_BIN(pipe),"audio");
 auto candidate=nativeAvPolicy(0,true);g_object_set(src,"session",path,nullptr);
 gboolean accepted=FALSE;g_signal_emit_by_name(src,"set-local-native-pool-candidate",&candidate,&accepted);CHECK(accepted);
 g_object_set(video,"emit-signals",TRUE,nullptr);g_signal_connect(video,"propose-allocation",G_CALLBACK(nativeAvReaderAllocation),&candidate);
 CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstClockTime first=GST_CLOCK_TIME_NONE;guint64 frames=0;GstMemory*retained=nullptr;gsize finalOffset=0;gint finalStride=0;
 for(unsigned n=0;n<8;++n){
  auto*v=gst_app_sink_try_pull_sample(GST_APP_SINK(video),5*GST_SECOND);CHECK(v);read.signal();
  auto*a=gst_app_sink_try_pull_sample(GST_APP_SINK(audio),5*GST_SECOND);CHECK(a);
  auto*vb=gst_sample_get_buffer(v);auto*ab=gst_sample_get_buffer(a);auto*memory=gst_buffer_peek_memory(vb,0);CHECK(relaytest::isNativeMemory(memory));
  auto*meta=gst_buffer_get_video_meta(vb);auto*identity=gst_buffer_get_framelink_frame_meta(vb);guint64 generation=0;g_object_get(src,"generation",&generation,nullptr);
  CHECK(identity&&identity->frame.streamGeneration==generation&&generation);
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb));
  if(!n)first=GST_BUFFER_PTS(ab);
  CHECK(GST_BUFFER_PTS(ab)==first+n*1000000&&GST_BUFFER_DURATION(ab)==1000000&&GST_BUFFER_DURATION(vb)==1000000);
  CHECK(meta);relaytest::checkNativePixels(memory,meta->offset[0],meta->stride[0],guint8(n+11));guint8 bytes[192];CHECK(gst_buffer_get_size(ab)==sizeof bytes&&gst_buffer_extract(ab,0,bytes,sizeof bytes)==sizeof bytes);
  for(unsigned i=0;i<sizeof bytes;++i)CHECK(bytes[i]==guint8((n*29+i*7+31)&255));frames+=48;
  if(n==7){retained=gst_memory_ref(memory);finalOffset=meta->offset[0];finalStride=meta->stride[0];}
  gst_sample_unref(v);gst_sample_unref(a);
  if(retained)held.signal();
 }
 CHECK(frames==384);avTerminal(pipe,GST_MESSAGE_EOS);CHECK(gst_app_sink_is_eos(GST_APP_SINK(video))&&gst_app_sink_is_eos(GST_APP_SINK(audio)));
 // Keep the control session live until the sender accepts the real drain.
 release.wait();
 CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
 CHECK(retained);relaytest::checkNativePixels(retained,finalOffset,finalStride,18);exit.signal();release.wait();gst_memory_unref(retained);exit.wait();
 gst_object_unref(src);gst_object_unref(video);gst_object_unref(audio);gst_object_unref(pipe);
 puts("automatic FAR5 receiver bin: 8 native/384 PCM, exact bytes/37ms, held GstMemory and authenticated EOS");return 0;
}
static void binNegatives(){
 for(unsigned mode=0;mode<10;++mode){
  auto*bin=gst_element_factory_make("framelinkavsrc",nullptr);CHECK(bin);
  g_object_set(bin,"session","unused-far5-negative","native-memory",TRUE,"video-pool",2u,"video-queue-frames",1u,nullptr);
  auto candidate=nativeAvPolicy(0,true);
  if(mode==1)g_object_set(bin,"session",nullptr,nullptr);
  if(mode==2)g_object_set(bin,"tracks","video",nullptr);
  if(mode==3)g_object_set(bin,"video-pool",0u,nullptr);
  if(mode==4)g_object_set(bin,"video-pool",1u,nullptr);
  if(mode==5)g_object_set(bin,"shared-cpu",TRUE,nullptr);
  if(mode==6)candidate.sessionGeneration=1;
  if(mode==7)g_object_set(bin,"native-memory",FALSE,nullptr);
  gboolean accepted=FALSE;
  if(mode==9){g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(accepted);}
  accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-pool-candidate",mode==8?nullptr:&candidate,&accepted);
  CHECK(accepted==(mode==0));
  if(mode==0){
   g_signal_emit_by_name(bin,"set-local-native-pool-candidate",&candidate,&accepted);CHECK(!accepted);
   g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);
   g_signal_emit_by_name(bin,"adopt-channel",nullptr,&accepted);CHECK(!accepted);
   g_signal_emit_by_name(bin,"apply-memory-policy",&candidate,&accepted);CHECK(!accepted);
   g_object_set(bin,"width",96u,nullptr);guint width=0;g_object_get(bin,"width",&width,nullptr);CHECK(width==1280);
  }
  gst_object_unref(bin);
 }
 for(bool mismatch:{false,true}){
  auto*pipe=gst_pipeline_new(nullptr);auto*bin=gst_element_factory_make("framelinkavsrc",nullptr);CHECK(pipe&&bin);gst_bin_add(GST_BIN(pipe),bin);
  gchar*directory=g_dir_make_tmp("framelink-far5-bin-negative-XXXXXX",nullptr);CHECK(directory);std::string path=std::string(directory)+"/session";g_free(directory);
  g_object_set(bin,"session",path.c_str(),"native-memory",TRUE,"video-format","RGBA","width",64u,"height",32u,
    "video-pool",2u,"video-queue-frames",1u,"connect-timeout-ms",mismatch?1000u:60000u,nullptr);
  gst_util_set_object_arg(G_OBJECT(bin),"framerate","30/1");
  auto candidate=nativeAvPolicy(0,true);gboolean accepted=FALSE;g_signal_emit_by_name(bin,"set-local-native-pool-candidate",&candidate,&accepted);CHECK(accepted);
  CHECK(gst_element_set_state(pipe,GST_STATE_READY)!=GST_STATE_CHANGE_FAILURE);
  if(mismatch){
   auto*session=fl_gst_session_new(3);auto caps=relaytest::localCaps();GstFrameLinkMemoryPolicy bound{};flChannel*channel=nullptr;GError*error=nullptr;
   // Existing FAR4 producer must not silently downgrade this explicit FAR5 bin.
   gboolean ok=fl_gst_session_open_local_native(session,TRUE,path.c_str(),&caps,1,2,80,1000,1000,&candidate,&bound,&channel,nullptr,&error);
   CHECK(!ok&&!channel&&!bound.version&&error);g_clear_error(&error);g_object_unref(session);avTerminal(pipe,GST_MESSAGE_ERROR);
  }else g_usleep(30000);
  const auto began=g_get_monotonic_time();CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  CHECK(g_get_monotonic_time()-began<1000000);gst_object_unref(pipe);
 }
 puts("FAR5 bin controls: explicit immutable profile, invalid candidates, FAR4 mismatch/no fallback, bounded missing-peer cancellation");
}
// Parent: source ring and writer, FAR5 sender, same-allocation forwarding.
static void process(const char*plugin){
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 const auto cfg=upstreamConfig(5000);auto*up=identityMappedUpstream(cfg);auto*down=fl_gst_session_new(3);
 auto name="native-local-process-"+std::to_string(g_get_monotonic_time());
 flChannel*source=nullptr,*writer=nullptr,*downstream=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,&source)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&writer)==FL_OK);
 // Pin every unpublished slot; export them reversed, so slot index is not identity.
 flBuffer reservations[2]{};relaytest::PoolExport exports{};
 for(unsigned n=0;n<2;++n){CHECK(flAcquireBuffer(writer,&reservations[n],1000)==FL_OK);relaytest::exportImage(writer,reservations[n].image,exports,1-n);}
 gchar*dir=g_dir_make_tmp("framelink-native-process-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
 relaytest::Gate held,release,exit;
 relaytest::Child child({plugin,"--relay-local-reader",path,held.arg(),release.arg(),exit.arg()},{&held,&release,&exit});
 GError*error=nullptr;
 CHECK(relaytest::openRelayPool(down,up,path.c_str(),&caps,1,2,80,5000,5000,&candidate,exports,&bound,&downstream,nullptr,&error));
 for(auto&b:reservations)CHECK(flCancelBuffer(writer,&b)==FL_OK);
 CHECK(fl_gst_session_local_ready(down,TRUE,1000,nullptr));
 flsample::NativeRelay relay(source,downstream,[](flImage*a,flImage*b){return relaytest::sameAllocation(a,b);});
 CHECK(relay.initialize()==FL_OK);
 for(unsigned n=0;n<8;++n){
  flBuffer buffer{};CHECK(flAcquireBuffer(writer,&buffer,1000)==FL_OK);
  relaytest::fill(writer,buffer,static_cast<unsigned char>(n+11));
  const gint64 pts=37000000+n*1000000;
  flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts,1000000,19,{}};
  CHECK(flSetFrameMetadata(writer,&buffer,&meta)==FL_OK);CHECK(flSubmit(writer,&buffer,pts)==FL_OK);
  flFrame frame{};CHECK(flAcquireFrame(source,&frame,1000)==FL_OK);CHECK(relay.forward(frame)==FL_OK);
  if(n==7){held.wait();CHECK(relay.pending()==1&&relay.finish()==FL_BUSY);release.signal();}
  const auto deadline=g_get_monotonic_time()+2000000;
  while(relay.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
 }
 CHECK(relay.count()==8&&relay.finish()==FL_OK);exit.signal();
 CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));child.join();
 flClose(downstream);flClose(writer);flClose(source);g_object_unref(down);g_object_unref(up);
 puts("FAR5 cross-process: no inherited native resources; 8 exact frames/metadata; source lease retained until actual remote reader release; authenticated drain");
}
static void failures(){
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);
 const auto cfg=upstreamConfig(1000);auto*up=identityMappedUpstream(cfg);
 auto name="native-pool-negative-"+std::to_string(g_get_monotonic_time());flChannel*reader=nullptr,*writer=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&reader)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&writer)==FL_OK);
 flBuffer slots[2]{};for(auto&b:slots)CHECK(flAcquireBuffer(writer,&b,1000)==FL_OK);
 relaytest::PoolExport original{};for(unsigned n=0;n<2;++n)relaytest::exportImage(writer,slots[n].image,original,n);
 relaytest::Gate foreign; // a valid kernel object that is not an image
 const relaytest::BadExport bad[]={relaytest::BadExport::Duplicate,relaytest::BadExport::Invalid,
  relaytest::BadExport::Foreign,relaytest::BadExport::Mismatch};
 for(unsigned mode=0;mode<6;++mode){
  auto exports=original;if(mode<4)relaytest::corrupt(exports,bad[mode],foreign);
  auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
  GstFrameLinkMemoryPolicy bound{},received{};flChannel*p=nullptr,*c=nullptr;
  GError*localError=nullptr,*remoteError=nullptr;
  gchar*dir=g_dir_make_tmp("framelink-pool-negative-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
  gboolean remote=TRUE;auto before=g_get_monotonic_time();
  std::thread peer([&]{
   remote=mode==5?fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,1,2,80,500,1000,&candidate,&received,&c,nullptr,&remoteError):
    fl_gst_session_open_local_native_pool_receiver(r,path.c_str(),&caps,1,mode==4?3:2,80,500,1000,&candidate,&received,&c,nullptr,&remoteError);
  });
  auto local=relaytest::openRelayPool(s,up,path.c_str(),&caps,1,2,80,500,1000,&candidate,exports,&bound,&p,nullptr,&localError);
  peer.join();fprintf(stderr,"pool negative mode=%u local=%d remote=%d localError=%s remoteError=%s\n",mode,int(local),int(remote),localError?localError->message:"none",remoteError?remoteError->message:"none");
  CHECK(!local&&!remote&&localError&&remoteError&&!p&&!c&&!bound.version&&!received.version);
  if(mode<5){CHECK(strstr(localError->message,"authenticated relay pool transfer")&&strstr(remoteError->message,"authenticated relay pool transfer"));}
  CHECK(g_get_monotonic_time()-before<2000000);
  flSessionConfig still{};CHECK(fl_gst_session_get_config(up,&still)==FL_GST_SESSION_READY);
  CHECK(relaytest::exportsAlive(original,2));
  g_clear_error(&localError);g_clear_error(&remoteError);g_object_unref(s);g_object_unref(r);
 }
 // Missing downstream must not mask a subsequently cancelled upstream for
 // the full two-second discovery budget. The native source slots stay pinned.
 {
  auto*down=fl_gst_session_new(3);GstFrameLinkMemoryPolicy bound{};flChannel*output=nullptr;GError*error=nullptr;
  gchar*dir=g_dir_make_tmp("framelink-pool-source-cancel-XXXXXX",nullptr);CHECK(dir);
  std::string path=std::string(dir)+"/session";g_free(dir);
  const auto began=g_get_monotonic_time();
  std::thread cancel([&]{g_usleep(30000);fl_gst_session_cancel(up);});
  auto ok=relaytest::openRelayPool(down,up,path.c_str(),&caps,1,2,80,2000,1000,&candidate,original,&bound,&output,nullptr,&error);
  cancel.join();const auto elapsed=g_get_monotonic_time()-began;
  CHECK(!ok&&error&&!output&&!bound.version&&elapsed<500000);
  fprintf(stderr,"upstream cancellation during missing-downstream setup: %.3fms\n",elapsed/1000.0);
  g_clear_error(&error);g_object_unref(down);
 }
 for(auto&b:slots)CHECK(flCancelBuffer(writer,&b)==FL_OK);
 for(auto&b:slots)CHECK(flAcquireBuffer(writer,&b,1000)==FL_OK);
 for(auto&b:slots)CHECK(flCancelBuffer(writer,&b)==FL_OK);
 flClose(writer);flClose(reader);g_object_unref(up);
 puts("FAR5 pool failures: duplicate/invalid/foreign exports, adapter or layout mismatch, wrong capacity, legacy receiver reject; outputs clear, exports and source pool survive");
}
}

// Three independent processes: source child, this relay, final reader child.
// Uses the committed consumer-pool snapshot, never inherited native resources.
namespace graph {
using localsource::Frames;using localsource::pts;using localsource::Peer;
struct HeldGraph { flSessionConfig config{};relaytest::AllocationId allocations[2]{}; };
static void threeProcess(const char*plugin,bool audio=false,bool replaceWhileHeld=false,const HeldGraph*old=nullptr,bool failedSource=false,bool binFinal=false){
 gchar*dir=g_dir_make_tmp("framelink-three-local-XXXXXX",nullptr);CHECK(dir);
 const std::string root=dir;g_free(dir);const auto inputPath=root+"/input",outputPath=root+"/output";
 Peer source(plugin,inputPath,audio);
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);
 auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
 flChannel*input=nullptr,*output=nullptr;GError*error=nullptr;
 relaytest::Gate held,release,exit,read;
 const char*readerMode=binFinal?"--relay-local-reader-bin":(failedSource?"--relay-local-reader-failed":(audio?"--relay-local-reader-av":"--relay-local-reader-generic"));
 std::vector<std::string> readerArgs{plugin,readerMode,outputPath,held.arg(),release.arg(),exit.arg()};
 std::vector<const relaytest::Gate*> readerGates{&held,&release,&exit};
 if(binFinal){readerArgs.push_back(read.arg());readerGates.push_back(&read);}
 relaytest::Child final(readerArgs,readerGates);
 flsample::LocalNativeRelaySetup prepared{};
 CHECK(flsample::setupLocalNativeRelay(up,down,inputPath.c_str(),outputPath.c_str(),&caps,&candidate,
   1,2,80,5000,5000,nullptr,&prepared,&error));
 input=prepared.input;output=prepared.output;
 auto*snapshot=prepared.snapshot;
 flSessionConfig original{};CHECK(fl_gst_session_get_config(up,&original)==FL_GST_SESSION_READY);
 flChannelInfo info{};CHECK(flsample::queryLocalPoolSnapshot(snapshot,&info)==FL_OK&&info.poolSize==2);
 relaytest::AllocationId allocations[2]{};for(unsigned n=0;n<2;++n)allocations[n]=relaytest::snapshotAllocation(snapshot,n);
 // Both channels are ready, but the source is still forbidden to publish.
 CHECK(source.published.idle());
 flSessionConfig forwarded{};CHECK(fl_gst_session_get_config(down,&forwarded)==FL_GST_SESSION_READY);
 CHECK(flsample::SessionRelayContract::compatible(original,forwarded));
 if(old){
  CHECK(memcmp(original.sessionId.bytes,old->config.sessionId.bytes,16)!=0);
  CHECK(memcmp(original.clockId.bytes,old->config.clockId.bytes,16)!=0);
  CHECK(!flsample::SessionRelayContract::compatible(old->config,forwarded));
  for(auto&current:allocations)for(auto&prior:old->allocations)CHECK(!relaytest::sameAllocation(current,prior));
 }
 flsample::SessionNativeRelay relay(up,down,input,output,[](flImage*a,flImage*b){return relaytest::sameAllocation(a,b);});
 CHECK(relay.initialize()==FL_OK);
 std::unique_ptr<LocalProcessAudio> pcmInput,pcmOutput;
 std::unique_ptr<flsample::SessionPcmRelay> pcm;
 if(audio){
  pcmInput.reset(new LocalProcessAudio(up,false));pcmOutput.reset(new LocalProcessAudio(down,true));
  pcm.reset(new flsample::SessionPcmRelay(up,down,GST_APP_SINK(pcmInput->port),GST_APP_SRC(pcmOutput->port)));
  CHECK(pcm->initialize()==FL_GST_SESSION_READY);pcmOutput->start();pcmInput->start();
 }
 flsample::SessionRelaySupervisor supervisor(up,down);CHECK(supervisor.start()==FL_GST_SESSION_READY);
 CHECK(fl_gst_session_local_ready(down,TRUE,1000,nullptr));
 CHECK(fl_gst_session_local_ready(up,FALSE,1000,nullptr));
 if(binFinal){FlGstVideoEvent barrier{FL_GST_VIDEO_BARRIER,0,guint64(pts(0)),0};CHECK(fl_gst_session_send_video_event(down,original.generation,&barrier)==FL_GST_SESSION_READY);}
 for(unsigned n=0;n<Frames;++n){
  source.next();flFrame frame{};CHECK(flAcquireFrame(input,&frame,1000)==FL_OK);
  CHECK(frame.ptsNs==pts(n));CHECK(relay.forward(frame)==FL_OK);
  if(binFinal)read.wait(); // the bin has taken frame n before frame n+1 can be published
  if(audio){
   const auto until=g_get_monotonic_time()+5000000;FlGstSessionResult result;
   do{result=pcm->forward(100);CHECK(result==FL_GST_SESSION_READY||result==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<until);}while(result==FL_GST_SESSION_TIMEOUT);
  }
  if(n+1==Frames){
   held.wait();CHECK(relay.collect()==FL_OK&&relay.pending()==1);
   // Collection cannot retire the source lease while the final reader holds it.
   // Do not call SessionNativeRelay::finish here: that closes publication
   // before the source's authenticated END can be forwarded.
   CHECK(relay.collect()==FL_OK&&relay.pending()==1);
   if(failedSource){
    source.fail();const auto deadline=g_get_monotonic_time()+1000000;
    while(supervisor.status()==FL_GST_SESSION_READY){CHECK(g_get_monotonic_time()<deadline);g_usleep(1000);}
    CHECK(supervisor.status()!=FL_GST_SESSION_READY&&relay.pending()==1&&!relay.videoEnded());
    FlGstVideoEvent event{};CHECK(fl_gst_session_receive_video_event(up,original.generation,0,&event)!=FL_GST_SESSION_READY);
   }
   if(replaceWhileHeld){
    HeldGraph heldGraph{original,{allocations[0],allocations[1]}};
    threeProcess(plugin,true,false,&heldGraph);
    CHECK(relay.pending()==1);
    if(!failedSource)CHECK(relay.collect()==FL_OK);
    if(!failedSource){flSessionConfig unchanged{};CHECK(fl_gst_session_get_config(up,&unchanged)==FL_GST_SESSION_READY);CHECK(memcmp(&unchanged,&original,sizeof original)==0);}
   }
   if(!binFinal)release.signal();
   if(failedSource){
    // Source is already stopped. Retire the entire failed graph only after
    // the old final reader has completed its real lease and exited.
    exit.signal();final.join();
    CHECK(relay.pending()==1&&!relay.videoEnded());supervisor.stop();
    pcm.reset();pcmOutput.reset();pcmInput.reset();
    flsample::releaseLocalPoolSnapshot(snapshot);flClose(output);flClose(input);
    g_object_unref(down);g_object_unref(up);
    puts("source-death replacement: bounded cancellation, no synthetic END/EOS, fresh A/V graph with disjoint allocations, old held pixels intact before final-reader completion");
    return;
   }
  }
  const auto deadline=g_get_monotonic_time()+2000000;
  while(relay.pending()>(binFinal?1u:0u)){CHECK(g_get_monotonic_time()<deadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
  // Raw readers acknowledge actual retirement. Streaming appsink permits one
  // in-flight sample; this gate permits the next write attempt, whose native
  // acquisition still enforces actual slot ownership. It never completes a lease.
  source.release();
 }
 CHECK(relay.count()==Frames);
 if(audio){
  CHECK(relay.forwardEvent(5000)==FL_GST_SESSION_READY&&relay.videoEnded());
  avTerminal(pcmInput->pipe,GST_MESSAGE_EOS);
  if(binFinal){
   CHECK(relay.pending()==1&&pcm->finish(relay,0)==FL_GST_SESSION_TIMEOUT);
   CHECK(pcm->endPublication(relay,5000)==FL_GST_SESSION_READY);
   supervisor.stop();CHECK(supervisor.status()==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));release.signal();
   exit.wait();CHECK(relay.pending()==1&&relay.finish()==FL_BUSY);
   release.signal();
   const auto deadline=g_get_monotonic_time()+2000000;
   while(relay.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
  }else CHECK(pcm->finish(relay,5000)==FL_GST_SESSION_READY);
  CHECK(pcm->blockCount()==8&&pcm->frameCount()==384&&pcm->byteCount()==1536);
 }
 CHECK(relay.finish()==FL_OK);
 supervisor.stop();CHECK(supervisor.status()==FL_GST_SESSION_READY);
 exit.signal();if(!binFinal)CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));
 CHECK(fl_gst_session_finish_local(up,FALSE,1000,nullptr));
 final.join();source.join();
 // The snapshot pins the source epoch for the ENTIRE graph, not only import.
 pcm.reset();pcmOutput.reset();pcmInput.reset();
 flsample::releaseLocalPoolSnapshot(snapshot);flClose(output);flClose(input);
 g_object_unref(down);g_object_unref(up);
 if(replaceWhileHeld)puts("fresh-session replacement: distinct session/clock/native allocations; old reader held through complete replacement A/V graph; old pixels rechecked before real release");
 if(audio)puts("three-process local authenticated A/V: 384 exact PCM frames/1536 bytes, 37ms offset, real END/EOS, final native lease held");
 puts("three-process local native relay: FAR4 source-time input + FAR5 snapshot transfer, no inherited native resources, 8 exact final pixels/metadata, held final lease, real drain");
}

static void setupNegatives(){
 for(bool cancelSetup:{false,true}){
  gchar*directory=g_dir_make_tmp("framelink-relay-setup-XXXXXX",nullptr);CHECK(directory);std::string root=directory;g_free(directory);
  const auto inputPath=root+"/input",outputPath=root+"/absent-output";
  auto*producerSession=fl_gst_session_new(3);auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
  auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);auto*cancel=g_cancellable_new();
  std::atomic<bool> opened{false};bool producerReady=true;
  std::thread source([&]{
   GstFrameLinkMemoryPolicy bound{};flChannel*channel=nullptr;GError*error=nullptr;
   CHECK(fl_gst_session_open_local_native(producerSession,TRUE,inputPath.c_str(),&caps,1,2,80,2000,2000,&candidate,&bound,&channel,nullptr,&error));
   opened.store(true);producerReady=fl_gst_session_local_ready(producerSession,TRUE,2000,nullptr);
   CHECK(!producerReady);fl_gst_session_cancel(producerSession);flClose(channel);
  });
  std::thread stop;
  if(cancelSetup)stop=std::thread([&]{const auto deadline=g_get_monotonic_time()+2000000;while(!opened.load()){CHECK(g_get_monotonic_time()<deadline);g_usleep(1000);}g_usleep(30000);g_cancellable_cancel(cancel);});
  flsample::LocalNativeRelaySetup result{};GError*error=nullptr;const auto began=g_get_monotonic_time();
  CHECK(!flsample::setupLocalNativeRelay(up,down,inputPath.c_str(),outputPath.c_str(),&caps,&candidate,
    1,2,80,cancelSetup?60000u:500u,2000,cancel,&result,&error));
  CHECK(error&&!result.input&&!result.output&&!result.snapshot&&!result.inputPolicy.version&&!result.outputPolicy.version);
  CHECK(g_get_monotonic_time()-began<1500000);source.join();if(stop.joinable())stop.join();
  CHECK(opened.load()&&!producerReady);flSessionConfig config{};
  CHECK(fl_gst_session_get_config(up,&config)==FL_GST_SESSION_CANCELLED&&fl_gst_session_get_config(down,&config)==FL_GST_SESSION_CANCELLED);
  g_clear_error(&error);g_object_unref(cancel);g_object_unref(up);g_object_unref(down);g_object_unref(producerSession);
 }
 puts("transactional relay setup: downstream absent/cancelled, clear outputs, both sessions cancelled, source never readied");
}
}

#endif
// Platform-neutral controls: the video event adapter, the relay supervisor
// lifecycle, and FAR5 receiver bin PAUSED/resume and paused cancellation.
namespace controls {
using relaytest::nativeAvPolicy;
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

// Lifecycle controls supplement the real endpoint-failure tests. In particular,
// preflight rejection/normal shutdown must never cancel a healthy source graph.
static void relaySupervisorControls(){
 auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;
 cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
 cfg.video={FL_FORMAT_RGBA8,64,32,30,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=caps.audioCount=1;caps.video[0]=cfg.video;caps.audio[0]=cfg.audio;
 CHECK(fl_gst_session_configure(up,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 cfg.sessionId.bytes[0]=3;
 CHECK(fl_gst_session_configure(down,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=9;
 CHECK(fl_gst_session_set_mapping(up,1,&mapping,0)==FL_GST_SESSION_READY);
 std::atomic<unsigned> failures{0};
 {
  flsample::SessionRelaySupervisor invalid(up,up,[&]{++failures;});
  CHECK(invalid.start()==FL_GST_SESSION_INVALID);
  flSessionConfig snapshot{};
  CHECK(fl_gst_session_get_config(up,&snapshot)==FL_GST_SESSION_READY);
 }
 {
  flsample::SessionRelaySupervisor stopped(up,down,[&]{++failures;});
  stopped.stop();CHECK(stopped.start()==FL_GST_SESSION_INVALID);
 }
 {
  flsample::SessionRelaySupervisor supervisor(up,down,[&]{++failures;});
  CHECK(supervisor.start()==FL_GST_SESSION_READY);
  supervisor.stop();supervisor.stop();
  CHECK(supervisor.start()==FL_GST_SESSION_INVALID);
  fl_gst_session_cancel(down);
  flSessionConfig snapshot{};
  CHECK(fl_gst_session_get_config(up,&snapshot)==FL_GST_SESSION_READY);
  CHECK(supervisor.status()==FL_GST_SESSION_READY&&failures.load()==0);
 }
 // A pre-existing failure is rejected, not treated as a monitored graph.
 {
  flsample::SessionRelaySupervisor rejected(up,down,[&]{++failures;});
  CHECK(rejected.start()==FL_GST_SESSION_CANCELLED);
  CHECK(failures.load()==0);
 }
 g_object_unref(up);g_object_unref(down);
 puts("supervisor controls: unrelated/cancelled graph rejects without propagation; stopped-before-start rejects; stop joins without cancellation; single-use lifecycle");
}


#if defined(FL_GST_LOCAL_NATIVE_RELAY_POOL) && !defined(__ANDROID__) // Android: phase 4
// Bounded automatic FAR5 lifecycle controls. Only command/notice gates cross
// into the reader process; source admission stops before every pause command.
// No claim about overload behavior, physical playback, or lifecycle memory slope.
static void pauseMemory(GstMemory*memory,gsize offset,gint stride,unsigned value){
 CHECK(memory&&relaytest::isNativeMemory(memory));GstMapInfo map{};
 CHECK(gst_memory_map(memory,&map,GST_MAP_READ));
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)
  CHECK(map.data[offset+y*stride+x]==value);
 gst_memory_unmap(memory,&map);
}
static void pauseState(GstElement*pipe,GstState state){
 const auto began=g_get_monotonic_time();
 CHECK(gst_element_set_state(pipe,state)!=GST_STATE_CHANGE_FAILURE);
 GstState actual=GST_STATE_VOID_PENDING,pending=GST_STATE_VOID_PENDING;
 auto result=gst_element_get_state(pipe,&actual,&pending,GST_SECOND);
 CHECK(result!=GST_STATE_CHANGE_FAILURE&&result!=GST_STATE_CHANGE_ASYNC&&actual==state);
 CHECK(g_get_monotonic_time()-began<1500000);
}
// Failure diagnostics for the reader log: which sample and any element errors.
static void busErrors(GstElement*pipe,unsigned n){
 fprintf(stderr,"pause reader: no sample %u\n",n);auto*bus=gst_element_get_bus(pipe);
 while(auto*m=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR)){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);
  fprintf(stderr,"pause reader: %s (%s)\n",e->message,d?d:"");g_clear_error(&e);g_free(d);gst_message_unref(m);}
 gst_object_unref(bus);
}
static int pauseReader(const char*path,relaytest::Gate&notice,relaytest::Gate&command,relaytest::Gate&read,bool cancelPaused){
 CHECK(freopen((std::string(path)+".pause-reader.log").c_str(),"w",stderr));setvbuf(stderr,nullptr,_IONBF,0);
 GError*error=nullptr;
 auto*pipe=gst_parse_launch("framelinkavsrc name=transport native-memory=true video-format=RGBA width=64 height=32 framerate=30/1 video-pool=2 video-queue-frames=1 transport.video_src ! appsink name=video max-buffers=1 sync=false async=false enable-last-sample=false transport.audio_src ! appsink name=audio max-buffers=1 sync=false async=false enable-last-sample=false",&error);CHECK(pipe&&!error);
 auto*src=gst_bin_get_by_name(GST_BIN(pipe),"transport");auto*video=gst_bin_get_by_name(GST_BIN(pipe),"video");auto*audio=gst_bin_get_by_name(GST_BIN(pipe),"audio");
 auto candidate=nativeAvPolicy(0,true);g_object_set(src,"session",path,nullptr);
 gboolean accepted=FALSE;g_signal_emit_by_name(src,"set-local-native-pool-candidate",&candidate,&accepted);CHECK(accepted);
 g_object_set(video,"emit-signals",TRUE,nullptr);g_signal_connect(video,"propose-allocation",G_CALLBACK(nativeAvReaderAllocation),&candidate);
 // Persist EOS observations across GstPipeline bus flushing during NULL.
 std::atomic<unsigned> eosEvents{0};GstPad*eosPads[2]={gst_element_get_static_pad(src,"video_src"),gst_element_get_static_pad(src,"audio_src")};gulong eosProbes[2]{};
 for(unsigned i=0;i<2;++i){CHECK(eosPads[i]);eosProbes[i]=gst_pad_add_probe(eosPads[i],GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,[](GstPad*,GstPadProbeInfo*info,gpointer data){if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info))==GST_EVENT_EOS)static_cast<std::atomic<unsigned>*>(data)->fetch_add(1);return GST_PAD_PROBE_OK;},&eosEvents,nullptr);CHECK(eosProbes[i]);}
 CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstClockTime first=GST_CLOCK_TIME_NONE;guint64 originalGeneration=0,pcmFrames=0;std::string originalSession;
 GstMemory*held=nullptr;gsize heldOffset=0;gint heldStride=0;unsigned received=0;
 auto identity=[&]{guint64 generation=0;gchar*session=nullptr;g_object_get(src,"generation",&generation,"session-id",&session,nullptr);
  CHECK(generation&&session&&*session);if(!originalGeneration){originalGeneration=generation;originalSession=session;}
  CHECK(generation==originalGeneration&&originalSession==session);g_free(session);};
 auto noEos=[&]{CHECK(eosEvents.load()==0);auto*bus=gst_element_get_bus(pipe);CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);};
 for(unsigned n=0;n<(cancelPaused?4u:8u);++n){
  auto*v=gst_app_sink_try_pull_sample(GST_APP_SINK(video),5*GST_SECOND);if(!v)busErrors(pipe,n);CHECK(v);read.signal();
  auto*a=gst_app_sink_try_pull_sample(GST_APP_SINK(audio),5*GST_SECOND);if(!a)busErrors(pipe,n);CHECK(a);
  auto*vb=gst_sample_get_buffer(v);auto*ab=gst_sample_get_buffer(a);auto*memory=gst_buffer_peek_memory(vb,0);
  auto*meta=gst_buffer_get_video_meta(vb);auto*frame=gst_buffer_get_framelink_frame_meta(vb);identity();
  CHECK(meta&&frame&&frame->frame.streamGeneration==originalGeneration);
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb));
  if(!n)first=GST_BUFFER_PTS(ab);
  CHECK(GST_BUFFER_PTS(ab)==first+n*1000000&&GST_BUFFER_DURATION(ab)==1000000&&GST_BUFFER_DURATION(vb)==1000000);
  pauseMemory(memory,meta->offset[0],meta->stride[0],n+11);
  guint8 bytes[192];CHECK(gst_buffer_get_size(ab)==sizeof bytes&&gst_buffer_extract(ab,0,bytes,sizeof bytes)==sizeof bytes);
  for(unsigned i=0;i<sizeof bytes;++i)CHECK(bytes[i]==guint8((n*29+i*7+31)&255));pcmFrames+=48;++received;
  if(n==3||n==7){held=gst_memory_ref(memory);heldOffset=meta->offset[0];heldStride=meta->stride[0];}
  gst_sample_unref(v);gst_sample_unref(a);
  if(n==3){
   pauseState(pipe,GST_STATE_PAUSED);identity();noEos();
   pauseMemory(held,heldOffset,heldStride,14);notice.signal();
   command.wait(); // parent verified admission and pending lease
   if(cancelPaused){
    pauseState(pipe,GST_STATE_NULL);noEos();pauseMemory(held,heldOffset,heldStride,14);
    notice.signal();command.wait(); // cancellation observed; still no release
    pauseMemory(held,heldOffset,heldStride,14);gst_memory_unref(held);held=nullptr;
    notice.signal();command.wait();break;
   }
   pauseState(pipe,GST_STATE_PLAYING);identity();noEos();pauseMemory(held,heldOffset,heldStride,14);
   notice.signal();command.wait(); // parent proves resume alone did not complete lease
   gst_memory_unref(held);held=nullptr;notice.signal();
  }else if(n==7)notice.signal();
 }
 if(cancelPaused){CHECK(received==4&&pcmFrames==192&&!held);noEos();}
 else{
  CHECK(received==8&&pcmFrames==384&&held);avTerminal(pipe,GST_MESSAGE_EOS);CHECK(eosEvents.load()==2);
  CHECK(gst_app_sink_is_eos(GST_APP_SINK(video))&&gst_app_sink_is_eos(GST_APP_SINK(audio)));identity();
  notice.signal();command.wait(); // parent accepts authenticated terminal before teardown
  fprintf(stderr,"pause final memory refs before NULL: %d\n",GST_MINI_OBJECT_REFCOUNT_VALUE(held));
  pauseState(pipe,GST_STATE_NULL);pauseMemory(held,heldOffset,heldStride,18);
  fprintf(stderr,"pause final memory refs after NULL: %d\n",GST_MINI_OBJECT_REFCOUNT_VALUE(held));
  gst_memory_unref(held);held=nullptr;fprintf(stderr,"pause final unref returned\n");notice.signal();command.wait();
 }
 for(unsigned i=0;i<2;++i){gst_pad_remove_probe(eosPads[i],eosProbes[i]);gst_object_unref(eosPads[i]);}
 gst_object_unref(src);gst_object_unref(video);gst_object_unref(audio);gst_object_unref(pipe);
 puts(cancelPaused?"FAR5 paused cancellation:4 exact native/192 PCM, held pixels valid through NULL and cancellation, no EOS":"FAR5 pause/resume:8 exact native/384 PCM, unchanged session/generation/37ms, new media and real EOS after resume");return 0;
}
static void pauseControl(const char*plugin,bool cancelPaused){
 gchar*directory=g_dir_make_tmp("framelink-far5-pause-XXXXXX",nullptr);CHECK(directory);const std::string root=directory;g_free(directory);
 const auto inputPath=root+"/input",outputPath=root+"/output";
 relaytest::Gate notice,command,read,advance,published,retired,sourceCancelled,sourceClose;
 relaytest::Child child({plugin,"--relay-local-pause-reader",outputPath,notice.arg(),command.arg(),read.arg(),cancelPaused?"1":"0"},{&notice,&command,&read});
 auto caps=relaytest::localCaps();auto candidate=nativeAvPolicy(0,true);auto*source=fl_gst_session_new(3);auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
 std::atomic<unsigned> submitted{0};std::atomic<gint64> cancellationUs{0};
 std::thread producer([&]{
  flChannel*writer=nullptr;GstFrameLinkMemoryPolicy bound{};GError*error=nullptr;
  CHECK(fl_gst_session_open_local_native(source,TRUE,inputPath.c_str(),&caps,1,2,80,5000,5000,&candidate,&bound,&writer,nullptr,&error));
  flSessionConfig cfg{};CHECK(fl_gst_session_get_config(source,&cfg)==FL_GST_SESSION_READY);
  {LocalProcessAudio audio(source,true);audio.start();CHECK(fl_gst_session_local_ready(source,TRUE,5000,nullptr));
   for(unsigned n=0;n<(cancelPaused?4u:8u);++n){
    advance.wait();flBuffer buffer{};CHECK(flAcquireBuffer(writer,&buffer,1000)==FL_OK);
    relaytest::fill(writer,buffer,static_cast<unsigned char>(n+11));
    const gint64 pts=37000000+n*1000000;flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts,1000000,cfg.generation,{}};
    CHECK(flSetFrameMetadata(writer,&buffer,&meta)==FL_OK&&flSubmit(writer,&buffer,pts)==FL_OK);
    auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);guint8 bytes[192];for(unsigned i=0;i<sizeof bytes;++i)bytes[i]=guint8((n*29+i*7+31)&255);
    CHECK(gst_buffer_fill(pcm,0,bytes,sizeof bytes)==sizeof bytes);GST_BUFFER_PTS(pcm)=n*1000000;GST_BUFFER_DURATION(pcm)=1000000;
    CHECK(gst_app_src_push_buffer(GST_APP_SRC(audio.port),pcm)==GST_FLOW_OK);submitted.store(n+1);published.signal();retired.wait();
   }
   if(cancelPaused){
    const auto began=g_get_monotonic_time();flSessionConfig snapshot{};FlGstSessionResult result;
    do{result=fl_gst_session_wait(source,cfg.generation,20,&snapshot);if(result==FL_GST_SESSION_READY)g_usleep(1000);CHECK(g_get_monotonic_time()-began<1500000);}while(result==FL_GST_SESSION_READY||result==FL_GST_SESSION_TIMEOUT);
    CHECK(result==FL_GST_SESSION_CANCELLED);cancellationUs.store(g_get_monotonic_time()-began);
    auto*bus=gst_element_get_bus(audio.pipe);CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);
    sourceCancelled.signal();sourceClose.wait();
   }else{
    FlGstVideoEvent end{FL_GST_VIDEO_END,8,44000000,0};CHECK(fl_gst_session_send_video_event(source,cfg.generation,&end)==FL_GST_SESSION_READY);
    CHECK(fl_gst_session_video_eos(source,cfg.generation,8,44000000)==FL_GST_SESSION_READY);CHECK(gst_app_src_end_of_stream(GST_APP_SRC(audio.port))==GST_FLOW_OK);
    avTerminal(audio.pipe,GST_MESSAGE_EOS);CHECK(fl_gst_session_finish_local(source,TRUE,5000,nullptr));
   }
  }
  flClose(writer);
 });
 flsample::LocalNativeRelaySetup setup{};GError*error=nullptr;
 CHECK(flsample::setupLocalNativeRelay(up,down,inputPath.c_str(),outputPath.c_str(),&caps,&candidate,1,2,80,5000,5000,nullptr,&setup,&error));
 flSessionConfig original{};CHECK(fl_gst_session_get_config(up,&original)==FL_GST_SESSION_READY);
 {
  flsample::SessionNativeRelay video(up,down,setup.input,setup.output,[](flImage*a,flImage*b){return relaytest::sameAllocation(a,b);});CHECK(video.initialize()==FL_OK);
  LocalProcessAudio input(up,false),output(down,true);flsample::SessionPcmRelay pcm(up,down,GST_APP_SINK(input.port),GST_APP_SRC(output.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);
  output.start();input.start();flsample::SessionRelaySupervisor supervisor(up,down);CHECK(supervisor.start()==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_local_ready(down,TRUE,1000,nullptr)&&fl_gst_session_local_ready(up,FALSE,1000,nullptr));
  FlGstVideoEvent barrier{FL_GST_VIDEO_BARRIER,0,37000000,0};CHECK(fl_gst_session_send_video_event(down,original.generation,&barrier)==FL_GST_SESSION_READY);
  for(unsigned n=0;n<(cancelPaused?4u:8u);++n){
   advance.signal();published.wait();flFrame frame{};CHECK(flAcquireFrame(setup.input,&frame,1000)==FL_OK&&frame.ptsNs==gint64(37000000+n*1000000));CHECK(video.forward(frame)==FL_OK);
   const auto audioDeadline=g_get_monotonic_time()+5000000;while(pcm.blockCount()<n+1){auto r=pcm.forward(20);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<audioDeadline);}
   read.wait(); // one coordinated frame in flight: the bin pulls n before n+1 is published
   if(n==3){
    notice.wait();CHECK(submitted.load()==4&&published.idle());
    CHECK(video.collect()==FL_OK&&video.pending()==1&&!video.videoEnded());
    flSessionConfig still{};CHECK(fl_gst_session_get_config(up,&still)==FL_GST_SESSION_READY&&memcmp(&still,&original,sizeof original)==0);
    const auto began=g_get_monotonic_time();command.signal();notice.wait();
    CHECK(g_get_monotonic_time()-began<1500000&&submitted.load()==4);
    if(cancelPaused){
     retired.signal();sourceCancelled.wait();
     const auto cancelElapsed=g_get_monotonic_time()-began;
     CHECK(cancelElapsed<1500000&&supervisor.status()!=FL_GST_SESSION_READY);
     CHECK(video.pending()==1&&!video.videoEnded()&&pcm.blockCount()==4&&pcm.frameCount()==192&&pcm.byteCount()==768);
     FlGstVideoEvent absent{};CHECK(fl_gst_session_receive_video_event(up,original.generation,0,&absent)!=FL_GST_SESSION_READY);
     auto*bus=gst_element_get_bus(input.pipe);CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);
     command.signal();notice.wait(); // real GstMemory unref, not manufactured native completion
     command.signal();child.join(); // no reader survives graph disposal
     supervisor.stop();sourceClose.signal();producer.join();
     printf("paused FAR5 NULL propagated source cancellation in %.3fms (source observation %.3fms); no EOS, four submissions only\n",cancelElapsed/1000.0,cancellationUs.load()/1000.0);
     break;
    }
    CHECK(video.collect()==FL_OK&&video.pending()==1&&!video.videoEnded());
    command.signal();notice.wait(); // allow actual reader release only after proving resume preserved ownership
   }else if(n==7){notice.wait();CHECK(video.collect()==FL_OK&&video.pending()==1);}
   const auto deadline=g_get_monotonic_time()+2000000;while(video.pending()>1){CHECK(g_get_monotonic_time()<deadline&&video.collect()==FL_OK);g_usleep(100);}
   retired.signal();
  }
  if(!cancelPaused){
   CHECK(submitted.load()==8&&video.count()==8&&pcm.blockCount()==8&&pcm.frameCount()==384&&pcm.byteCount()==1536);
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY&&video.videoEnded());avTerminal(input.pipe,GST_MESSAGE_EOS);
   CHECK(video.pending()==1&&pcm.finish(video,0)==FL_GST_SESSION_TIMEOUT);CHECK(pcm.endPublication(video,5000)==FL_GST_SESSION_READY);avTerminal(output.pipe,GST_MESSAGE_EOS);
   notice.wait();supervisor.stop();CHECK(supervisor.status()==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));command.signal();notice.wait();
   const auto deadline=g_get_monotonic_time()+2000000;while(video.pending()){
    const auto collected=video.collect();
    if(collected!=FL_OK||g_get_monotonic_time()>=deadline){
     gchar*log=nullptr;gsize length=0;if(g_file_get_contents((outputPath+".pause-reader.log").c_str(),&log,&length,nullptr)){fwrite(log,1,length,stderr);g_free(log);}
     flChannelInfo inputInfo{},outputInfo{};const auto inputQuery=flQuery(setup.input,&inputInfo),outputQuery=flQuery(setup.output,&outputInfo);
     fprintf(stderr,"pause final collect=%d pending=%u inputQuery=%d inputGeneration=%llu outputQuery=%d outputGeneration=%llu\n",int(collected),video.pending(),int(inputQuery),(unsigned long long)inputInfo.generation,int(outputQuery),(unsigned long long)outputInfo.generation);CHECK(false);
    }
    g_usleep(100);
   }
   CHECK(video.finish()==FL_OK);command.signal();child.join();
   CHECK(fl_gst_session_finish_local(up,FALSE,1000,nullptr));producer.join();
  }
 }
 flsample::releaseLocalPoolSnapshot(setup.snapshot);flClose(setup.output);flClose(setup.input);g_object_unref(source);g_object_unref(up);g_object_unref(down);
}
#endif
}
#if defined(FL_GST_LOCAL_NATIVE_RELAY_POOL) && !defined(__ANDROID__) // Android: phase 4
#include "fl_gst_native_bin_relay.inc"
#include "fl_gst_native_relay_av.inc"
#include "fl_gst_native_relay_process.inc"
#include "fl_gst_native_relay_automatic.inc"
#endif

#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(gio);}
#endif
int main(int argc,char**argv){
 gst_init(&argc,&argv);CHECK(argc>=3);
#ifdef __ANDROID__
 // Android links GStreamer and the plugin statically; argv[1] is a placeholder
 // kept so child command lines have the same shape on every platform.
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
 GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 if(!dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId")){puts("SKIP: AHardwareBuffer_getId unavailable (API 31+)");return 77;}
#else
 GError*e=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&e);CHECK(plugin&&!e);gst_object_unref(plugin);
#endif
 const std::string mode=argv[2];
#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
 if(argc==7&&mode.rfind("--relay-local-reader",0)==0){
  relaytest::Gate held(argv[4]),release(argv[5]),exit(argv[6]);
  if(mode=="--relay-local-reader")return localpool::reader(argv[3],held,release,exit);
  if(mode=="--relay-local-reader-generic")return localpool::reader(argv[3],held,release,exit,false);
  if(mode=="--relay-local-reader-av")return localpool::reader(argv[3],held,release,exit,false,true);
  if(mode=="--relay-local-reader-failed")return localpool::reader(argv[3],held,release,exit,false,true,true);
 }
 if(argc==8&&mode=="--relay-local-reader-bin"){
  relaytest::Gate held(argv[4]),release(argv[5]),exit(argv[6]),read(argv[7]);
  return localpool::binReader(argv[3],held,release,exit,read);
 }
#ifndef __ANDROID__ // Android: phase 4
 if(argc==8&&mode=="--relay-local-pause-reader"){
  relaytest::Gate notice(argv[4]),command(argv[5]),read(argv[6]);
  return controls::pauseReader(argv[3],notice,command,read,std::string(argv[7])=="1");
 }
 {int code=0;if(relayprocess::child(argc,argv,&code))return code;}
#endif
#endif
 if(argc==7&&(mode=="--relay-local-source"||mode=="--relay-local-source-av")){
  relaytest::Gate advance(argv[4]),published(argv[5]),retired(argv[6]);
  return localsource::producer(argv[3],advance,published,retired,mode=="--relay-local-source-av");
 }
 CHECK(argc==3);
 if(mode=="--relay-local-session"){nativeLocalRelaySession();return 0;}
 if(mode=="--relay-local-source-control"){localsource::control(argv[1]);return 0;}
 if(mode=="--video-relay-event-controls"){controls::videoRelayEventControls();return 0;}
 if(mode=="--relay-supervisor-controls"){controls::relaySupervisorControls();return 0;}
#ifdef FL_GST_LOCAL_NATIVE_RELAY_POOL
 if(mode=="--relay-local-process"){localpool::process(argv[1]);return 0;}
 if(mode=="--relay-local-pool-failures"){localpool::failures();return 0;}
 if(mode=="--relay-local-three-process"){graph::threeProcess(argv[1]);return 0;}
 if(mode=="--relay-three-local-av"){graph::threeProcess(argv[1],true);return 0;}
 if(mode=="--relay-local-replacement"){graph::threeProcess(argv[1],true,true);return 0;}
 if(mode=="--relay-local-recovery"){graph::threeProcess(argv[1],true,true,nullptr,true);return 0;}
 if(mode=="--relay-local-bin"){graph::threeProcess(argv[1],true,false,nullptr,false,true);return 0;}
 if(mode=="--relay-local-bin-negatives"){localpool::binNegatives();return 0;}
 if(mode=="--relay-setup-negatives"){graph::setupNegatives();return 0;}
#ifndef __ANDROID__ // Android: phase 4
 if(mode=="--relay-local-bin-pause"){controls::pauseControl(argv[1],false);return 0;}
 if(mode=="--relay-local-bin-paused-cancel"){controls::pauseControl(argv[1],true);return 0;}
 if(mode=="--bin-relay"){binrelay::run(0);return 0;}
 if(mode=="--lease-relay"){binrelay::run(0,true);return 0;}
 if(mode=="--bin-relay-cancel"){binrelay::run(6);return 0;}
 if(mode=="--bin-relay-events"){binrelay::run(7);return 0;}
 if(mode=="--bin-relay-supervised-downstream"){binrelay::run(8);return 0;}
 if(mode=="--bin-relay-supervised-upstream"){binrelay::run(9);return 0;}
 if(mode=="--relay-pool-lifetime"){binrelay::run(10);return 0;}
 if(mode=="--relay-authenticated-pool"){binrelay::run(11);return 0;}
 if(mode=="--relay-local-pool"){binrelay::run(12);return 0;}
 if(mode=="--relay-local-cancel-downstream"){binrelay::run(13);return 0;}
 if(mode=="--relay-local-cancel-upstream"){binrelay::run(14);return 0;}
 if(mode=="--relay-local-events"){binrelay::run(15);return 0;}
 if(mode=="--relay-pool-controls"){binrelay::relayPoolControls();return 0;}
 if(mode=="--relay-av-diagnostic"){relayav::nativeRelayAv(false);relayav::nativeRelayAv(true);for(unsigned fault=1;fault<=18;++fault)relayav::nativeRelayAv(false,fault);relayav::nativeRelayAv(true,14);return 0;}
 if(mode=="--relay-process-diagnostic"){relayprocess::run(argv[1],false);relayprocess::run(argv[1],true);return 0;}
 if(mode=="--relay-reservation-diagnostic"){relayprocess::run(argv[1],false,true);return 0;}
 if(mode=="--relay-death-diagnostic"){relayprocess::run(argv[1],false,false,false,1);relayprocess::run(argv[1],false,true,false,1);return 0;}
 if(mode=="--relay-downstream-death-diagnostic"){relayprocess::downstreamDeath(argv[1],false);relayprocess::downstreamDeath(argv[1],true);return 0;}
 if(mode=="--relay-cancel-diagnostic"){relayprocess::run(argv[1],false,false,false,2);relayprocess::run(argv[1],false,true,false,2);return 0;}
 if(mode=="--relay-decoder-diagnostic"){if(!relayprocess::decoderAvailable())return 77;relayprocess::run(argv[1],false,true,true);return 0;}
 if(mode=="--automatic-relay-negative"){autorelay::negative();return 0;}
#endif
#endif
 fprintf(stderr,"unknown mode %s\n",mode.c_str());return 2;
}

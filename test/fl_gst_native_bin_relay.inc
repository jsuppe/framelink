// In-process GStreamer bin relays, shared by Windows (D3D11) and desktop Linux
// (DMA-BUF); included by fl_gst_native_relay.cpp. A real native producer
// (framelinkavsink with native-memory, writing into its own pool) feeds a
// first framelinkavsrc; the relay republishes each native reader GstMemory into
// a same-allocation ring read by a final framelinkavsrc. Platform parts are in
// fl_gst_native_relay_platform.h.
//
// Broker-owned positive composition fixture, not an automatic discovery API.
// The first avsrc has already authenticated/consumed upstream video events.
// This bounded fixture republishes barriers and optionally forwards actual
// serialized GAP observations; arbitrary events/discovery are not supported.
#include "../samples/session_video_relay_events.h"
#include <mutex>
namespace binrelay {
static void avStreamPair(GSocketConnection**a,GSocketConnection**b){
 auto*l=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(l,nullptr,nullptr);CHECK(port);
 std::thread peer([&]{*a=g_socket_listener_accept(l,nullptr,nullptr,nullptr);});
 auto*c=g_socket_client_new();g_socket_client_set_timeout(c,2);*b=g_socket_client_connect_to_host(c,"127.0.0.1",port,nullptr,nullptr);
 peer.join();CHECK(*a&&*b);g_object_unref(c);g_object_unref(l);
}
// A bounded observation queue: callbacks never perform network writes.
struct BinRelayEvents {
 std::mutex mutex;
 FlGstVideoEvent gaps[32]{};unsigned size=0;guint64 frames=0,discont=0;
 static GstPadProbeReturn observe(GstPad*,GstPadProbeInfo*info,gpointer data){
  auto&self=*static_cast<BinRelayEvents*>(data);std::lock_guard<std::mutex> lock(self.mutex);
  if(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_BUFFER){
   if(GST_BUFFER_FLAG_IS_SET(GST_PAD_PROBE_INFO_BUFFER(info),GST_BUFFER_FLAG_DISCONT))self.discont|=1ull<<self.frames;
   CHECK(self.frames<8);++self.frames;
  }else if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info))==GST_EVENT_GAP){
   CHECK(self.size<32);GstClockTime pts,duration;gst_event_parse_gap(GST_PAD_PROBE_INFO_EVENT(info),&pts,&duration);
   self.gaps[self.size++]={FL_GST_VIDEO_GAP,self.frames,pts,duration};
  }
  return GST_PAD_PROBE_OK;
 }
 void attach(GstElement*element){auto*pad=gst_element_get_static_pad(element,"sink");CHECK(pad);
  gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),observe,this,nullptr);gst_object_unref(pad);}
};
struct BrokerNativeBinRelay {
 FlGstSession *up,*sender=fl_gst_session_new(3),*receiver=fl_gst_session_new(3);
 GstElement *tx=nullptr,*rx=nullptr,*input=nullptr,*video=nullptr,*audio=nullptr;
 relaytest::GstRelay& native;
 flSessionConfig config{};
 GstMemory* held=nullptr;
 guint64 count=0,pcmFrames=0;gint64 last=-1;
 bool localControl=false,failureObserved=false;
 bool events=false;unsigned forwardedGaps=0;BinRelayEvents upstreamEvents,downstreamEvents;
 flsample::SessionVideoRelayEvents eventAdapter;
 std::unique_ptr<flsample::SessionRelayContract> eventContract;
 std::atomic<unsigned> supervisorFailures{0};
 std::unique_ptr<flsample::SessionRelaySupervisor> supervisor;
 void startSupervision(){
  supervisor=std::make_unique<flsample::SessionRelaySupervisor>(up,sender,[this]{eventAdapter.cancel();++supervisorFailures;});
  CHECK(supervisor->start()==FL_GST_SESSION_READY);
  CHECK(supervisor->start()==FL_GST_SESSION_INVALID);
 }
 void observeEvents(GstElement*upstream,bool withGaps=true){
  events=withGaps;upstreamEvents.attach(upstream);downstreamEvents.attach(video);
  eventContract=std::make_unique<flsample::SessionRelayContract>(up,sender);
  CHECK(eventContract->initialize()==FL_GST_SESSION_READY);
  auto*pad=gst_element_get_static_pad(upstream,"sink");CHECK(pad);
  gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM|GST_PAD_PROBE_TYPE_EVENT_FLUSH),
    flsample::SessionVideoRelayEvents::probe,&eventAdapter,nullptr);gst_object_unref(pad);
 }
 void forwardGaps(){
  if(!eventContract)return;
  CHECK(eventContract->check()==FL_GST_SESSION_READY);
  CHECK(eventAdapter.drain(count,[&](const FlGstVideoEvent& gap){
   auto result=eventContract->check();
   if(result==FL_GST_SESSION_READY)result=fl_gst_session_send_video_event(sender,config.generation,&gap);
   if(result==FL_GST_SESSION_READY)++forwardedGaps;
   return result;
  })==FL_GST_SESSION_READY);
 }
 void verifyEvents(){
  if(!events)return;
  std::lock_guard<std::mutex> a(upstreamEvents.mutex),b(downstreamEvents.mutex);
  CHECK(upstreamEvents.frames==8&&downstreamEvents.frames==8&&forwardedGaps==3);
  CHECK(upstreamEvents.size==3&&downstreamEvents.size==3);
  const guint64 positions[]={4,4,8},times[]={41000000,41250000,46000000},durations[]={250000,750000,500000};
  for(unsigned i=0;i<3;++i){const auto&u=upstreamEvents.gaps[i];const auto&d=downstreamEvents.gaps[i];
   CHECK(u.afterFrames==positions[i]&&u.ptsNs==times[i]&&u.durationNs==durations[i]);
   CHECK(d.afterFrames==u.afterFrames&&d.ptsNs==mapped(u.ptsNs)&&d.durationNs==u.durationNs);
  }
  CHECK((upstreamEvents.discont&17)==17&&downstreamEvents.discont==upstreamEvents.discont);
  puts("broker bin serialized GAP: exact 4,4,8 frame boundaries, negotiated mapping, durations 250/750/500us, DISCONT preserved, trailing GAP before EOS");
 }
 BrokerNativeBinRelay(FlGstSession* source,const flSessionConfig& original,
   const GstFrameLinkMemoryPolicy& policy,flChannel* channel,relaytest::GstRelay& adapter,
   FlGstSession*localSender=nullptr,FlGstSession*localReceiver=nullptr)
   :up(source),native(adapter),config(original){
  localControl=localSender&&localReceiver;
  if(localControl){
   g_object_unref(sender);g_object_unref(receiver);
   sender=static_cast<FlGstSession*>(g_object_ref(localSender));receiver=static_cast<FlGstSession*>(g_object_ref(localReceiver));
   CHECK(fl_gst_session_get_config(sender,&config)==FL_GST_SESSION_READY);
  }else{
  config.sessionId.bytes[0]^=0x40;
  flSessionCapabilities caps{};caps.version=config.version;caps.videoCount=caps.audioCount=1;
  caps.video[0]=config.video;caps.audio[0]=config.audio;
  for(auto*s:{sender,receiver})CHECK(fl_gst_session_configure(s,&config,&caps,&caps)==FL_GST_SESSION_READY);
  guint8 secret[32]{};secret[0]=99;auto*key=g_bytes_new(secret,32);
  for(unsigned kind=0;kind<3;++kind){
   GSocketConnection*a=nullptr,*b=nullptr;avStreamPair(&a,&b);gboolean ok=FALSE;
   auto auth=[&](FlGstSession*s,GSocketConnection*c,bool producing){
    if(kind==2)return fl_gst_session_authenticate_native_memory(s,c,producing,key,&policy,1000,nullptr,nullptr);
    auto fn=kind?fl_gst_session_authenticate_event_stream:fl_gst_session_authenticate_audio_stream;
    return fn(s,c,producing,key,1000,nullptr,nullptr);
   };
   std::thread peer([&]{ok=auth(sender,a,true);});CHECK(auth(receiver,b,false));peer.join();CHECK(ok);
   g_object_unref(a);g_object_unref(b);
  }
  g_bytes_unref(key);
  flClockMapping mapping{};mapping.sourceClockId=config.clockId;mapping.targetClockId.bytes[0]=9;
  mapping.sourceAnchorNs=config.epochNs;mapping.targetAnchorNs=5000000;
  CHECK(fl_gst_session_set_mapping(receiver,config.generation,&mapping,0)==FL_GST_SESSION_READY);
  }
  flsample::SessionRelayContract contract(up,sender);CHECK(contract.initialize()==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_register_track(sender,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
  GError*error=nullptr;
  tx=gst_parse_launch("appsrc name=input is-live=true format=time max-bytes=192 block=false caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av",&error);CHECK(tx&&!error);
  rx=gst_parse_launch("framelinkavsrc name=transport native-memory=true video-format=RGBA width=64 height=32 framerate=30/1 transport.video_src ! appsink name=video max-buffers=1 sync=false async=false enable-last-sample=false transport.audio_src ! appsink name=audio max-buffers=1 sync=false async=false enable-last-sample=false",&error);CHECK(rx&&!error);
  auto*src=gst_bin_get_by_name(GST_BIN(rx),"transport");gboolean accepted=FALSE;
  g_signal_emit_by_name(src,"adopt-channel",channel,&accepted);CHECK(accepted);
  auto reverse=policy;std::swap(reverse.requirements[0],reverse.requirements[1]);std::swap(reverse.paths[0],reverse.paths[1]);
  g_signal_emit_by_name(src,"apply-memory-policy",&reverse,&accepted);CHECK(accepted);gst_object_unref(src);
  input=gst_bin_get_by_name(GST_BIN(tx),"input");video=gst_bin_get_by_name(GST_BIN(rx),"video");audio=gst_bin_get_by_name(GST_BIN(rx),"audio");
  // The policy is borrowed from the enclosing fixture and outlives this object.
  g_object_set(video,"emit-signals",TRUE,nullptr);
  g_signal_connect(video,"propose-allocation",G_CALLBACK(nativeAvReaderAllocation),const_cast<GstFrameLinkMemoryPolicy*>(&policy));
  for(auto entry:{std::make_pair(tx,sender),std::make_pair(rx,receiver)}){
   auto*c=fl_gst_session_create_context(entry.second);gst_element_set_context(entry.first,c);gst_context_unref(c);
  }
  CHECK(fl_gst_session_track_ready(sender,FL_GST_TRACK_VIDEO,config.generation)==FL_GST_SESSION_READY);
  CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  if(localControl){
   CHECK(fl_gst_session_local_ready(receiver,FALSE,1000,nullptr));
   CHECK(fl_gst_session_local_ready(sender,TRUE,1000,nullptr));
  }
 }
 guint64 mapped(guint64 pts){
  gint64 result=-1;CHECK(fl_gst_session_map_running_time(receiver,config.generation,pts,TRUE,&result)==FL_GST_SESSION_READY&&result>=0);
  return guint64(result);
 }
 void forward(GstSample* vs,GstSample* as,unsigned n){
  flsample::SessionRelayContract contract(up,sender);CHECK(contract.initialize()==FL_GST_SESSION_READY);
  auto*v=gst_sample_get_buffer(vs);auto*a=gst_sample_get_buffer(as);
  const guint64 offset=events&&n>=4?1000000:0;
  CHECK(v&&a&&n==count&&GST_BUFFER_PTS(v)==37000000+n*1000000+offset&&GST_BUFFER_PTS(a)==n*1000000+offset);
  for(auto*sample:{vs,as}){
   GstClockTime running=GST_CLOCK_TIME_NONE;auto*b=gst_sample_get_buffer(sample);
   CHECK(fl_gst_segment_running_time(gst_sample_get_segment(sample),GST_BUFFER_PTS(b),&running)&&running==GST_BUFFER_PTS(b));
  }
  CHECK(gst_buffer_get_size(a)==192&&GST_BUFFER_DURATION(a)==1000000);
  forwardGaps();
  if(!count||GST_BUFFER_FLAG_IS_SET(v,GST_BUFFER_FLAG_DISCONT)){
   FlGstVideoEvent barrier{FL_GST_VIDEO_BARRIER,count,GST_BUFFER_PTS(v),0};
   CHECK(fl_gst_session_send_video_event(sender,config.generation,&barrier)==FL_GST_SESSION_READY);
  }
  CHECK(native.forward(v)==FL_OK);
  CHECK(gst_app_src_get_current_level_bytes(GST_APP_SRC(input))==0);
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(input),gst_buffer_ref(a))==GST_FLOW_OK);
  auto*outV=gst_app_sink_try_pull_sample(GST_APP_SINK(video),2*GST_SECOND);
  auto*outA=gst_app_sink_try_pull_sample(GST_APP_SINK(audio),2*GST_SECOND);CHECK(outV&&outA);
  auto*vb=gst_sample_get_buffer(outV);auto*ab=gst_sample_get_buffer(outA);
  CHECK(GST_BUFFER_PTS(vb)==mapped(GST_BUFFER_PTS(v))&&GST_BUFFER_DTS(vb)==mapped(GST_BUFFER_DTS(v)));
  CHECK(GST_BUFFER_DURATION(vb)==GST_BUFFER_DURATION(v));
  CHECK(GST_BUFFER_PTS(ab)==mapped(GST_BUFFER_PTS(a))&&GST_BUFFER_DURATION(ab)==GST_BUFFER_DURATION(a));
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000);
  if(events&&n==4)CHECK(GST_BUFFER_FLAG_IS_SET(a,GST_BUFFER_FLAG_DISCONT)&&GST_BUFFER_FLAG_IS_SET(ab,GST_BUFFER_FLAG_DISCONT));
  auto*metadata=gst_buffer_get_framelink_frame_meta(vb);CHECK(metadata&&metadata->frame.streamGeneration==config.generation);
  auto*memory=gst_buffer_peek_memory(vb,0);CHECK(relaytest::isNativeMemory(memory));
  CHECK(relaytest::sameMemory(memory,gst_buffer_peek_memory(v,0)));
  auto*meta=gst_buffer_get_video_meta(vb);GstMapInfo map{};CHECK(meta&&gst_memory_map(memory,&map,GST_MAP_READ));
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(map.data[meta->offset[0]+y*meta->stride[0]+x]==n+1);
  gst_memory_unmap(memory,&map);
  guint8 pcm[192]{};CHECK(gst_buffer_get_size(ab)==sizeof pcm&&gst_buffer_extract(ab,0,pcm,sizeof pcm)==sizeof pcm);
  for(auto value:pcm)CHECK(value==n+31);
  if(n==7)held=gst_memory_ref(memory);
  gst_sample_unref(outV);gst_sample_unref(outA);
  ++count;pcmFrames+=48;last=GST_BUFFER_PTS(v);
  auto collected=native.collect();fprintf(stderr,"bin relay n=%u collect=%d pending=%u\n",n,int(collected),native.pending());CHECK(collected==FL_OK);
 }
 // Explicit broker failure propagation, not autonomous bin-to-bin wiring.
 void cancelDownstream(bool supervised=false,GstElement*failedUpstream=nullptr){
  CHECK(count==8&&pcmFrames==384&&held&&native.pending()==1);
  flSessionConfig snapshot{};
  CHECK(fl_gst_session_wait(sender,config.generation,1000,&snapshot)==FL_GST_SESSION_READY);
  const auto began=g_get_monotonic_time();failureObserved=true;
  CHECK(gst_element_set_state(failedUpstream?failedUpstream:rx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  FlGstSessionResult result=FL_GST_SESSION_READY;
  do{result=fl_gst_session_wait(sender,config.generation,50,&snapshot);
   if(result!=FL_GST_SESSION_READY)break;g_usleep(1000);
  }while(g_get_monotonic_time()-began<1000000);
  CHECK(result==FL_GST_SESSION_CANCELLED);
  if(supervised){
   CHECK(supervisor);
   const auto deadline=g_get_monotonic_time()+1000000;
   while(supervisor->status()==FL_GST_SESSION_READY&&g_get_monotonic_time()<deadline)g_usleep(1000);
   CHECK(supervisor->status()==FL_GST_SESSION_CANCELLED);
   supervisor->stop();
   CHECK(supervisorFailures.load()==1);
   // No polling/publication path invokes cancel(up): the idle monitor must
   // propagate the actual endpoint failure and cancel event publication.
   CHECK(eventAdapter.status()==FL_GST_SESSION_CANCELLED);
  }
  if(eventContract){
   // Broker conveys the actual downstream failure, even with no queued GAP.
   if(!supervised)eventAdapter.cancel();
   CHECK(eventAdapter.drain(count,[](const FlGstVideoEvent&){CHECK(false);return FL_GST_SESSION_READY;})==FL_GST_SESSION_CANCELLED);
  }
  // Failure is not permission to retire a native reader or synthesize END.
  CHECK(native.pending()==1&&native.finish()==FL_BUSY);
  guint64 frames=999;gint64 pts=999;
  CHECK(fl_gst_session_wait_video_eos(sender,config.generation,0,&frames,&pts)==FL_GST_SESSION_CANCELLED);
  CHECK(frames==999&&pts==999);
  for(auto*p:{tx,rx}){auto*bus=gst_element_get_bus(p);
   CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);}
  if(!supervised)fl_gst_session_cancel(up); // legacy explicit broker control
  CHECK(fl_gst_session_wait(up,config.generation,1,&snapshot)==FL_GST_SESSION_CANCELLED);
  const auto elapsed=g_get_monotonic_time()-began;CHECK(elapsed<1500000);
  printf("broker bin %s cancellation: %.3fms, %s propagation, no END/EOS, final reader still held\n",
    failedUpstream?"upstream":"downstream",elapsed/1000.0,supervised?"automatic idle supervisor":"explicit broker");
 }
 void terminal(){
  guint64 frames=0;gint64 pts=-1;
  CHECK(fl_gst_session_wait_video_eos(up,config.generation,0,&frames,&pts)==FL_GST_SESSION_READY);
  CHECK(frames==count&&pts==last&&count==8&&pcmFrames==384);
  auto*bus=gst_element_get_bus(rx);CHECK(!gst_bus_pop_filtered(bus,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS)));gst_object_unref(bus);
  forwardGaps();
  FlGstVideoEvent end{FL_GST_VIDEO_END,count,guint64(last),0};
  CHECK(fl_gst_session_send_video_event(sender,config.generation,&end)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_video_eos(sender,config.generation,count,last)==FL_GST_SESSION_READY);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(input))==GST_FLOW_OK);
  avTerminal(tx,GST_MESSAGE_EOS);avTerminal(rx,GST_MESSAGE_EOS);verifyEvents();
  CHECK(fl_gst_session_wait_video_eos(receiver,config.generation,0,&frames,&pts)==FL_GST_SESSION_READY&&frames==count&&pts==last);
  CHECK(native.finish()==FL_BUSY&&held);
  if(supervisor){supervisor->stop();CHECK(supervisor->status()==FL_GST_SESSION_READY&&supervisorFailures.load()==0);}
 }
 void releaseAfterUpstreamTeardown(){
  if(supervisor)supervisor->stop();
  CHECK(held&&native.finish()==FL_BUSY);
  // Both downstream bin and the extra reader-memory reference keep the original
  // upstream lease alive until actual final completion, not merely EOS.
  CHECK(gst_element_set_state(tx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  CHECK(gst_element_set_state(rx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  for(auto*o:{input,video,audio,tx,rx})gst_object_unref(o);
  CHECK(native.finish()==FL_BUSY);
  GstMapInfo map{};CHECK(gst_memory_map(held,&map,GST_MAP_READ));CHECK(map.data[0]==8);gst_memory_unmap(held,&map);
  gst_memory_unref(held);held=nullptr;
  auto deadline=g_get_monotonic_time()+1000000;
  while(native.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(native.collect()==FL_OK);g_usleep(100);}
  CHECK(native.finish()==FL_OK);
  if(localControl&&!failureObserved){
   CHECK(fl_gst_session_finish_local(receiver,FALSE,1000,nullptr));
   CHECK(fl_gst_session_finish_local(sender,TRUE,1000,nullptr));
   puts("local native relay: retained authenticated discovery sessions, mapped A/V, genuine final-reader completion before drain acknowledgment");
  }
  g_object_unref(sender);g_object_unref(receiver);
  puts("broker native bin relay: 8 frames over two original allocations / 384 PCM frames; identity intermediate mapping, negotiated final mapping and exact 37ms A/V, authenticated session; final reader retained beyond upstream and downstream bin teardown");
 }
};

static void relayPoolControls(){
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;
 cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
 cfg.video={FL_FORMAT_RGBA8,64,32,30,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 auto*pool=gst_buffer_pool_new();auto*settings=gst_buffer_pool_get_config(pool);
 auto*caps=gst_caps_from_string("video/x-raw,format=RGBA,width=64,height=32,framerate=30/1");
 gst_buffer_pool_config_set_params(settings,caps,8192,2,2);gst_caps_unref(caps);
 CHECK(gst_buffer_pool_set_config(pool,settings));flChannel*r=nullptr,*w=nullptr;
 CHECK(relaytest::createRelayPool(pool,"unused",cfg,2,false,&r,&w)==FL_INVALID);
 CHECK(!r&&!w&&gst_buffer_pool_set_active(pool,TRUE));
 CHECK(relaytest::createRelayPool(pool,"unused",cfg,2,false,&r,&r)==FL_INVALID);
 for(unsigned count:{0u,1u,17u})CHECK(relaytest::createRelayPool(pool,"unused",cfg,count,false,&r,&w)==FL_INVALID);
 CHECK(relaytest::createRelayPool(pool,"unused",cfg,2,false,&r,&w)==FL_FORMAT_UNSUPPORTED);
 CHECK(!r&&!w);
 GstBuffer*buffers[2]{};GstBufferPoolAcquireParams nowait{};nowait.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
 for(auto&b:buffers)CHECK(gst_buffer_pool_acquire_buffer(pool,&b,&nowait)==GST_FLOW_OK);
 for(auto*b:buffers)gst_buffer_unref(b);
 CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
 puts("relay pool controls: inactive/unbounded/aliased-output requests rejected; foreign CPU pool rejected with every temporary buffer returned");
}

// The relay modes of the original native producer fixture (always a native
// receiver, broker-composed sessions):
//   0 bin relay; lease=true: the read-lease adapter alone, final reader in-test
//   6 bin, explicit downstream cancellation   7 bin, serialized GAP/DISCONT events
//   8 bin, supervised downstream cancellation  9 bin, supervised upstream cancellation
//  10 100 pool setup/teardown cycles, no handle or fd growth
//  11 broker-keyed authenticated pool transfer (with its negatives)
//  12 local FAR5 pool   13/14 local FAR5 downstream/upstream cancellation   15 local FAR5 events
static void run(unsigned mode,bool leaseRelay=false){
 const bool binRelay=!leaseRelay;
 const bool localPoolRelay=mode>=12&&mode<=15;
 const bool relayEvents=mode==7||mode==15;
 fprintf(stderr,"native relay mode %u lease=%d\n",mode,leaseRelay);
 auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;
 cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
 cfg.video={FL_FORMAT_RGBA8,64,32,30,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 // This fixture deliberately spends seconds exercising pre-media pool setup.
 // Keep the usual one-second failure controls unchanged in every other mode.
 if(mode==10||localPoolRelay)cfg.connectTimeoutMs=cfg.idleTimeoutMs=30000;
 if(localPoolRelay){auto*clock=gst_system_clock_obtain();cfg.epochNs=gst_clock_get_time(clock)-GST_SECOND;gst_object_unref(clock);}
 flSessionCapabilities offer{};offer.version=2;offer.videoCount=1;offer.video[0]=cfg.video;offer.audioCount=1;offer.audio[0]=cfg.audio;
 flChannel*channel=nullptr;flChannel*producer=nullptr;
 auto policy=nativeAvPolicy(cfg.generation,true);
 GSocketConnection*audioPeer=nullptr;
 CHECK(fl_gst_session_configure(s,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
 CHECK(fl_gst_session_configure(r,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
 {
  guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);
  for(bool audio:{false,true}){
   GSocketConnection*a=nullptr,*b=nullptr;avStreamPair(&a,&b);gboolean ok=FALSE;
   auto authenticate=audio?fl_gst_session_authenticate_audio_stream:fl_gst_session_authenticate_event_stream;
   std::thread peer([&]{ok=authenticate(s,a,TRUE,key,1000,nullptr,nullptr);});
   CHECK(authenticate(r,b,FALSE,key,1000,nullptr,nullptr));peer.join();CHECK(ok);
   g_object_unref(a);if(audio)audioPeer=b;else g_object_unref(b);
  }
  g_bytes_unref(key);
 }
 flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=binRelay?0:1000000;mapping.sourceAnchorNs=cfg.epochNs;
 CHECK(fl_gst_session_set_mapping(r,1,&mapping,0)==FL_GST_SESSION_READY);
 gint64 mappedBase=0;CHECK(fl_gst_session_map_running_time(r,1,0,TRUE,&mappedBase)==FL_GST_SESSION_READY);
 auto name="native-relay-"+std::to_string(g_get_monotonic_time());
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA|FL_DEFER_RELEASE,&channel)==FL_OK);
 const std::string videoCaps=std::string("video/x-raw(")+relaytest::nativeFeature()+"),format=RGBA,width=64,height=32,framerate=30/1";
 GError*e=nullptr;
 auto*tx=gst_parse_launch(("framelinkavsink name=transport native-memory=true appsrc name=vi is-live=true format=time caps="+videoCaps+" ! transport.video_sink appsrc name=ai is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! transport.audio_sink").c_str(),&e);CHECK(tx&&!e);
 auto*rx=gst_parse_launch("framelinkavsrc name=transport video-format=RGBA width=64 height=32 framerate=30/1 transport.video_src ! appsink name=vo sync=false async=false transport.audio_src ! appsink name=ao sync=false async=false",&e);CHECK(rx&&!e);
 auto*sink=gst_bin_get_by_name(GST_BIN(tx),"transport");auto*src=gst_bin_get_by_name(GST_BIN(rx),"transport");
 g_object_set(src,"native-memory",TRUE,nullptr);
 // Both endpoints authenticate the exact producer-ordered policy before use.
 {
  guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);
  GSocketConnection*ma=nullptr,*mb=nullptr;avStreamPair(&ma,&mb);gboolean agreed=FALSE;
  std::thread agreement([&]{agreed=fl_gst_session_authenticate_native_memory(s,ma,TRUE,key,&policy,1000,nullptr,nullptr);});
  CHECK(fl_gst_session_authenticate_native_memory(r,mb,FALSE,key,&policy,1000,nullptr,nullptr));agreement.join();CHECK(agreed);
  g_io_stream_close(G_IO_STREAM(ma),nullptr,nullptr);g_io_stream_close(G_IO_STREAM(mb),nullptr,nullptr);
  g_object_unref(ma);g_object_unref(mb);g_bytes_unref(key);
 }
 CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 {
  gboolean accepted=FALSE;
  g_signal_emit_by_name(sink,"adopt-channel",producer,&accepted);CHECK(accepted);
  g_signal_emit_by_name(sink,"apply-memory-policy",&policy,&accepted);CHECK(accepted);
  gboolean adopted=FALSE;g_signal_emit_by_name(src,"adopt-channel",channel,&adopted);CHECK(adopted);
  auto consumerPolicy=policy;std::swap(consumerPolicy.requirements[0],consumerPolicy.requirements[1]);std::swap(consumerPolicy.paths[0],consumerPolicy.paths[1]);
  g_signal_emit_by_name(src,"apply-memory-policy",&consumerPolicy,&accepted);CHECK(accepted);
  auto*video=gst_bin_get_by_name(GST_BIN(sink),"video");g_object_set(video,"async",FALSE,"enable-last-sample",FALSE,nullptr);gst_object_unref(video);
  for(auto entry:{std::make_pair(tx,s),std::make_pair(rx,r)}){auto*c=fl_gst_session_create_context(entry.second);gst_element_set_context(entry.first,c);gst_context_unref(c);}
 }
 auto*vi=gst_bin_get_by_name(GST_BIN(tx),"vi");auto*ai=gst_bin_get_by_name(GST_BIN(tx),"ai");auto*vo=gst_bin_get_by_name(GST_BIN(rx),"vo");auto*ao=gst_bin_get_by_name(GST_BIN(rx),"ao");
 CHECK(g_signal_lookup("propose-allocation",G_OBJECT_TYPE(vo)));
 g_object_set(vo,"emit-signals",TRUE,"enable-last-sample",FALSE,nullptr);
 g_signal_connect(vo,"propose-allocation",G_CALLBACK(nativeAvReaderAllocation),&policy);
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 // The producer's own native pool, from a real allocation query.
 auto*caps=gst_caps_from_string(videoCaps.c_str());
 auto*q=gst_query_new_allocation(caps,TRUE);
 guint8 reqWire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&policy.requirements[0],reqWire,sizeof reqWire)==FL_MEMORY_OK);
 auto*reqBytes=g_bytes_new(reqWire,sizeof reqWire);gst_structure_set(gst_query_writable_structure(q),"framelink-memory-requirements",G_TYPE_BYTES,reqBytes,nullptr);g_bytes_unref(reqBytes);
 gst_structure_set(gst_query_writable_structure(q),"framelink-completion-contract",G_TYPE_STRING,relaytest::producerCompletion(),nullptr);
 auto*pad=gst_element_get_static_pad(sink,"video_sink");CHECK(gst_pad_query(pad,q));
 GstBufferPool*pool=nullptr;guint size=0,min=0,max=0;gst_query_parse_nth_allocation_pool(q,0,&pool,&size,&min,&max);CHECK(pool&&max==2);
 gst_query_unref(q);gst_caps_unref(caps);gst_object_unref(pad);CHECK(gst_buffer_pool_set_active(pool,TRUE));
 flChannel* leaseReader=nullptr;flChannel* leaseWriter=nullptr;
 std::unique_ptr<relaytest::GstRelay> leaseAdapter;
 std::unique_ptr<BrokerNativeBinRelay> binAdapter;
 FlGstSession*localRelaySender=nullptr,*localRelayReceiver=nullptr;
 flFrame heldRelayFrame{};uint64_t heldRelayLease=0;std::atomic<bool> finalReaderRetired{false};
 if(mode==10){
  unsigned long baseline=0;
  for(unsigned cycle=0;cycle<=100;++cycle){
   auto testName=name+"-cycle-"+std::to_string(cycle);
   flChannel*reader=nullptr,*writer=nullptr;
   CHECK(relaytest::createRelayPool(pool,testName.c_str(),cfg,2,true,&reader,&writer)==FL_OK);
   relaytest::GstRelay adapter(writer,cfg.generation);
   CHECK(adapter.initialize()==FL_OK&&adapter.pending()==0&&adapter.finish()==FL_OK);
   flClose(writer);flClose(reader);
   unsigned long handles=relaytest::openHandles();
   if(!cycle){
    // Asynchronous driver startup adds handles even without another channel
    // creation. Settle once, before the measured 100 cycles; never rebase
    // after observing their outcome.
    printf("relay pool warmup immediate=%lu\n",handles);
    g_usleep(3000000);
    baseline=handles=relaytest::openHandles();
   }
   if(cycle%10==0)printf("relay pool cleanup cycle=%u handles=%lu baseline=%lu\n",cycle,handles,baseline);
   if(cycle==100){
    const auto deadline=g_get_monotonic_time()+1000000;
    while(handles>baseline&&g_get_monotonic_time()<deadline){g_usleep(1000);handles=relaytest::openHandles();}
    CHECK(handles<=baseline);
   }
  }
  puts("relay pool cleanup: 100 complete setup/adapter-reservation/close cycles, no net handle or fd growth after one warmup and fixed 3s settle; same upstream pool remains usable");
 }
 if(mode==8){
  GstBuffer*held=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&held,nullptr)==GST_FLOW_OK);
  auto before=g_get_monotonic_time();
  CHECK(relaytest::createRelayPool(pool,(name+"-busy").c_str(),cfg,2,true,&leaseReader,&leaseWriter)==FL_BUSY);
  CHECK(!leaseReader&&!leaseWriter&&g_get_monotonic_time()-before<500000);
  gst_buffer_unref(held);
  auto wrong=cfg;wrong.video.width*=2;
  CHECK(relaytest::createRelayPool(pool,(name+"-wrong").c_str(),wrong,2,true,&leaseReader,&leaseWriter)==FL_FORMAT_UNSUPPORTED);
  CHECK(!leaseReader&&!leaseWriter);
  CHECK(relaytest::createRelayPool(pool,(name+"-partial").c_str(),cfg,3,true,&leaseReader,&leaseWriter)==FL_FORMAT_UNSUPPORTED);
  CHECK(!leaseReader&&!leaseWriter);
  puts("relay pool setup: held-slot BUSY is bounded; geometry/capacity mismatch rejects; all temporary reservations returned");
 }
 if(mode==11||localPoolRelay){
  const auto endpoint="framelink-cpu-native-relay-"+std::to_string(g_get_monotonic_time()); // the Windows pipe endpoint requires this prefix
  relaytest::Cancel cancellation;
  guint8 secret[32]{};secret[0]=93;auto*key=g_bytes_new(secret,sizeof secret);
  auto ring=name+"-lease";
  // Every negative must return no channels and all source reservations. The
  // positive immediately following is also the exact native media/lease oracle.
  for(unsigned negative=0;negative<(mode<13?4u:0u);++negative){
   auto bad=cfg;if(negative==1)++bad.generation;
   guint8 wrong[32]{};wrong[0]=94;auto*wrongKey=g_bytes_new(wrong,sizeof wrong);
   flResult received=FL_INTERNAL;flChannel*reader=nullptr,*writer=nullptr;
   const auto attempt=endpoint+"-negative-"+std::to_string(negative);
   auto before=g_get_monotonic_time();
   if(negative==3)cancellation.set();
   std::thread peer([&]{received=relaytest::createAuthenticatedRelayPool(false,nullptr,
     attempt,ring.c_str(),bad,2,negative!=2,negative==0?wrongKey:key,cancellation,500,&reader);});
   auto sent=relaytest::createAuthenticatedRelayPool(true,pool,attempt,ring.c_str(),cfg,
     2,true,key,cancellation,500,&writer);
   peer.join();CHECK(sent!=FL_OK&&received!=FL_OK&&!reader&&!writer);
   CHECK(g_get_monotonic_time()-before<1500000);cancellation.reset();
   g_bytes_unref(wrongKey);
  }
  if(localPoolRelay){
   gchar*dir=g_dir_make_tmp("framelink-relay-pool-XXXXXX",nullptr);CHECK(dir);
   std::string path=std::string(dir)+"/session";g_free(dir);
   flSessionCapabilities localRelayCaps{};localRelayCaps.version=2;localRelayCaps.videoCount=localRelayCaps.audioCount=1;
   localRelayCaps.video[0]=cfg.video;localRelayCaps.audio[0]=cfg.audio;
   auto*down=fl_gst_session_new(3);auto*final=fl_gst_session_new(3);
   auto candidate=policy;candidate.sessionGeneration=0;
   GstFrameLinkMemoryPolicy bound{},receivedPolicy{};gboolean received=FALSE;
   GError*localError=nullptr,*remoteError=nullptr;
   {
    relaytest::RelayPoolExport exported;CHECK(exported.prepare(pool,cfg,2)==FL_OK);
    std::thread peer([&]{received=fl_gst_session_open_local_native_pool_receiver(final,path.c_str(),
      &localRelayCaps,1,2,80,5000,5000,&candidate,&receivedPolicy,&leaseReader,nullptr,&remoteError);});
    auto sent=relaytest::openRelayPool(down,r,path.c_str(),&localRelayCaps,1,2,80,5000,5000,
      &candidate,exported,&bound,&leaseWriter,nullptr,&localError);
    peer.join();if(localError)fprintf(stderr,"local pool: %s\n",localError->message);
    if(remoteError)fprintf(stderr,"remote pool: %s\n",remoteError->message);
    CHECK(sent&&received&&!localError&&!remoteError&&leaseReader&&leaseWriter);
   }
   flSessionConfig negotiated{};CHECK(fl_gst_session_get_config(down,&negotiated)==FL_GST_SESSION_READY);
   CHECK(flsample::SessionRelayContract::compatible(cfg,negotiated));
   CHECK(bound.sessionGeneration==cfg.generation&&receivedPolicy.sessionGeneration==cfg.generation);
   localRelaySender=down;localRelayReceiver=final;
  }else{
   flResult received=FL_INTERNAL;
   std::thread peer([&]{received=relaytest::createAuthenticatedRelayPool(false,nullptr,
     endpoint,ring.c_str(),cfg,2,true,key,cancellation,5000,&leaseReader);});
   auto sent=relaytest::createAuthenticatedRelayPool(true,pool,endpoint,ring.c_str(),cfg,
     2,true,key,cancellation,5000,&leaseWriter);
   peer.join();
   CHECK(sent==FL_OK&&received==FL_OK&&leaseReader&&leaseWriter);
  }
  g_bytes_unref(key);
  puts("authenticated native pool: same-allocation imported ring attached");
 }else{
  CHECK(relaytest::createRelayPool(pool,(name+"-lease").c_str(),cfg,2,true,&leaseReader,&leaseWriter)==FL_OK);
 }
 leaseAdapter.reset(new relaytest::GstRelay(leaseWriter,cfg.generation));CHECK(leaseAdapter->initialize()==FL_OK);
 if(binRelay)binAdapter.reset(new BrokerNativeBinRelay(r,cfg,policy,leaseReader,*leaseAdapter,localRelaySender,localRelayReceiver));
 if(localRelaySender){g_object_unref(localRelaySender);g_object_unref(localRelayReceiver);}
 if(relayEvents||mode==6||mode==8||mode==9||localPoolRelay){CHECK(binRelay);binAdapter->observeEvents(vo,relayEvents);}
 if(relayEvents||mode==8||mode==9||localPoolRelay)binAdapter->startSupervision();
 GstMemory* retainedReceiver=nullptr;GstMemory* retainedFinal=nullptr;uint64_t retainedIdentity=0;
 const unsigned count=8;
 for(unsigned n=0;n<count;++n){
  if(relayEvents&&n==4){
   auto*vpad=gst_element_get_static_pad(vi,"src");
   CHECK(gst_pad_push_event(vpad,gst_event_new_gap(41000000,250000)));
   CHECK(gst_pad_push_event(vpad,gst_event_new_gap(41250000,750000)));gst_object_unref(vpad);
  }
  GstBuffer*b=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK);
  auto*mem=gst_buffer_peek_memory(b,0);CHECK(relaytest::isNativeMemory(mem));
  relaytest::fillPoolBuffer(sink,b,static_cast<unsigned char>(n+1));
  const guint64 audioPts=n*1000000+(relayEvents&&n>=4?1000000:0),videoDuration=1000000;
  if(n+1==count){retainedFinal=gst_memory_ref(mem);retainedIdentity=relaytest::poolAllocation(mem);}
  GST_BUFFER_PTS(b)=37000000+audioPts;GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b)-100;GST_BUFFER_DURATION(b)=videoDuration;
  const gsize pcmBytes=192;
  auto*a=gst_buffer_new_allocate(nullptr,pcmBytes,nullptr);gst_buffer_memset(a,0,n+31,pcmBytes);GST_BUFFER_PTS(a)=audioPts;GST_BUFFER_DURATION(a)=1000000;
  if(relayEvents&&n==4)GST_BUFFER_FLAG_SET(a,GST_BUFFER_FLAG_DISCONT);
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(vi),b)==GST_FLOW_OK);CHECK(gst_app_src_push_buffer(GST_APP_SRC(ai),a)==GST_FLOW_OK);
  auto*vs=gst_app_sink_try_pull_sample(GST_APP_SINK(vo),2*GST_SECOND);auto*as=gst_app_sink_try_pull_sample(GST_APP_SINK(ao),2*GST_SECOND);
  if(!vs||!as){
   fprintf(stderr,"relay frame=%u video=%p audio=%p\n",n,(void*)vs,(void*)as);
   for(auto*pipeline:{tx,rx}){auto*bus=gst_element_get_bus(pipeline);
    while(auto*msg=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR)){GError*error=nullptr;gchar*debug=nullptr;gst_message_parse_error(msg,&error,&debug);
     fprintf(stderr,"pipeline error: %s %s\n",error->message,debug?debug:"");g_clear_error(&error);g_free(debug);gst_message_unref(msg);}
    gst_object_unref(bus);}
  }
  CHECK(vs&&as);
  auto*vb=gst_sample_get_buffer(vs);auto*ab=gst_sample_get_buffer(as);
  CHECK(GST_BUFFER_PTS(vb)==guint64(mappedBase)+37000000+audioPts);
  CHECK(GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb)-100);
  CHECK(GST_BUFFER_PTS(ab)==guint64(mappedBase)+audioPts&&GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000);
  CHECK(GST_BUFFER_DURATION(vb)==videoDuration&&GST_BUFFER_DURATION(ab)==1000000);
  CHECK(gst_buffer_n_memory(vb)==1);
  {
   auto*memory=gst_buffer_peek_memory(vb,0);CHECK(relaytest::isNativeMemory(memory)&&relaytest::isReaderMemory(memory));
   CHECK(gst_caps_features_contains(gst_caps_get_features(gst_sample_get_caps(vs),0),relaytest::nativeFeature()));
   auto*meta=gst_buffer_get_video_meta(vb);CHECK(meta&&meta->n_planes==1);
   // Explicit test-oracle readback, not a transport fallback. Respect device pitch.
   GstMapInfo map{};CHECK(gst_memory_map(memory,&map,GST_MAP_READ));
   for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(map.data[meta->offset[0]+y*meta->stride[0]+x]==n+1);
   gst_memory_unmap(memory,&map);
   if(n+1==count)retainedReceiver=gst_memory_ref(memory);
  }
  if(binRelay){
   if(n+1==count)gst_mini_object_weak_ref(GST_MINI_OBJECT(gst_buffer_peek_memory(vb,0)),
    [](gpointer data,GstMiniObject*){static_cast<std::atomic<bool>*>(data)->store(true);},&finalReaderRetired);
   binAdapter->forward(vs,as,n);
  }
  if(leaseRelay){
   auto*bad=gst_buffer_new_allocate(nullptr,64*32*4,nullptr);GST_BUFFER_PTS(bad)=GST_BUFFER_PTS(vb);
   CHECK(leaseAdapter->forward(bad)==FL_INVALID);gst_buffer_unref(bad);
   auto*untimed=gst_buffer_copy(vb);GST_BUFFER_PTS(untimed)=GST_CLOCK_TIME_NONE;
   CHECK(leaseAdapter->forward(untimed)==FL_INVALID);gst_buffer_unref(untimed);
   auto*stale=gst_buffer_copy(vb);auto*staleMeta=gst_buffer_get_framelink_frame_meta(stale);CHECK(staleMeta);++staleMeta->frame.streamGeneration;
   CHECK(leaseAdapter->forward(stale)==FL_INVALID);gst_buffer_unref(stale);
   if(!n){
    CHECK(!relaytest::isReaderMemory(nullptr));
    flChannel* unrelatedReader=nullptr,*unrelatedWriter=nullptr;
    CHECK(flCreateChannelEx((name+"-unrelated").c_str(),64,32,FL_FORMAT_RGBA8,2,
      FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,&unrelatedReader)==FL_OK);
    CHECK(flOpenProducer((name+"-unrelated").c_str(),&unrelatedWriter)==FL_OK);
    relaytest::GstRelay unrelated(unrelatedWriter,cfg.generation);CHECK(unrelated.initialize()==FL_OK);
    CHECK(unrelated.forward(vb)==FL_FORMAT_UNSUPPORTED&&unrelated.pending()==0&&unrelated.count()==0);
    flFrame absent{};CHECK(flAcquireFrame(unrelatedReader,&absent,0)==FL_TIMEOUT);
    CHECK(unrelated.finish()==FL_OK);flClose(unrelatedWriter);flClose(unrelatedReader);
   }
   CHECK(leaseAdapter->forward(vb)==FL_OK&&leaseAdapter->pending()==1);
   flFrame output{};CHECK(flAcquireFrame(leaseReader,&output,1000)==FL_OK);
   CHECK(output.ptsNs==gint64(GST_BUFFER_PTS(vb)));
   flFrameMetadata metadata{};CHECK(flGetFrameMetadata(leaseReader,&output,&metadata)==FL_OK);
   CHECK(metadata.streamGeneration==cfg.generation&&metadata.dtsNs==gint64(GST_BUFFER_DTS(vb))&&metadata.durationNs==GST_BUFFER_DURATION(vb));
   uint64_t lease=0;CHECK(flLeaseFrame(leaseReader,&output,&lease)==FL_OK);
   CHECK(leaseAdapter->finish()==FL_BUSY&&leaseAdapter->pending()==1);
   // Drop the actual upstream sample before using downstream pixels. Only the
   // adapter's memory reference (and the explicit final-frame oracle) holds it.
   if(n+1==count)gst_mini_object_weak_ref(GST_MINI_OBJECT(gst_buffer_peek_memory(vb,0)),
    [](gpointer data,GstMiniObject*){static_cast<std::atomic<bool>*>(data)->store(true);},&finalReaderRetired);
   gst_sample_unref(vs);vs=nullptr;
   GstBuffer* available=nullptr,*blocked=nullptr;GstBufferPoolAcquireParams noWait{};noWait.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
   CHECK(gst_buffer_pool_acquire_buffer(pool,&available,&noWait)==GST_FLOW_OK);
   CHECK(gst_buffer_pool_acquire_buffer(pool,&blocked,&noWait)==GST_FLOW_EOS&&!blocked);
   CHECK(!relaytest::isReaderMemory(gst_buffer_peek_memory(available,0)));
   gst_buffer_unref(available);
   flMappedImage mapped{};CHECK(flImageMapPlanes(output.image,&mapped)==FL_OK);
   for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(((unsigned char*)mapped.plane[0].pixels)[y*mapped.plane[0].stride+x]==n+1);
   flImageUnmap(output.image);
   if(n+1==count){heldRelayFrame=output;heldRelayLease=lease;}
   else{
    CHECK(flCompleteReadLease(leaseReader,lease)==FL_OK);CHECK(flCollectReadLeases(leaseReader)==FL_OK);
    auto deadline=g_get_monotonic_time()+1000000;
    while(leaseAdapter->pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(leaseAdapter->collect()==FL_OK);g_usleep(100);}
   }
   // Appsink retains its current sample internally until the next pull or
   // shutdown. Do not demand both upstream slots be free at this boundary.
   // The final lease is instead checked after every upstream object is gone.
  }
  std::vector<guint8>pcm(pcmBytes);CHECK(gst_buffer_get_size(ab)==pcmBytes&&gst_buffer_extract(ab,0,pcm.data(),pcmBytes)==pcmBytes);for(auto v:pcm)CHECK(v==n+31);
  if(vs)gst_sample_unref(vs);gst_sample_unref(as);
 }
 if(relayEvents){auto*vpad=gst_element_get_static_pad(vi,"src");
  CHECK(gst_pad_push_event(vpad,gst_event_new_gap(46000000,500000)));gst_object_unref(vpad);}
 if(leaseRelay)CHECK(leaseAdapter->count()==count&&leaseAdapter->pending()==1&&leaseAdapter->finish()==FL_BUSY);
 if(mode==6||mode==8||mode==9||mode==13||mode==14){
  CHECK(binRelay);binAdapter->cancelDownstream(mode!=6,(mode==9||mode==14)?tx:nullptr);
  // Observe cancellation at the actual original producer, while its last
  // native allocation remains held by the final downstream reader.
  flSessionConfig snapshot{};auto began=g_get_monotonic_time();
  FlGstSessionResult result=FL_GST_SESSION_READY;
  do{result=fl_gst_session_wait(s,cfg.generation,50,&snapshot);
   if(result!=FL_GST_SESSION_READY)break;g_usleep(1000);
  }while(g_get_monotonic_time()-began<1000000);
  CHECK(result==FL_GST_SESSION_CANCELLED&&g_get_monotonic_time()-began<1500000);
  CHECK(!finalReaderRetired.load());
  for(auto*p:{tx,rx}){auto*bus=gst_element_get_bus(p);
   CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);}
  printf("broker bin original producer observes cancellation: %.3fms, no EOS\n",(g_get_monotonic_time()-began)/1000.0);
 }else{
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?vi:ai))==GST_FLOW_OK);
  auto*bus=gst_element_get_bus(rx);CHECK(!gst_bus_timed_pop_filtered(bus,30*GST_MSECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR)));gst_object_unref(bus);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?ai:vi))==GST_FLOW_OK);avTerminal(tx,GST_MESSAGE_EOS);avTerminal(rx,GST_MESSAGE_EOS);
  const gint64 finalPts=37000000+gint64(count-1)*1000000+(relayEvents?1000000:0);
  guint64 frames=999;gint64 last=999;CHECK(fl_gst_session_wait_video_eos(r,cfg.generation,0,&frames,&last)==FL_GST_SESSION_READY&&frames==count&&last==finalPts);
 }
 if(binRelay&&!binAdapter->failureObserved)binAdapter->terminal();
 auto start=g_get_monotonic_time();gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);CHECK(g_get_monotonic_time()-start<1000000);
 CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
 for(auto*object:{vi,ai,vo,ao,sink,src,tx,rx})gst_object_unref(object);
 if(audioPeer)g_object_unref(audioPeer);g_object_unref(s);g_object_unref(r);
 relaytest::checkRetained(retainedReceiver);gst_memory_unref(retainedReceiver);
 relaytest::checkRetained(retainedFinal);CHECK(relaytest::poolAllocation(retainedFinal)==retainedIdentity);gst_memory_unref(retainedFinal);
 if(binRelay){
  CHECK(!finalReaderRetired.load());binAdapter->releaseAfterUpstreamTeardown();
  CHECK(finalReaderRetired.load());binAdapter.reset();leaseAdapter.reset();flClose(leaseWriter);
  // leaseReader was adopted and is owned by the downstream avsrc/reader memory.
 }else{
  CHECK(!finalReaderRetired.load()&&leaseAdapter->finish()==FL_BUSY);
  flMappedImage finalPixels{};CHECK(flImageMapPlanes(heldRelayFrame.image,&finalPixels)==FL_OK);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(((unsigned char*)finalPixels.plane[0].pixels)[y*finalPixels.plane[0].stride+x]==count);
  flImageUnmap(heldRelayFrame.image);
  CHECK(flCompleteReadLease(leaseReader,heldRelayLease)==FL_OK);CHECK(flCollectReadLeases(leaseReader)==FL_OK);
  auto deadline=g_get_monotonic_time()+1000000;
  while(leaseAdapter->pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(leaseAdapter->collect()==FL_OK);g_usleep(100);}
  CHECK(finalReaderRetired.load()&&leaseAdapter->finish()==FL_OK);
  leaseAdapter.reset();flClose(leaseWriter);flClose(leaseReader);
  puts("native GstMemory lease adapter: 8 frames, reversed same-allocation slots, exact mapped timestamps/pixels, invalid/foreign inputs rejected; final upstream reader retained after complete bin teardown and retired only after downstream completion");
 }
 puts("native relay: retained producer and receiver allocations after full teardown, original allocations, exact pixels/PCM, 37ms mapping and terminal semantics pass");
}
}

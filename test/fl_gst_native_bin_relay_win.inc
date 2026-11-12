// Broker-owned positive composition fixture, not an automatic discovery API.
// The first avsrc has already authenticated/consumed upstream video events.
// This bounded fixture republishes barriers and optionally forwards actual
// serialized GAP observations; arbitrary events/discovery are not supported.
#include "../samples/session_relay_contract.h"
#include <mutex>
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
 flsample::NativeGstD3D11Relay& native;
 flSessionConfig config{};
 GstMemory* held=nullptr;
 guint64 count=0,pcmFrames=0;gint64 last=-1;
 bool events=false;unsigned forwardedGaps=0;BinRelayEvents upstreamEvents,downstreamEvents;
 void observeEvents(GstElement*upstream){events=true;upstreamEvents.attach(upstream);downstreamEvents.attach(video);}
 void forwardGaps(){
  if(!events)return;
  std::lock_guard<std::mutex> lock(upstreamEvents.mutex);
  while(forwardedGaps<upstreamEvents.size){const auto&gap=upstreamEvents.gaps[forwardedGaps];
   CHECK(gap.afterFrames==count);
   CHECK(fl_gst_session_send_video_event(sender,config.generation,&gap)==FL_GST_SESSION_READY);++forwardedGaps;
  }
 }
 void verifyEvents(){
  if(!events)return;
  std::lock_guard<std::mutex> a(upstreamEvents.mutex),b(downstreamEvents.mutex);
  CHECK(upstreamEvents.frames==8&&downstreamEvents.frames==8&&forwardedGaps==3);
  CHECK(upstreamEvents.size==3&&downstreamEvents.size==3);
  const guint64 positions[]={4,4,8},times[]={41000000,41250000,46000000},durations[]={250000,750000,500000};
  for(unsigned i=0;i<3;++i){const auto&u=upstreamEvents.gaps[i];const auto&d=downstreamEvents.gaps[i];
   CHECK(u.afterFrames==positions[i]&&u.ptsNs==times[i]&&u.durationNs==durations[i]);
   CHECK(d.afterFrames==u.afterFrames&&d.ptsNs==u.ptsNs+5000000&&d.durationNs==u.durationNs);
  }
  CHECK((upstreamEvents.discont&17)==17&&downstreamEvents.discont==upstreamEvents.discont);
  puts("broker bin serialized GAP: exact 4,4,8 frame boundaries, +5ms mapping, durations 250/750/500us, DISCONT preserved, trailing GAP before EOS");
 }
 BrokerNativeBinRelay(FlGstSession* source,const flSessionConfig& original,
   const GstFrameLinkMemoryPolicy& policy,flChannel* channel,flsample::NativeGstD3D11Relay& adapter)
   :up(source),native(adapter),config(original){
  config.sessionId.bytes[0]^=0x40;
  flSessionCapabilities caps{};caps.version=config.version;caps.videoCount=caps.audioCount=1;
  caps.video[0]=config.video;caps.audio[0]=config.audio;
  for(auto*s:{sender,receiver})CHECK(fl_gst_session_configure(s,&config,&caps,&caps)==FL_GST_SESSION_READY);
  flsample::SessionRelayContract contract(up,sender);CHECK(contract.initialize()==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_register_track(sender,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
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
  CHECK(GST_BUFFER_PTS(vb)==GST_BUFFER_PTS(v)+5000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_DTS(v)+5000000);
  CHECK(GST_BUFFER_DURATION(vb)==GST_BUFFER_DURATION(v));
  CHECK(GST_BUFFER_PTS(ab)==GST_BUFFER_PTS(a)+5000000&&GST_BUFFER_DURATION(ab)==GST_BUFFER_DURATION(a));
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000);
  if(events&&n==4)CHECK(GST_BUFFER_FLAG_IS_SET(a,GST_BUFFER_FLAG_DISCONT)&&GST_BUFFER_FLAG_IS_SET(ab,GST_BUFFER_FLAG_DISCONT));
  auto*metadata=gst_buffer_get_framelink_frame_meta(vb);CHECK(metadata&&metadata->frame.streamGeneration==config.generation);
  auto*memory=gst_buffer_peek_memory(vb,0);CHECK(gst_is_d3d11_memory(memory));
  using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
  auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));CHECK(compare);
  CHECK(compare(gst_framelink_memory_get_d3d11_shared_handle(memory),gst_framelink_memory_get_d3d11_shared_handle(gst_buffer_peek_memory(v,0))));
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
 void cancelDownstream(){
  CHECK(count==8&&pcmFrames==384&&held&&native.pending()==1);
  flSessionConfig snapshot{};
  CHECK(fl_gst_session_wait(sender,config.generation,1000,&snapshot)==FL_GST_SESSION_READY);
  const auto began=g_get_monotonic_time();
  CHECK(gst_element_set_state(rx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  FlGstSessionResult result=FL_GST_SESSION_READY;
  do{result=fl_gst_session_wait(sender,config.generation,50,&snapshot);
   if(result!=FL_GST_SESSION_READY)break;g_usleep(1000);
  }while(g_get_monotonic_time()-began<1000000);
  CHECK(result==FL_GST_SESSION_CANCELLED);
  // Failure is not permission to retire a native reader or synthesize END.
  CHECK(native.pending()==1&&native.finish()==FL_BUSY);
  guint64 frames=999;gint64 pts=999;
  CHECK(fl_gst_session_wait_video_eos(sender,config.generation,0,&frames,&pts)==FL_GST_SESSION_CANCELLED);
  CHECK(frames==999&&pts==999);
  for(auto*p:{tx,rx}){auto*bus=gst_element_get_bus(p);
   CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);}
  fl_gst_session_cancel(up); // broker explicitly stops the upstream graph
  CHECK(fl_gst_session_wait(up,config.generation,1,&snapshot)==FL_GST_SESSION_CANCELLED);
  const auto elapsed=g_get_monotonic_time()-began;CHECK(elapsed<1500000);
  printf("broker bin downstream cancellation: %.3fms, explicit upstream cancellation, no END/EOS, final reader still held\n",elapsed/1000.0);
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
 }
 void releaseAfterUpstreamTeardown(){
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
  CHECK(native.finish()==FL_OK);g_object_unref(sender);g_object_unref(receiver);
  puts("broker native bin relay: 8 frames over two original allocations / 384 PCM frames; identity intermediate mapping, final +5ms and exact 37ms A/V, authenticated session; final reader retained beyond upstream and downstream bin teardown");
 }
};

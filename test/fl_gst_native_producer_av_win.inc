// Broker-bound native producer + authenticated PCM/event integration. The
// receiver intentionally selects CPU readback: this is not end-to-end zero-copy.
#include "../gst/framelink_session_transport.h"
#include <gst/app/gstappsink.h>
static void avStreamPair(GSocketConnection**a,GSocketConnection**b){
 auto*l=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(l,nullptr,nullptr);CHECK(port);
 std::thread peer([&]{*a=g_socket_listener_accept(l,nullptr,nullptr,nullptr);});
 auto*c=g_socket_client_new();g_socket_client_set_timeout(c,2);*b=g_socket_client_connect_to_host(c,"127.0.0.1",port,nullptr,nullptr);
 peer.join();CHECK(*a&&*b);g_object_unref(c);g_object_unref(l);
}
static void avTerminal(GstElement*p,GstMessageType expected){
 auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
 if(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);fprintf(stderr,"native AV: %s %s\n",e->message,d?d:"");g_clear_error(&e);g_free(d);}
 CHECK(m&&GST_MESSAGE_TYPE(m)==expected);gst_message_unref(m);gst_object_unref(bus);
}
static GstFrameLinkMemoryPolicy nativeAvPolicy(guint64 generation){
 GstFrameLinkMemoryPolicy policy{};policy.version=1;policy.sessionGeneration=generation;
 for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&policy.requirements[i]);
  policy.requirements[i].allowedResourceTypes=FL_MEMORY_D3D11;
  policy.requirements[i].allowedOperations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
  policy.paths[i].version=1;policy.paths[i].resourceType=FL_MEMORY_D3D11;
 }
 policy.paths[0].supportedUsage=policy.requirements[0].requiredUsage=FL_MEMORY_GPU_SAMPLE;
 policy.paths[1].supportedUsage=policy.requirements[1].requiredUsage=FL_MEMORY_CPU_READ;
 policy.paths[1].operations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
 return policy;
}
struct NativeEpochProbe {gint64 epoch;unsigned calls=0;};
static gint64 nativeEpoch(gpointer data,GstClockTime now){
 auto*p=static_cast<NativeEpochProbe*>(data);CHECK(now>0);++p->calls;return p->epoch;
}
static void nativeProducerAv(unsigned mode,bool local=false,bool customEpoch=false,bool automatic=false){
 fprintf(stderr,"native AV mode %u local=%d\n",mode,local);
 auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;
 cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
 cfg.video={FL_FORMAT_RGBA8,64,32,30,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 flSessionCapabilities offer{};offer.version=2;offer.videoCount=1;offer.video[0]=cfg.video;offer.audioCount=1;offer.audio[0]=cfg.audio;
 flChannel*channel=nullptr;flChannel*producer=nullptr;
 auto policy=nativeAvPolicy(local?0:cfg.generation);
 GSocketConnection*audioPeer=nullptr;
 if(local&&!automatic){
  gchar*dir=g_dir_make_tmp("framelink-native-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
  GstFrameLinkMemoryPolicy receiverPolicy{};gboolean opened=FALSE;GError*se=nullptr,*ce=nullptr;
  auto candidate=policy;
  auto*clock=gst_system_clock_obtain();NativeEpochProbe epoch{gint64(gst_clock_get_time(clock)-GST_SECOND)},ignored{-1};gst_object_unref(clock);
  std::thread peer([&]{opened=fl_gst_session_open_local_native_epoch(r,FALSE,path.c_str(),&offer,1,2,80,2000,1000,&candidate,&receiverPolicy,&channel,nativeEpoch,&ignored,nullptr,&ce);});
  gboolean sender=customEpoch?
   fl_gst_session_open_local_native_epoch(s,TRUE,path.c_str(),&offer,1,2,80,2000,1000,&candidate,&policy,&producer,nativeEpoch,&epoch,nullptr,&se):
   fl_gst_session_open_local_native(s,TRUE,path.c_str(),&offer,1,2,80,2000,1000,&candidate,&policy,&producer,nullptr,&se);
  peer.join();if(se||ce)fprintf(stderr,"local native: %s / %s\n",se?se->message:"ok",ce?ce->message:"ok");
  CHECK(sender&&opened&&!se&&!ce&&channel&&producer);
  CHECK(policy.sessionGeneration&&policy.sessionGeneration==receiverPolicy.sessionGeneration);
  CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
  flSessionConfig remote{};CHECK(fl_gst_session_get_config(r,&remote)==FL_GST_SESSION_READY);
  CHECK(!ignored.calls&&remote.epochNs==cfg.epochNs);
  CHECK(epoch.calls==(customEpoch?1u:0u));if(customEpoch)CHECK(cfg.epochNs==epoch.epoch);
 }else if(!automatic){
 CHECK(fl_gst_session_configure(s,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
 CHECK(fl_gst_session_configure(r,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
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
 flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=1000000;mapping.sourceAnchorNs=cfg.epochNs;
 if(!local&&!automatic)CHECK(fl_gst_session_set_mapping(r,1,&mapping,0)==FL_GST_SESSION_READY);
 gint64 mappedBase=0;if(!automatic)CHECK(fl_gst_session_map_running_time(r,1,0,TRUE,&mappedBase)==FL_GST_SESSION_READY);
 auto name="native-av-"+std::to_string(g_get_monotonic_time());
 if(!local&&!automatic)CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&channel)==FL_OK);
 GError*e=nullptr;
 auto*tx=gst_parse_launch("framelinkavsink name=transport native-memory=true appsrc name=vi is-live=true format=time caps=video/x-raw(memory:D3D11Memory),format=RGBA,width=64,height=32,framerate=30/1 ! transport.video_sink appsrc name=ai is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! transport.audio_sink",&e);CHECK(tx&&!e);
 auto*rx=gst_parse_launch("framelinkavsrc name=transport video-format=RGBA width=64 height=32 framerate=30/1 transport.video_src ! appsink name=vo sync=false async=false transport.audio_src ! appsink name=ao sync=false async=false",&e);CHECK(rx&&!e);
 auto*sink=gst_bin_get_by_name(GST_BIN(tx),"transport");auto*src=gst_bin_get_by_name(GST_BIN(rx),"transport");
 // Both endpoints authenticate the exact producer-ordered policy before use.
 if(!local&&!automatic){
 guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);
 GSocketConnection*ma=nullptr,*mb=nullptr;avStreamPair(&ma,&mb);gboolean agreed=FALSE;
 std::thread agreement([&]{agreed=fl_gst_session_authenticate_native_memory(s,ma,TRUE,key,&policy,1000,nullptr,nullptr);});
 CHECK(fl_gst_session_authenticate_native_memory(r,mb,FALSE,key,&policy,1000,nullptr,nullptr));agreement.join();CHECK(agreed);
 g_io_stream_close(G_IO_STREAM(ma),nullptr,nullptr);g_io_stream_close(G_IO_STREAM(mb),nullptr,nullptr);
 g_object_unref(ma);g_object_unref(mb);g_bytes_unref(key);
 CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 }
 if(!automatic){
 gboolean accepted=FALSE;
 g_signal_emit_by_name(sink,"adopt-channel",producer,&accepted);CHECK(accepted);
 g_signal_emit_by_name(sink,"apply-memory-policy",&policy,&accepted);CHECK(accepted);
 gboolean adopted=FALSE;g_signal_emit_by_name(src,"adopt-channel",channel,&adopted);CHECK(adopted);
 auto consumerPolicy=policy;std::swap(consumerPolicy.requirements[0],consumerPolicy.requirements[1]);std::swap(consumerPolicy.paths[0],consumerPolicy.paths[1]);
 g_signal_emit_by_name(src,"apply-memory-policy",&consumerPolicy,&accepted);CHECK(accepted);
 auto*video=gst_bin_get_by_name(GST_BIN(sink),"video");g_object_set(video,"async",FALSE,"enable-last-sample",FALSE,nullptr);gst_object_unref(video);
 for(auto entry:{std::make_pair(tx,s),std::make_pair(rx,r)}){auto*c=fl_gst_session_create_context(entry.second);gst_element_set_context(entry.first,c);gst_context_unref(c);}
 }else{
  gchar*dir=g_dir_make_tmp("framelink-native-bins-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
  for(auto*bin:{sink,src})g_object_set(bin,"session",path.c_str(),"video-format","RGBA","width",64u,"height",32u,
    "video-pool",2u,"video-queue-frames",1u,"connect-timeout-ms",2000u,"idle-timeout-ms",1000u,nullptr);
  policy=nativeAvPolicy(0);gboolean accepted=FALSE;
  for(auto*bin:{sink,src}){auto candidate=policy;g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(accepted);
   candidate.version=99; // the bin must retain a copy, not this caller-owned pointer
  }
 }
 auto*vi=gst_bin_get_by_name(GST_BIN(tx),"vi");auto*ai=gst_bin_get_by_name(GST_BIN(tx),"ai");auto*vo=gst_bin_get_by_name(GST_BIN(rx),"vo");auto*ao=gst_bin_get_by_name(GST_BIN(rx),"ao");
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 if(automatic){
  for(auto entry:{std::make_pair(sink,&s),std::make_pair(src,&r)}){
   auto*child=gst_bin_get_by_name(GST_BIN(entry.first),"video");CHECK(child);
   GstState state=GST_STATE_NULL;const auto end=g_get_monotonic_time()+5000000;
   do {gst_element_get_state(child,&state,nullptr,0);if(state==GST_STATE_PLAYING)break;g_usleep(1000);}while(g_get_monotonic_time()<end);
   CHECK(state==GST_STATE_PLAYING);auto*context=gst_element_get_context(child,"framelink.session.v1");CHECK(context);
   g_object_unref(*entry.second);*entry.second=fl_gst_session_from_context(context);CHECK(*entry.second);gst_context_unref(context);gst_object_unref(child);
  }
  CHECK(fl_gst_session_get_config(s,&cfg)==FL_GST_SESSION_READY);
  CHECK(cfg.epochNs==gint64(gst_element_get_base_time(tx)));
  CHECK(fl_gst_session_map_running_time(r,cfg.generation,0,TRUE,&mappedBase)==FL_GST_SESSION_READY);
 }
 auto*caps=gst_caps_from_string("video/x-raw(memory:D3D11Memory),format=RGBA,width=64,height=32,framerate=30/1");
 auto*q=gst_query_new_allocation(caps,TRUE);
 guint8 reqWire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&policy.requirements[0],reqWire,sizeof reqWire)==FL_MEMORY_OK);
 auto*reqBytes=g_bytes_new(reqWire,sizeof reqWire);gst_structure_set(gst_query_writable_structure(q),"framelink-memory-requirements",G_TYPE_BYTES,reqBytes,nullptr);g_bytes_unref(reqBytes);
 gst_structure_set(gst_query_writable_structure(q),"framelink-completion-contract",G_TYPE_STRING,"same-device-before-abandon-v1",nullptr);
 auto*pad=gst_element_get_static_pad(sink,"video_sink");CHECK(gst_pad_query(pad,q));
 GstBufferPool*pool=nullptr;guint size=0,min=0,max=0;gst_query_parse_nth_allocation_pool(q,0,&pool,&size,&min,&max);CHECK(pool&&max==2);
 gst_query_unref(q);gst_caps_unref(caps);gst_object_unref(pad);CHECK(gst_buffer_pool_set_active(pool,TRUE));
 const unsigned count=mode==2?0:8;
 for(unsigned n=0;n<count;++n){
  GstBuffer*b=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK);
  auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_d3d11_memory(mem));
  auto*reservation=(FrameLinkD3DReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(mem),FrameLinkD3DProducer::key());CHECK(reservation);
  auto*texture=(ID3D11Texture2D*)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem));CHECK(texture==flImageD3D11(reservation->buffer.image));
  ID3D11Device*d=nullptr;texture->GetDevice(&d);ID3D11DeviceContext*c=nullptr;d->GetImmediateContext(&c);
  std::vector<unsigned char>pixels(64*32*4,static_cast<unsigned char>(n+1));c->UpdateSubresource(texture,0,nullptr,pixels.data(),256,0);c->Release();d->Release();
  GST_BUFFER_PTS(b)=37000000+n*1000000;GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b)-100;GST_BUFFER_DURATION(b)=1000000;
  auto*a=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(a,0,n+31,192);GST_BUFFER_PTS(a)=n*1000000;GST_BUFFER_DURATION(a)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(vi),b)==GST_FLOW_OK);CHECK(gst_app_src_push_buffer(GST_APP_SRC(ai),a)==GST_FLOW_OK);
  auto*vs=gst_app_sink_try_pull_sample(GST_APP_SINK(vo),2*GST_SECOND);auto*as=gst_app_sink_try_pull_sample(GST_APP_SINK(ao),2*GST_SECOND);CHECK(vs&&as);
  auto*vb=gst_sample_get_buffer(vs);auto*ab=gst_sample_get_buffer(as);
  CHECK(GST_BUFFER_PTS(vb)==guint64(mappedBase)+37000000+n*1000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb)-100);
  CHECK(GST_BUFFER_PTS(ab)==guint64(mappedBase)+n*1000000&&GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000);
  CHECK(GST_BUFFER_DURATION(vb)==1000000&&GST_BUFFER_DURATION(ab)==1000000);
  GstMapInfo map{};CHECK(gst_buffer_map(vb,&map,GST_MAP_READ));CHECK(map.size==pixels.size());for(gsize i=0;i<map.size;++i)CHECK(map.data[i]==n+1);gst_buffer_unmap(vb,&map);
  guint8 pcm[192];CHECK(gst_buffer_extract(ab,0,pcm,192)==192);for(auto v:pcm)CHECK(v==n+31);
  gst_sample_unref(vs);gst_sample_unref(as);
 }
 if(mode==3){CHECK(audioPeer);CHECK(g_socket_shutdown(g_socket_connection_get_socket(audioPeer),TRUE,TRUE,nullptr));avTerminal(rx,GST_MESSAGE_ERROR);}
 else{
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?vi:ai))==GST_FLOW_OK);
  auto*bus=gst_element_get_bus(rx);CHECK(!gst_bus_timed_pop_filtered(bus,30*GST_MSECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR)));gst_object_unref(bus);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?ai:vi))==GST_FLOW_OK);avTerminal(tx,GST_MESSAGE_EOS);avTerminal(rx,GST_MESSAGE_EOS);
  guint64 frames=999;gint64 last=999;CHECK(fl_gst_session_wait_video_eos(r,cfg.generation,0,&frames,&last)==FL_GST_SESSION_READY&&frames==count&&last==(count?37000000+(count-1)*1000000:-1LL));
 }
 auto start=g_get_monotonic_time();gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);CHECK(g_get_monotonic_time()-start<1000000);
 if(automatic){
  CHECK(gst_element_set_state(sink,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);
  CHECK(gst_element_set_state(src,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);
 }
 CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
 for(auto*object:{vi,ai,vo,ao,sink,src,tx,rx})gst_object_unref(object);
 if(audioPeer)g_object_unref(audioPeer);g_object_unref(s);g_object_unref(r);
 puts("native producer AV: original allocation, exact pixels/PCM, 37ms mapping and terminal semantics pass");
}
static void nativeLocalNegatives(){
 for(unsigned mode=0;mode<11;++mode){
  auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
  flSessionCapabilities caps{};caps.version=2;caps.videoCount=1;caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};
  caps.audioCount=1;caps.audio[0]={FL_PCM_S16,48000,2};
  auto candidate=nativeAvPolicy(0),other=candidate;GstFrameLinkMemoryPolicy a{},b{};
  gchar*dir=g_dir_make_tmp("framelink-native-negative-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
  flChannel*pch=nullptr,*rch=nullptr;gboolean peer=TRUE;auto start=g_get_monotonic_time();
  if(mode<5){
   if(mode==0)candidate.sessionGeneration=1;
   if(mode==1)candidate.paths[1].operations=0;
   if(mode==2)candidate.paths[0].resourceType=FL_MEMORY_SHARED_CPU;
   if(mode==3)candidate.requirements[0].allowedOperations=0;
   if(mode==4)candidate.paths[0].supportedUsage=FL_MEMORY_GPU_RENDER;
   a.version=99;
   CHECK(!fl_gst_session_open_local_native(s,TRUE,path.c_str(),&caps,1,2,80,200,1000,&candidate,&a,&pch,nullptr,nullptr));
   CHECK(!pch&&!a.version&&!a.sessionGeneration);
  }else if(mode<8){
   if(mode==5)other.requirements[0].preferredUsage=FL_MEMORY_GPU_SAMPLE; // valid but distinct candidate
   std::thread receiver([&]{
    if(mode==6)peer=fl_gst_session_open_local_av(r,FALSE,path.c_str(),&caps,1,2,80,200,1000,0,&rch,nullptr,nullptr);
    else peer=fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,mode==7?2:1,2,80,200,1000,&other,&b,&rch,nullptr,nullptr);
   });
   CHECK(!fl_gst_session_open_local_native(s,TRUE,path.c_str(),&caps,mode==7?2:1,2,80,200,1000,&candidate,&a,&pch,nullptr,nullptr));
   receiver.join();CHECK(!peer&&!pch&&!rch&&!a.version&&!b.version);
  }else if(mode==10){
   NativeEpochProbe invalid{-1};GError*error=nullptr;
   a.version=b.version=99;
   std::thread receiver([&]{peer=fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,1,2,80,500,1000,&other,&b,&rch,nullptr,nullptr);});
   CHECK(!fl_gst_session_open_local_native_epoch(s,TRUE,path.c_str(),&caps,1,2,80,500,1000,&candidate,&a,&pch,nativeEpoch,&invalid,nullptr,&error));
   receiver.join();CHECK(error&&strstr(error->message,"invalid session epoch"));g_clear_error(&error);
   CHECK(invalid.calls==1&&!peer&&!pch&&!rch&&!a.version&&!b.version);
  }else{
   auto*cancel=g_cancellable_new();
   std::thread task([&]{peer=fl_gst_session_open_local_native(s,mode==8,path.c_str(),&caps,1,2,80,60000,1000,&candidate,&a,&pch,cancel,nullptr);});
   g_usleep(30000);g_cancellable_cancel(cancel);task.join();g_object_unref(cancel);
   CHECK(!peer&&!pch&&!a.version);
  }
  CHECK(g_get_monotonic_time()-start<1500000);g_object_unref(s);g_object_unref(r);
 }
 puts("native local setup: invalid policies, candidate mismatch, legacy isolation, capacity and cancellation pass");
}

static void nativeBinNegatives(){
 for(bool producer:{false,true}){
  for(unsigned mode=0;mode<10;++mode){
   auto*bin=gst_element_factory_make(producer?"framelinkavsink":"framelinkavsrc",nullptr);CHECK(bin);
   g_object_set(bin,"session","unused-native-negative","native-memory",producer,nullptr);
   auto candidate=nativeAvPolicy(0);
   if(mode==0)candidate.version=2;
   if(mode==1)candidate.sessionGeneration=1;
   if(mode==2)candidate.paths[1].operations=0;
   if(mode==3)candidate.requirements[0].allowedOperations=0;
   if(mode==4)g_object_set(bin,"session",nullptr,nullptr);
   if(mode==5)g_object_set(bin,"shared-cpu",TRUE,nullptr);
   if(mode==6)g_object_set(bin,"tracks","audio",nullptr);
   if(mode==7)g_object_set(bin,"native-memory",!producer,nullptr);
   gboolean accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-candidate",mode==8?nullptr:&candidate,&accepted);
   CHECK(accepted==(mode==9));
   if(mode==9){
    accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);
    accepted=TRUE;g_signal_emit_by_name(bin,"apply-memory-policy",&candidate,&accepted);CHECK(!accepted);
    accepted=TRUE;g_signal_emit_by_name(bin,"adopt-channel",nullptr,&accepted);CHECK(!accepted);
    g_object_set(bin,"width",96u,nullptr);guint width=0;g_object_get(bin,"width",&width,nullptr);CHECK(width==1280);
   }
   gst_object_unref(bin);
  }
  // Setup cancellation must interrupt an absent peer's 60-second deadline.
  auto*pipeline=gst_pipeline_new(nullptr);
  auto*bin=gst_element_factory_make(producer?"framelinkavsink":"framelinkavsrc",nullptr);CHECK(bin);
  gst_bin_add(GST_BIN(pipeline),bin);
  gchar*dir=g_dir_make_tmp("framelink-native-cancel-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
  g_object_set(bin,"session",path.c_str(),"native-memory",producer,"connect-timeout-ms",60000u,nullptr);
  auto candidate=nativeAvPolicy(0);gboolean accepted=FALSE;
  g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(accepted);
  CHECK(gst_element_set_state(pipeline,GST_STATE_READY)!=GST_STATE_CHANGE_FAILURE);
  accepted=TRUE;g_signal_emit_by_name(bin,"set-local-native-candidate",&candidate,&accepted);CHECK(!accepted);
  g_usleep(30000);auto start=g_get_monotonic_time();
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  CHECK(g_get_monotonic_time()-start<1000000);gst_object_unref(pipeline);
 }
 puts("native bins: 20 configuration controls, immutable copies and both-role bounded cancellation pass");
}

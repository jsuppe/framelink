// FAR5 transfer across a real process boundary. Only control events are inherited;
// texture handles are pulled from the authenticated kernel-pinned peer.
#include "../samples/native_relay.h"
static flSessionCapabilities localPoolProcessCaps(){
 flSessionCapabilities caps{};caps.version=2;caps.videoCount=caps.audioCount=1;
 caps.video[0]={FL_FORMAT_RGBA8,64,32,30,1};caps.audio[0]={FL_PCM_S16,48000,2};return caps;
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
static int localPoolProcessReader(const char*path,HANDLE held,HANDLE release,HANDLE exitEvent,bool fixedFixture=true,bool audio=false,bool failedSource=false){
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
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
   CHECK(SetEvent(held));relayprocess::wait(release);
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
 relayprocess::wait(exitEvent);
 if(!failedSource)CHECK(fl_gst_session_finish_local(session,FALSE,1000,nullptr));
 else{fl_gst_session_cancel(session);CHECK(!gst_app_sink_is_eos(GST_APP_SINK(pcm->port)));}
 pcm.reset();flClose(channel);g_object_unref(session);return 0;
}
// Explicit automatic FAR5 receiver: no external session context or adoption.
static int localPoolBinReader(const char*path,HANDLE held,HANDLE release,HANDLE exitEvent){
 GError*error=nullptr;
 auto*pipe=gst_parse_launch("framelinkavsrc name=transport native-memory=true video-format=RGBA width=64 height=32 framerate=30/1 video-pool=2 video-queue-frames=1 transport.video_src ! appsink name=video max-buffers=1 sync=false async=false enable-last-sample=false transport.audio_src ! appsink name=audio max-buffers=1 sync=false async=false enable-last-sample=false",&error);CHECK(pipe&&!error);
 auto*src=gst_bin_get_by_name(GST_BIN(pipe),"transport");auto*video=gst_bin_get_by_name(GST_BIN(pipe),"video");auto*audio=gst_bin_get_by_name(GST_BIN(pipe),"audio");
 auto candidate=nativeAvPolicy(0,true);g_object_set(src,"session",path,nullptr);
 gboolean accepted=FALSE;g_signal_emit_by_name(src,"set-local-native-pool-candidate",&candidate,&accepted);CHECK(accepted);
 g_object_set(video,"emit-signals",TRUE,nullptr);g_signal_connect(video,"propose-allocation",G_CALLBACK(nativeAvReaderAllocation),&candidate);
 CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstClockTime first=GST_CLOCK_TIME_NONE;guint64 frames=0;GstMemory*retained=nullptr;gsize finalOffset=0;gint finalStride=0;
 for(unsigned n=0;n<8;++n){
  auto*v=gst_app_sink_try_pull_sample(GST_APP_SINK(video),5*GST_SECOND);auto*a=gst_app_sink_try_pull_sample(GST_APP_SINK(audio),5*GST_SECOND);CHECK(v&&a);
  auto*vb=gst_sample_get_buffer(v);auto*ab=gst_sample_get_buffer(a);auto*memory=gst_buffer_peek_memory(vb,0);CHECK(gst_is_d3d11_memory(memory));
  auto*meta=gst_buffer_get_video_meta(vb);auto*identity=gst_buffer_get_framelink_frame_meta(vb);guint64 generation=0;g_object_get(src,"generation",&generation,nullptr);
  CHECK(identity&&identity->frame.streamGeneration==generation&&generation);
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb));
  if(!n)first=GST_BUFFER_PTS(ab);
  CHECK(GST_BUFFER_PTS(ab)==first+n*1000000&&GST_BUFFER_DURATION(ab)==1000000&&GST_BUFFER_DURATION(vb)==1000000);
  GstMapInfo map{};CHECK(meta&&gst_memory_map(memory,&map,GST_MAP_READ));
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(map.data[meta->offset[0]+y*meta->stride[0]+x]==n+11);
  gst_memory_unmap(memory,&map);guint8 bytes[192];CHECK(gst_buffer_get_size(ab)==sizeof bytes&&gst_buffer_extract(ab,0,bytes,sizeof bytes)==sizeof bytes);
  for(unsigned i=0;i<sizeof bytes;++i)CHECK(bytes[i]==guint8((n*29+i*7+31)&255));frames+=48;
  if(n==7){retained=gst_memory_ref(memory);finalOffset=meta->offset[0];finalStride=meta->stride[0];}
  gst_sample_unref(v);gst_sample_unref(a);
  if(retained)CHECK(SetEvent(held));
 }
 CHECK(frames==384);avTerminal(pipe,GST_MESSAGE_EOS);CHECK(gst_app_sink_is_eos(GST_APP_SINK(video))&&gst_app_sink_is_eos(GST_APP_SINK(audio)));
 // Keep the control session live until the sender accepts the real drain.
 relayprocess::wait(release);CHECK(ResetEvent(release));
 CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
 CHECK(retained);GstMapInfo stable{};CHECK(gst_memory_map(retained,&stable,GST_MAP_READ));
 for(unsigned y=0;y<32;++y)for(unsigned col=0;col<256;++col)CHECK(stable.data[finalOffset+y*finalStride+col]==18);
 gst_memory_unmap(retained,&stable);CHECK(SetEvent(exitEvent));relayprocess::wait(release);gst_memory_unref(retained);relayprocess::wait(exitEvent);
 gst_object_unref(src);gst_object_unref(video);gst_object_unref(audio);gst_object_unref(pipe);
 puts("automatic FAR5 receiver bin:8 native/384 PCM, exact bytes/37ms, held GstMemory and authenticated EOS");return 0;
}

static void localPoolBinNegatives(){
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
   auto*session=fl_gst_session_new(3);auto caps=localPoolProcessCaps();GstFrameLinkMemoryPolicy bound{};flChannel*channel=nullptr;GError*error=nullptr;
   // Existing FAR4 producer must not silently downgrade this explicit FAR5 bin.
   gboolean ok=fl_gst_session_open_local_native(session,TRUE,path.c_str(),&caps,1,2,80,1000,1000,&candidate,&bound,&channel,nullptr,&error);
   CHECK(!ok&&!channel&&!bound.version&&error);g_clear_error(&error);g_object_unref(session);avTerminal(pipe,GST_MESSAGE_ERROR);
  }else g_usleep(30000);
  const auto began=g_get_monotonic_time();CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  CHECK(g_get_monotonic_time()-began<1000000);gst_object_unref(pipe);
 }
 puts("FAR5 bin controls: explicit immutable profile, invalid candidates, FAR4 mismatch/no fallback, bounded missing-peer cancellation");
}

static void localPoolProcess(const char*plugin){
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=31;cfg.clockId.bytes[0]=73;cfg.generation=19;
 cfg.videoTrackId=7;cfg.audioTrackId=8;cfg.video=caps.video[0];cfg.audio=caps.audio[0];
 cfg.videoQueueFrames=1;cfg.audioQueueMs=80;cfg.connectTimeoutMs=cfg.idleTimeoutMs=5000;
 auto*clock=gst_system_clock_obtain();cfg.epochNs=gst_clock_get_time(clock)-GST_SECOND;gst_object_unref(clock);
 CHECK(fl_gst_session_configure(up,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=81;mapping.sourceAnchorNs=cfg.epochNs;
 CHECK(fl_gst_session_set_mapping(up,cfg.generation,&mapping,0)==FL_GST_SESSION_READY);
 auto name="native-local-process-"+std::to_string(g_get_monotonic_time());
 flChannel*source=nullptr,*writer=nullptr,*downstream=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,&source)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&writer)==FL_OK);
 flChannelInfo info{};CHECK(flQuery(writer,&info)==FL_OK);
 flBuffer reservations[2]{};void*exports[2]{};
 for(unsigned n=0;n<2;++n){CHECK(flAcquireBuffer(writer,&reservations[n],1000)==FL_OK);exports[1-n]=flImageSharedHandle(reservations[n].image);}
 gchar*dir=g_dir_make_tmp("framelink-native-process-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
 flnative::Handle held,release,exitEvent;
 held.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);release.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);exitEvent.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 CHECK(held.value&&release.value&&exitEvent.value);
 auto command=std::string("\"")+plugin+"\" --relay-local-reader \""+path+"\" "+
   std::to_string(uintptr_t(held.value))+" "+std::to_string(uintptr_t(release.value))+" "+std::to_string(uintptr_t(exitEvent.value));
 auto child=relayprocess::spawn(command,{held.value,release.value,exitEvent.value});
 GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native_relay_pool(down,up,path.c_str(),&caps,1,2,80,5000,5000,
   &candidate,info.adapterLUID,exports,&bound,&downstream,nullptr,&error));
 for(auto&b:reservations)CHECK(flCancelBuffer(writer,&b)==FL_OK);
 CHECK(fl_gst_session_local_ready(down,TRUE,1000,nullptr));
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));CHECK(compare);
 flsample::NativeRelay relay(source,downstream,[&](flImage*a,flImage*b){return compare(flImageSharedHandle(a),flImageSharedHandle(b))!=FALSE;});
 CHECK(relay.initialize()==FL_OK);
 ID3D11DeviceContext*context=nullptr;flChannelD3D11Device(writer)->GetImmediateContext(&context);
 for(unsigned n=0;n<8;++n){
  flBuffer buffer{};CHECK(flAcquireBuffer(writer,&buffer,1000)==FL_OK);
  std::vector<unsigned char> pixels(64*32*4,static_cast<unsigned char>(n+11));
  context->UpdateSubresource(flImageD3D11(buffer.image),0,nullptr,pixels.data(),256,0);
  const gint64 pts=37000000+n*1000000;
  flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts,1000000,19,{}};
  CHECK(flSetFrameMetadata(writer,&buffer,&meta)==FL_OK);CHECK(flSubmit(writer,&buffer,pts)==FL_OK);
  flFrame frame{};CHECK(flAcquireFrame(source,&frame,1000)==FL_OK);CHECK(relay.forward(frame)==FL_OK);
  if(n==7){relayprocess::wait(held.value);CHECK(relay.pending()==1&&relay.finish()==FL_BUSY);CHECK(SetEvent(release.value));}
  const auto deadline=g_get_monotonic_time()+2000000;
  while(relay.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
 }
 context->Release();CHECK(relay.count()==8&&relay.finish()==FL_OK);CHECK(SetEvent(exitEvent.value));
 CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));relayprocess::join(child);
 flClose(downstream);flClose(writer);flClose(source);g_object_unref(down);g_object_unref(up);
 puts("FAR5 cross-process: no inherited textures; 8 exact frames/metadata; source lease retained until actual remote reader release; authenticated drain");
}

static void localPoolFailureControls(){
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=31;cfg.clockId.bytes[0]=73;cfg.generation=19;
 cfg.videoTrackId=7;cfg.audioTrackId=8;cfg.video=caps.video[0];cfg.audio=caps.audio[0];
 cfg.videoQueueFrames=1;cfg.audioQueueMs=80;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 auto*clock=gst_system_clock_obtain();cfg.epochNs=gst_clock_get_time(clock)-GST_SECOND;gst_object_unref(clock);
 auto*up=fl_gst_session_new(3);CHECK(fl_gst_session_configure(up,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
 flClockMapping map{};map.sourceClockId=cfg.clockId;map.targetClockId.bytes[0]=9;map.sourceAnchorNs=cfg.epochNs;
 CHECK(fl_gst_session_set_mapping(up,19,&map,0)==FL_GST_SESSION_READY);
 auto name="native-pool-negative-"+std::to_string(g_get_monotonic_time());flChannel*reader=nullptr,*writer=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&reader)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&writer)==FL_OK);flChannelInfo info{};CHECK(flQuery(writer,&info)==FL_OK);
 flBuffer slots[2]{};for(auto&b:slots)CHECK(flAcquireBuffer(writer,&b,1000)==FL_OK);
 flnative::Handle event;event.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(event.value);
 for(unsigned mode=0;mode<6;++mode){
  void*handles[]={flImageSharedHandle(slots[0].image),flImageSharedHandle(slots[1].image)};
  guint8 adapter[8];memcpy(adapter,info.adapterLUID,8);
  if(mode==0)handles[1]=handles[0];
  if(mode==1)handles[1]=reinterpret_cast<void*>(~uintptr_t(0));
  if(mode==2)handles[1]=event.value;
  if(mode==3)memset(adapter,0xff,8);
  auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
  GstFrameLinkMemoryPolicy bound{},received{};flChannel*p=nullptr,*c=nullptr;
  GError*localError=nullptr,*remoteError=nullptr;
  gchar*dir=g_dir_make_tmp("framelink-pool-negative-XXXXXX",nullptr);CHECK(dir);std::string path=std::string(dir)+"/session";g_free(dir);
  gboolean remote=TRUE;auto before=g_get_monotonic_time();
  std::thread peer([&]{
   remote=mode==5?fl_gst_session_open_local_native(r,FALSE,path.c_str(),&caps,1,2,80,500,1000,&candidate,&received,&c,nullptr,&remoteError):
    fl_gst_session_open_local_native_pool_receiver(r,path.c_str(),&caps,1,mode==4?3:2,80,500,1000,&candidate,&received,&c,nullptr,&remoteError);
  });
  auto local=fl_gst_session_open_local_native_relay_pool(s,up,path.c_str(),&caps,1,2,80,500,1000,
   &candidate,adapter,handles,&bound,&p,nullptr,&localError);
  peer.join();fprintf(stderr,"pool negative mode=%u local=%d remote=%d localError=%s remoteError=%s\n",mode,int(local),int(remote),localError?localError->message:"none",remoteError?remoteError->message:"none");CHECK(!local&&!remote&&localError&&remoteError&&!p&&!c&&!bound.version&&!received.version);
  if(mode<5){CHECK(strstr(localError->message,"authenticated relay pool transfer")&&strstr(remoteError->message,"authenticated relay pool transfer"));}
  CHECK(g_get_monotonic_time()-before<2000000);
  flSessionConfig still{};CHECK(fl_gst_session_get_config(up,&still)==FL_GST_SESSION_READY);
  DWORD flags=0;CHECK(GetHandleInformation(event.value,&flags));
  for(auto&b:slots)CHECK(GetHandleInformation(flImageSharedHandle(b.image),&flags));
  g_clear_error(&localError);g_clear_error(&remoteError);g_object_unref(s);g_object_unref(r);
 }
 // Missing downstream must not mask a subsequently cancelled upstream for
 // the full two-second discovery budget. The native source slots stay pinned.
 {
  auto*down=fl_gst_session_new(3);GstFrameLinkMemoryPolicy bound{};flChannel*output=nullptr;GError*error=nullptr;
  void*handles[]={flImageSharedHandle(slots[0].image),flImageSharedHandle(slots[1].image)};
  gchar*dir=g_dir_make_tmp("framelink-pool-source-cancel-XXXXXX",nullptr);CHECK(dir);
  std::string path=std::string(dir)+"/session";g_free(dir);
  const auto began=g_get_monotonic_time();
  std::thread cancel([&]{g_usleep(30000);fl_gst_session_cancel(up);});
  auto ok=fl_gst_session_open_local_native_relay_pool(down,up,path.c_str(),&caps,1,2,80,2000,1000,
    &candidate,info.adapterLUID,handles,&bound,&output,nullptr,&error);
  cancel.join();const auto elapsed=g_get_monotonic_time()-began;
  CHECK(!ok&&error&&!output&&!bound.version&&elapsed<500000);
  fprintf(stderr,"upstream cancellation during missing-downstream setup: %.3fms\n",elapsed/1000.0);
  g_clear_error(&error);g_object_unref(down);
 }
 for(auto&b:slots)CHECK(flCancelBuffer(writer,&b)==FL_OK);
 for(auto&b:slots)CHECK(flAcquireBuffer(writer,&b,1000)==FL_OK);
 for(auto&b:slots)CHECK(flCancelBuffer(writer,&b)==FL_OK);
 flClose(writer);flClose(reader);g_object_unref(up);
 puts("FAR5 pool failures: duplicate/invalid/non-texture handles, wrong LUID/capacity, legacy receiver reject; outputs clear, source/event handles and source pool survive");
}

// Bounded automatic FAR5 lifecycle controls. Only command/notice events cross
// into the reader process; source admission stops before every pause command.
// No claim about overload behavior, physical playback, or lifecycle memory slope.
#include "../samples/local_native_relay_setup.h"
#include "../samples/session_relay_supervisor.h"
static void localPoolPauseMemory(GstMemory*memory,gsize offset,gint stride,unsigned value){
 CHECK(memory&&gst_is_d3d11_memory(memory));GstMapInfo map{};
 CHECK(gst_memory_map(memory,&map,GST_MAP_READ));
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)
  CHECK(map.data[offset+y*stride+x]==value);
 gst_memory_unmap(memory,&map);
}
static void localPoolPauseState(GstElement*pipe,GstState state){
 const auto began=g_get_monotonic_time();
 CHECK(gst_element_set_state(pipe,state)!=GST_STATE_CHANGE_FAILURE);
 GstState actual=GST_STATE_VOID_PENDING,pending=GST_STATE_VOID_PENDING;
 auto result=gst_element_get_state(pipe,&actual,&pending,GST_SECOND);
 CHECK(result!=GST_STATE_CHANGE_FAILURE&&result!=GST_STATE_CHANGE_ASYNC&&actual==state);
 CHECK(g_get_monotonic_time()-began<1500000);
}
static int localPoolPauseReader(const char*path,HANDLE notice,HANDLE command,bool cancelPaused){
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
  auto*v=gst_app_sink_try_pull_sample(GST_APP_SINK(video),5*GST_SECOND);auto*a=gst_app_sink_try_pull_sample(GST_APP_SINK(audio),5*GST_SECOND);CHECK(v&&a);
  auto*vb=gst_sample_get_buffer(v);auto*ab=gst_sample_get_buffer(a);auto*memory=gst_buffer_peek_memory(vb,0);
  auto*meta=gst_buffer_get_video_meta(vb);auto*frame=gst_buffer_get_framelink_frame_meta(vb);identity();
  CHECK(meta&&frame&&frame->frame.streamGeneration==originalGeneration);
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb));
  if(!n)first=GST_BUFFER_PTS(ab);
  CHECK(GST_BUFFER_PTS(ab)==first+n*1000000&&GST_BUFFER_DURATION(ab)==1000000&&GST_BUFFER_DURATION(vb)==1000000);
  localPoolPauseMemory(memory,meta->offset[0],meta->stride[0],n+11);
  guint8 bytes[192];CHECK(gst_buffer_get_size(ab)==sizeof bytes&&gst_buffer_extract(ab,0,bytes,sizeof bytes)==sizeof bytes);
  for(unsigned i=0;i<sizeof bytes;++i)CHECK(bytes[i]==guint8((n*29+i*7+31)&255));pcmFrames+=48;++received;
  if(n==3||n==7){held=gst_memory_ref(memory);heldOffset=meta->offset[0];heldStride=meta->stride[0];}
  gst_sample_unref(v);gst_sample_unref(a);
  if(n==3){
   localPoolPauseState(pipe,GST_STATE_PAUSED);identity();noEos();
   localPoolPauseMemory(held,heldOffset,heldStride,14);CHECK(SetEvent(notice));
   relayprocess::wait(command); // parent verified admission and pending lease
   if(cancelPaused){
    localPoolPauseState(pipe,GST_STATE_NULL);noEos();localPoolPauseMemory(held,heldOffset,heldStride,14);
    CHECK(SetEvent(notice));relayprocess::wait(command); // cancellation observed; still no release
    localPoolPauseMemory(held,heldOffset,heldStride,14);gst_memory_unref(held);held=nullptr;
    CHECK(SetEvent(notice));relayprocess::wait(command);break;
   }
   localPoolPauseState(pipe,GST_STATE_PLAYING);identity();noEos();localPoolPauseMemory(held,heldOffset,heldStride,14);
   CHECK(SetEvent(notice));relayprocess::wait(command); // parent proves resume alone did not complete lease
   gst_memory_unref(held);held=nullptr;CHECK(SetEvent(notice));
  }else if(n==7)CHECK(SetEvent(notice));
 }
 if(cancelPaused){CHECK(received==4&&pcmFrames==192&&!held);noEos();}
 else{
  CHECK(received==8&&pcmFrames==384&&held);avTerminal(pipe,GST_MESSAGE_EOS);CHECK(eosEvents.load()==2);
  CHECK(gst_app_sink_is_eos(GST_APP_SINK(video))&&gst_app_sink_is_eos(GST_APP_SINK(audio)));identity();
  CHECK(SetEvent(notice));relayprocess::wait(command); // parent accepts authenticated terminal before teardown
  localPoolPauseState(pipe,GST_STATE_NULL);localPoolPauseMemory(held,heldOffset,heldStride,18);
  gst_memory_unref(held);held=nullptr;CHECK(SetEvent(notice));relayprocess::wait(command);
 }
 for(unsigned i=0;i<2;++i){gst_pad_remove_probe(eosPads[i],eosProbes[i]);gst_object_unref(eosPads[i]);}
 gst_object_unref(src);gst_object_unref(video);gst_object_unref(audio);gst_object_unref(pipe);
 puts(cancelPaused?"FAR5 paused cancellation:4 exact native/192 PCM, held pixels valid through NULL and cancellation, no EOS":"FAR5 pause/resume:8 exact native/384 PCM, unchanged session/generation/37ms, new media and real EOS after resume");return 0;
}
static void localPoolPauseControl(const char*plugin,bool cancelPaused){
 gchar*directory=g_dir_make_tmp("framelink-far5-pause-XXXXXX",nullptr);CHECK(directory);const std::string root=directory;g_free(directory);
 const auto inputPath=root+"/input",outputPath=root+"/output";
 flnative::Handle notice,command,advance,published,retired,sourceCancelled,sourceClose;
 for(auto*h:{&notice,&command,&advance,&published,&retired,&sourceCancelled,&sourceClose}){h->value=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(h->value);}
 const auto args=std::string("\"")+plugin+"\" --relay-local-pause-reader \""+outputPath+"\" "+std::to_string(uintptr_t(notice.value))+" "+std::to_string(uintptr_t(command.value))+" "+std::to_string(cancelPaused);
 auto child=relayprocess::spawn(args,{notice.value,command.value});
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);auto*source=fl_gst_session_new(3);auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
 std::atomic<unsigned> submitted{0};std::atomic<gint64> cancellationUs{0};
 std::thread producer([&]{
  flChannel*writer=nullptr;GstFrameLinkMemoryPolicy bound{};GError*error=nullptr;
  CHECK(fl_gst_session_open_local_native(source,TRUE,inputPath.c_str(),&caps,1,2,80,5000,5000,&candidate,&bound,&writer,nullptr,&error));
  flSessionConfig cfg{};CHECK(fl_gst_session_get_config(source,&cfg)==FL_GST_SESSION_READY);
  {LocalProcessAudio audio(source,true);audio.start();CHECK(fl_gst_session_local_ready(source,TRUE,5000,nullptr));
   ID3D11DeviceContext*ctx=nullptr;flChannelD3D11Device(writer)->GetImmediateContext(&ctx);CHECK(ctx);
   for(unsigned n=0;n<(cancelPaused?4u:8u);++n){
    relayprocess::wait(advance.value);flBuffer buffer{};CHECK(flAcquireBuffer(writer,&buffer,1000)==FL_OK);
    std::vector<unsigned char> pixels(64*32*4,static_cast<unsigned char>(n+11));ctx->UpdateSubresource(flImageD3D11(buffer.image),0,nullptr,pixels.data(),256,0);
    const gint64 pts=37000000+n*1000000;flFrameMetadata meta{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts,1000000,cfg.generation,{}};
    CHECK(flSetFrameMetadata(writer,&buffer,&meta)==FL_OK&&flSubmit(writer,&buffer,pts)==FL_OK);
    auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);guint8 bytes[192];for(unsigned i=0;i<sizeof bytes;++i)bytes[i]=guint8((n*29+i*7+31)&255);
    CHECK(gst_buffer_fill(pcm,0,bytes,sizeof bytes)==sizeof bytes);GST_BUFFER_PTS(pcm)=n*1000000;GST_BUFFER_DURATION(pcm)=1000000;
    CHECK(gst_app_src_push_buffer(GST_APP_SRC(audio.port),pcm)==GST_FLOW_OK);submitted.store(n+1);CHECK(SetEvent(published.value));relayprocess::wait(retired.value);
   }
   ctx->Release();
   if(cancelPaused){
    const auto began=g_get_monotonic_time();flSessionConfig snapshot{};FlGstSessionResult result;
    do{result=fl_gst_session_wait(source,cfg.generation,20,&snapshot);if(result==FL_GST_SESSION_READY)g_usleep(1000);CHECK(g_get_monotonic_time()-began<1500000);}while(result==FL_GST_SESSION_READY||result==FL_GST_SESSION_TIMEOUT);
    CHECK(result==FL_GST_SESSION_CANCELLED);cancellationUs.store(g_get_monotonic_time()-began);
    auto*bus=gst_element_get_bus(audio.pipe);CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);
    CHECK(SetEvent(sourceCancelled.value));relayprocess::wait(sourceClose.value);
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
  flsample::SessionNativeRelay video(up,down,setup.input,setup.output,relaySame);CHECK(video.initialize()==FL_OK);
  LocalProcessAudio input(up,false),output(down,true);flsample::SessionPcmRelay pcm(up,down,GST_APP_SINK(input.port),GST_APP_SRC(output.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);
  output.start();input.start();flsample::SessionRelaySupervisor supervisor(up,down);CHECK(supervisor.start()==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_local_ready(down,TRUE,1000,nullptr)&&fl_gst_session_local_ready(up,FALSE,1000,nullptr));
  FlGstVideoEvent barrier{FL_GST_VIDEO_BARRIER,0,37000000,0};CHECK(fl_gst_session_send_video_event(down,original.generation,&barrier)==FL_GST_SESSION_READY);
  for(unsigned n=0;n<(cancelPaused?4u:8u);++n){
   CHECK(SetEvent(advance.value));relayprocess::wait(published.value);flFrame frame{};CHECK(flAcquireFrame(setup.input,&frame,1000)==FL_OK&&frame.ptsNs==gint64(37000000+n*1000000));CHECK(video.forward(frame)==FL_OK);
   const auto audioDeadline=g_get_monotonic_time()+5000000;while(pcm.blockCount()<n+1){auto r=pcm.forward(20);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<audioDeadline);}
   if(n==3){
    relayprocess::wait(notice.value);CHECK(submitted.load()==4&&WaitForSingleObject(published.value,0)==WAIT_TIMEOUT);
    CHECK(video.collect()==FL_OK&&video.pending()==1&&!video.videoEnded());
    flSessionConfig still{};CHECK(fl_gst_session_get_config(up,&still)==FL_GST_SESSION_READY&&memcmp(&still,&original,sizeof original)==0);
    const auto began=g_get_monotonic_time();CHECK(SetEvent(command.value));relayprocess::wait(notice.value);
    CHECK(g_get_monotonic_time()-began<1500000&&submitted.load()==4);
    if(cancelPaused){
     CHECK(SetEvent(retired.value));relayprocess::wait(sourceCancelled.value);
     const auto cancelElapsed=g_get_monotonic_time()-began;
     CHECK(cancelElapsed<1500000&&supervisor.status()!=FL_GST_SESSION_READY);
     CHECK(video.pending()==1&&!video.videoEnded()&&pcm.blockCount()==4&&pcm.frameCount()==192&&pcm.byteCount()==768);
     FlGstVideoEvent absent{};CHECK(fl_gst_session_receive_video_event(up,original.generation,0,&absent)!=FL_GST_SESSION_READY);
     auto*bus=gst_element_get_bus(input.pipe);CHECK(!gst_bus_pop_filtered(bus,GST_MESSAGE_EOS));gst_object_unref(bus);
     CHECK(SetEvent(command.value));relayprocess::wait(notice.value); // real GstMemory unref, not manufactured native completion
     CHECK(SetEvent(command.value));relayprocess::join(child); // no reader survives graph disposal
     supervisor.stop();CHECK(SetEvent(sourceClose.value));producer.join();
     printf("paused FAR5 NULL propagated source cancellation in %.3fms (source observation %.3fms); no EOS, four submissions only\n",cancelElapsed/1000.0,cancellationUs.load()/1000.0);
     break;
    }
    CHECK(video.collect()==FL_OK&&video.pending()==1&&!video.videoEnded());
    CHECK(SetEvent(command.value));relayprocess::wait(notice.value); // allow actual reader release only after proving resume preserved ownership
   }else if(n==7){relayprocess::wait(notice.value);CHECK(video.collect()==FL_OK&&video.pending()==1);}
   const auto deadline=g_get_monotonic_time()+2000000;while(video.pending()>1){CHECK(g_get_monotonic_time()<deadline&&video.collect()==FL_OK);g_usleep(100);}
   CHECK(SetEvent(retired.value));
  }
  if(!cancelPaused){
   CHECK(submitted.load()==8&&video.count()==8&&pcm.blockCount()==8&&pcm.frameCount()==384&&pcm.byteCount()==1536);
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY&&video.videoEnded());avTerminal(input.pipe,GST_MESSAGE_EOS);
   CHECK(video.pending()==1&&pcm.finish(video,0)==FL_GST_SESSION_TIMEOUT);CHECK(pcm.endPublication(video,5000)==FL_GST_SESSION_READY);avTerminal(output.pipe,GST_MESSAGE_EOS);
   relayprocess::wait(notice.value);supervisor.stop();CHECK(supervisor.status()==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));CHECK(SetEvent(command.value));relayprocess::wait(notice.value);
   const auto deadline=g_get_monotonic_time()+2000000;while(video.pending()){CHECK(g_get_monotonic_time()<deadline&&video.collect()==FL_OK);g_usleep(100);}
   CHECK(video.finish()==FL_OK);CHECK(SetEvent(command.value));relayprocess::join(child);
   CHECK(fl_gst_session_finish_local(up,FALSE,1000,nullptr));producer.join();
  }
 }
 flReleaseD3D11PoolSnapshot(setup.snapshot);flClose(setup.output);flClose(setup.input);g_object_unref(source);g_object_unref(up);g_object_unref(down);
}

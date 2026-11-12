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
static int localPoolProcessReader(const char*path,HANDLE held,HANDLE release,HANDLE exitEvent,bool fixedFixture=true,bool audio=false){
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
  if(n==7){CHECK(SetEvent(held));relayprocess::wait(release);}
  CHECK(flCompleteReadLease(channel,lease)==FL_OK);CHECK(flCollectReadLeases(channel)==FL_OK);
 }
 if(audio){
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
 CHECK(fl_gst_session_finish_local(session,FALSE,1000,nullptr));
 pcm.reset();flClose(channel);g_object_unref(session);return 0;
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

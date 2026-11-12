// Independent upstream process scaffold for the separate-source FAR5 relay.
// Uses the existing FAR4 API only: no guessed consumer snapshot interface.
// Only test-control events are inherited, never textures or credentials.
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
static int producer(const char*path,HANDLE advance,HANDLE published,HANDLE retired,bool audio=false){
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native(session,TRUE,path,&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(bound.sessionGeneration==cfg.generation);
 std::unique_ptr<LocalProcessAudio> pcm;
 if(audio){pcm.reset(new LocalProcessAudio(session,true));pcm->start();}
 // Future relay parent waits to signal readiness until snapshot import and
 // actual same-allocation read-lease adapter binding have succeeded.
 CHECK(fl_gst_session_local_ready(session,TRUE,5000,nullptr));
 ID3D11DeviceContext*context=nullptr;flChannelD3D11Device(channel)->GetImmediateContext(&context);CHECK(context);
 for(unsigned n=0;n<Frames;++n){
  relayprocess::wait(advance);
  flBuffer buffer{};CHECK(flAcquireBuffer(channel,&buffer,1000)==FL_OK);
  std::vector<unsigned char> pixels(64*32*4,static_cast<unsigned char>(n+11));
  context->UpdateSubresource(flImageD3D11(buffer.image),0,nullptr,pixels.data(),256,0);
  flFrameMetadata meta{FL_FRAME_METADATA_VERSION,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,pts(n),1000000,cfg.generation,{}};
  CHECK(flSetFrameMetadata(channel,&buffer,&meta)==FL_OK);CHECK(flSubmit(channel,&buffer,pts(n))==FL_OK);
  if(audio){
   auto*buffer=gst_buffer_new_allocate(nullptr,192,nullptr);guint8 bytes[192];
   for(unsigned i=0;i<sizeof bytes;++i)bytes[i]=guint8((n*29+i*7+31)&255);
   CHECK(gst_buffer_fill(buffer,0,bytes,sizeof bytes)==sizeof bytes);
   GST_BUFFER_PTS(buffer)=n*1000000;GST_BUFFER_DURATION(buffer)=1000000;
   CHECK(gst_app_src_push_buffer(GST_APP_SRC(pcm->port),buffer)==GST_FLOW_OK);
  }
  CHECK(SetEvent(published));
  // A control gate bounds the fixture's outstanding frames; it is not proof
  // of native completion. The parent signals only after the real lease retires.
  relayprocess::wait(retired);
 }
 context->Release();
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
 flnative::Handle advance,published,retired;PROCESS_INFORMATION child{};
 Peer(const char*plugin,const std::string&path,bool audio=false){
  advance.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  published.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  retired.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  CHECK(advance.value&&published.value&&retired.value);
  const auto command=std::string("\"")+plugin+(audio?"\" --relay-local-source-av \"":"\" --relay-local-source \"")+path+"\" "+
    std::to_string(uintptr_t(advance.value))+" "+std::to_string(uintptr_t(published.value))+" "+std::to_string(uintptr_t(retired.value));
  child=relayprocess::spawn(command,{advance.value,published.value,retired.value});
 }
 Peer(const Peer&)=delete;Peer&operator=(const Peer&)=delete;
 void next(){CHECK(SetEvent(advance.value));relayprocess::wait(published.value);}
 void release(){CHECK(SetEvent(retired.value));}
 void join(){relayprocess::join(child);child={};}
 void fail(){
  CHECK(TerminateProcess(child.hProcess,73));relayprocess::wait(child.hProcess);
  DWORD result=0;CHECK(GetExitCodeProcess(child.hProcess,&result)&&result==73);
  CloseHandle(child.hThread);CloseHandle(child.hProcess);child={};
 }
};
// A focused upstream-only control for the new source child. This does not claim
// a three-process relay: the consumer snapshot integration is a separate step.
static void control(const char*plugin){
 gchar*dir=g_dir_make_tmp("framelink-local-source-XXXXXX",nullptr);CHECK(dir);
 const std::string path=std::string(dir)+"/session";g_free(dir);
 Peer source(plugin,path);
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
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
 puts("separate local source control: authenticated FAR4, 8 exact frames/metadata, no inherited textures, real read leases before control acknowledgment");
}
// Three independent processes: source child, this relay, final reader child.
// Uses the committed backend snapshot contract, never inherited texture handles.
struct HeldGraph { flSessionConfig config{};void*handles[2]{}; };
static void threeProcess(const char*plugin,bool audio=false,bool replaceWhileHeld=false,const HeldGraph*old=nullptr,bool failedSource=false){
 gchar*dir=g_dir_make_tmp("framelink-three-local-XXXXXX",nullptr);CHECK(dir);
 const std::string root=dir;g_free(dir);const auto inputPath=root+"/input",outputPath=root+"/output";
 Peer source(plugin,inputPath,audio);
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy inputPolicy{},outputPolicy{};
 auto*up=fl_gst_session_new(3);auto*down=fl_gst_session_new(3);
 flChannel*input=nullptr,*output=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native_relay_input(up,inputPath.c_str(),&caps,1,2,80,5000,5000,
   &candidate,&inputPolicy,&input,nullptr,&error));
 flSessionConfig original{};CHECK(fl_gst_session_get_config(up,&original)==FL_GST_SESSION_READY);
 // Attachment already happened, but source has not been told to publish.
 flD3D11PoolSnapshot*snapshot=nullptr;CHECK(flSnapshotD3D11ConsumerPool(input,&snapshot)==FL_OK);
 flChannelInfo info{};CHECK(flQueryD3D11PoolSnapshot(snapshot,&info)==FL_OK&&info.poolSize==2);
 void*handles[2]{};for(unsigned n=0;n<2;++n){handles[1-n]=flD3D11PoolSnapshotHandle(snapshot,n);CHECK(handles[1-n]);}
 flnative::Handle held,release,exitEvent;
 held.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);release.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);exitEvent.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 CHECK(held.value&&release.value&&exitEvent.value);
 const auto command=std::string("\"")+plugin+(failedSource?"\" --relay-local-reader-failed \"":(audio?"\" --relay-local-reader-av \"":"\" --relay-local-reader-generic \""))+outputPath+"\" "+
   std::to_string(uintptr_t(held.value))+" "+std::to_string(uintptr_t(release.value))+" "+std::to_string(uintptr_t(exitEvent.value));
 auto final=relayprocess::spawn(command,{held.value,release.value,exitEvent.value});
 CHECK(fl_gst_session_open_local_native_relay_pool(down,up,outputPath.c_str(),&caps,1,2,80,5000,5000,
   &candidate,info.adapterLUID,handles,&outputPolicy,&output,nullptr,&error));
 flSessionConfig forwarded{};CHECK(fl_gst_session_get_config(down,&forwarded)==FL_GST_SESSION_READY);
 CHECK(flsample::SessionRelayContract::compatible(original,forwarded));
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));CHECK(compare);
 if(old){
  CHECK(memcmp(original.sessionId.bytes,old->config.sessionId.bytes,16)!=0);
  CHECK(memcmp(original.clockId.bytes,old->config.clockId.bytes,16)!=0);
  CHECK(!flsample::SessionRelayContract::compatible(old->config,forwarded));
  for(auto current:handles)for(auto prior:old->handles)CHECK(!compare(current,prior));
 }
 flsample::SessionNativeRelay relay(up,down,input,output,[&](flImage*a,flImage*b){return compare(flImageSharedHandle(a),flImageSharedHandle(b))!=FALSE;});
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
 for(unsigned n=0;n<Frames;++n){
  source.next();flFrame frame{};CHECK(flAcquireFrame(input,&frame,1000)==FL_OK);
  CHECK(frame.ptsNs==pts(n));CHECK(relay.forward(frame)==FL_OK);
  if(audio){
   const auto until=g_get_monotonic_time()+5000000;FlGstSessionResult result;
   do{result=pcm->forward(100);CHECK(result==FL_GST_SESSION_READY||result==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<until);}while(result==FL_GST_SESSION_TIMEOUT);
  }
  if(n+1==Frames){
   relayprocess::wait(held.value);CHECK(relay.pending()==1);
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
    HeldGraph heldGraph{original,{handles[0],handles[1]}};
    threeProcess(plugin,true,false,&heldGraph);
    CHECK(relay.pending()==1);
    if(!failedSource)CHECK(relay.collect()==FL_OK);
    if(!failedSource){flSessionConfig unchanged{};CHECK(fl_gst_session_get_config(up,&unchanged)==FL_GST_SESSION_READY);CHECK(memcmp(&unchanged,&original,sizeof original)==0);}
   }
   CHECK(SetEvent(release.value));
   if(failedSource){
    // Source is already stopped. Retire the entire failed graph only after
    // the old final reader has completed its real lease and exited.
    CHECK(SetEvent(exitEvent.value));relayprocess::join(final);
    CHECK(relay.pending()==1&&!relay.videoEnded());supervisor.stop();
    pcm.reset();pcmOutput.reset();pcmInput.reset();
    flReleaseD3D11PoolSnapshot(snapshot);flClose(output);flClose(input);
    g_object_unref(down);g_object_unref(up);
    puts("source-death replacement: bounded cancellation, no synthetic END/EOS, fresh A/V graph with disjoint allocations, old held pixels intact before final-reader completion");
    return;
   }
  }
  const auto deadline=g_get_monotonic_time()+2000000;
  while(relay.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
  // A control acknowledgment follows real final-reader lease completion only.
  source.release();
 }
 CHECK(relay.count()==Frames);
 if(audio){
  CHECK(relay.forwardEvent(5000)==FL_GST_SESSION_READY&&relay.videoEnded());
  avTerminal(pcmInput->pipe,GST_MESSAGE_EOS);
  CHECK(pcm->finish(relay,5000)==FL_GST_SESSION_READY);
  CHECK(pcm->blockCount()==8&&pcm->frameCount()==384&&pcm->byteCount()==1536);
 }
 CHECK(relay.finish()==FL_OK);
 supervisor.stop();CHECK(supervisor.status()==FL_GST_SESSION_READY);
 CHECK(SetEvent(exitEvent.value));CHECK(fl_gst_session_finish_local(down,TRUE,1000,nullptr));
 CHECK(fl_gst_session_finish_local(up,FALSE,1000,nullptr));
 relayprocess::join(final);source.join();
 // The snapshot pins the source epoch for the ENTIRE graph, not only import.
 pcm.reset();pcmOutput.reset();pcmInput.reset();
 flReleaseD3D11PoolSnapshot(snapshot);flClose(output);flClose(input);
 g_object_unref(down);g_object_unref(up);
 if(replaceWhileHeld)puts("fresh-session replacement: distinct session/clock/native allocations; old reader held through complete replacement A/V graph; old pixels rechecked before real release");
 if(audio)puts("three-process local authenticated A/V: 384 exact PCM frames/1536 bytes, 37ms offset, real END/EOS, final native lease held");
 puts("three-process local native relay: FAR4 source-time input + FAR5 snapshot transfer, no inherited textures, 8 exact final pixels/metadata, held final lease, real drain");
}

}

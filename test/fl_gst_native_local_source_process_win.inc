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
static int producer(const char*path,HANDLE advance,HANDLE published,HANDLE retired){
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native(session,TRUE,path,&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(bound.sessionGeneration==cfg.generation);
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
  CHECK(SetEvent(published));
  // A control gate bounds the fixture's outstanding frames; it is not proof
  // of native completion. The parent signals only after the real lease retires.
  relayprocess::wait(retired);
 }
 context->Release();CHECK(fl_gst_session_finish_local(session,TRUE,5000,nullptr));
 flClose(channel);g_object_unref(session);return 0;
}
struct Peer {
 flnative::Handle advance,published,retired;PROCESS_INFORMATION child{};
 Peer(const char*plugin,const std::string&path){
  advance.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  published.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  retired.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  CHECK(advance.value&&published.value&&retired.value);
  const auto command=std::string("\"")+plugin+"\" --relay-local-source \""+path+"\" "+
    std::to_string(uintptr_t(advance.value))+" "+std::to_string(uintptr_t(published.value))+" "+std::to_string(uintptr_t(retired.value));
  child=relayprocess::spawn(command,{advance.value,published.value,retired.value});
 }
 Peer(const Peer&)=delete;Peer&operator=(const Peer&)=delete;
 void next(){CHECK(SetEvent(advance.value));relayprocess::wait(published.value);}
 void release(){CHECK(SetEvent(retired.value));}
 void join(){relayprocess::join(child);child={};}
};
// A focused upstream-only control for the new source child. This does not claim
// a three-process relay: the consumer snapshot integration is a separate step.
static void control(const char*plugin){
 gchar*dir=g_dir_make_tmp("framelink-local-source-XXXXXX",nullptr);CHECK(dir);
 const std::string path=std::string(dir)+"/session";g_free(dir);
 Peer source(plugin,path);
 auto caps=localPoolProcessCaps();auto candidate=nativeAvPolicy(0,true);GstFrameLinkMemoryPolicy bound{};
 auto*session=fl_gst_session_new(3);flChannel*channel=nullptr;GError*error=nullptr;
 CHECK(fl_gst_session_open_local_native(session,FALSE,path.c_str(),&caps,1,2,80,5000,5000,
   &candidate,&bound,&channel,nullptr,&error));
 flSessionConfig cfg{};CHECK(fl_gst_session_get_config(session,&cfg)==FL_GST_SESSION_READY);
 CHECK(cfg.generation&&bound.sessionGeneration==cfg.generation);
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
}

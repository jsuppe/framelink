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
// The FAR5 reader, bin reader and bin negatives moved to the shared
// fl_gst_native_relay.cpp; the pause controls below still use the helpers above.


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
  fprintf(stderr,"pause final memory refs before NULL: %d\n",GST_MINI_OBJECT_REFCOUNT_VALUE(held));
  localPoolPauseState(pipe,GST_STATE_NULL);localPoolPauseMemory(held,heldOffset,heldStride,18);
  fprintf(stderr,"pause final memory refs after NULL: %d\n",GST_MINI_OBJECT_REFCOUNT_VALUE(held));
  gst_memory_unref(held);held=nullptr;fprintf(stderr,"pause final unref returned\n");CHECK(SetEvent(notice));relayprocess::wait(command);
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
   const auto deadline=g_get_monotonic_time()+2000000;while(video.pending()){
    const auto collected=video.collect();
    if(collected!=FL_OK||g_get_monotonic_time()>=deadline){
     gchar*log=nullptr;gsize length=0;if(g_file_get_contents((outputPath+".pause-reader.log").c_str(),&log,&length,nullptr)){fwrite(log,1,length,stderr);g_free(log);}
     flChannelInfo inputInfo{},outputInfo{};const auto inputQuery=flQuery(setup.input,&inputInfo),outputQuery=flQuery(setup.output,&outputInfo);
     DWORD exitCode=0;CHECK(GetExitCodeProcess(child.hProcess,&exitCode));
     fprintf(stderr,"pause final collect=%d pending=%u inputQuery=%d inputGeneration=%llu outputQuery=%d outputGeneration=%llu childExit=%lu\n",int(collected),video.pending(),int(inputQuery),(unsigned long long)inputInfo.generation,int(outputQuery),(unsigned long long)outputInfo.generation,(unsigned long)exitCode);CHECK(false);
    }
    g_usleep(100);
   }
   CHECK(video.finish()==FL_OK);CHECK(SetEvent(command.value));relayprocess::join(child);
   CHECK(fl_gst_session_finish_local(up,FALSE,1000,nullptr));producer.join();
  }
 }
 flReleaseD3D11PoolSnapshot(setup.snapshot);flClose(setup.output);flClose(setup.input);g_object_unref(source);g_object_unref(up);g_object_unref(down);
}

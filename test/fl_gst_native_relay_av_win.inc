// Explicit broker-composed two-hop diagnostic. No automatic-discovery promise:
// the broker supplies the SAME immutable allocation pool to both video rings.
#include "../samples/session_native_relay.h"
#include "../samples/session_relay_contract.h"
#include <dxgi1_3.h>
#include <wrl/client.h>
static bool relaySame(flImage*a,flImage*b){
 using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);
 auto compare=reinterpret_cast<Compare>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles"));
 return compare&&compare(flImageSharedHandle(a),flImageSharedHandle(b));
}
static void relaySessionNegatives(flSessionConfig cfg,const flSessionCapabilities&offer){
 auto second=cfg;second.sessionId.bytes[0]=2;
 CHECK(flsample::SessionRelayContract::compatible(cfg,second));
 for(unsigned fault=0;fault<14;++fault){
  auto bad=second;
  switch(fault){
   case 0:bad.clockId.bytes[0]^=1;break;case 1:++bad.epochNs;break;
   case 2:++bad.generation;break;case 3:bad.sessionId=cfg.sessionId;break;
   case 4:++bad.video.width;break;case 5:++bad.video.height;break;
   case 6:bad.video.format=FL_FORMAT_NV12;break;case 7:++bad.video.fpsNum;break;
   case 8:++bad.video.fpsDen;break;case 9:bad.audio.pcm=FL_PCM_F32;break;
   case 10:bad.audio.rate=44100;break;case 11:bad.audio.layout=FL_LAYOUT_MONO;break;
   case 12:bad.videoTrackId=3;break;case 13:bad.audioTrackId=3;break;
  }
  CHECK(!flsample::SessionRelayContract::compatible(cfg,bad));
 }
 // Queue capacities are per-hop ownership limits, not media timeline changes.
 auto queues=second;++queues.videoQueueFrames;++queues.audioQueueMs;
 CHECK(flsample::SessionRelayContract::compatible(cfg,queues));
 for(unsigned mode=0;mode<3;++mode){
  auto*a=fl_gst_session_new(3);auto*b=fl_gst_session_new(3);
  CHECK(fl_gst_session_configure(a,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_configure(b,&second,&offer,&offer)==FL_GST_SESSION_READY);
  if(mode){flClockMapping m{};m.sourceClockId=cfg.clockId;m.targetClockId.bytes[0]=9;
   m.sourceAnchorNs=cfg.epochNs;m.targetAnchorNs=mode==1?1000000:0;
   CHECK(fl_gst_session_set_mapping(a,1,&m,0)==FL_GST_SESSION_READY);}
  flsample::SessionRelayContract contract(a,b);
  CHECK(contract.check()==FL_GST_SESSION_INVALID);
  CHECK(contract.initialize()==(mode==2?FL_GST_SESSION_READY:FL_GST_SESSION_INVALID));
  if(mode==2){
   CHECK(contract.initialize()==FL_GST_SESSION_INVALID);CHECK(contract.check()==FL_GST_SESSION_READY);
   auto replacement=second;++replacement.generation;
   CHECK(fl_gst_session_configure(b,&replacement,&offer,&offer)==FL_GST_SESSION_READY);
   CHECK(contract.check()==FL_GST_SESSION_STALE);
   fl_gst_session_cancel(a);CHECK(contract.check()==FL_GST_SESSION_CANCELLED);
  }
  g_object_unref(a);g_object_unref(b);
 }
 printf("relay session contract: 14 mismatch negatives, queue independence, missing/nonidentity mapping, stale generation and cancellation passed\n");
}
static void nativeRelayAv(bool empty,unsigned fault=0){
 using Microsoft::WRL::ComPtr;
 FlGstSession* sessions[4]{};
 GstElement* pipes[4]{},*ports[4]{};
 flSessionConfig cfg{};cfg.version=2;cfg.clockId.bytes[0]=2;cfg.generation=1;cfg.epochNs=5000000;
 cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
 cfg.video={FL_FORMAT_RGBA8,64,32,1000,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=2000;
 flSessionCapabilities offer{};offer.version=2;offer.videoCount=offer.audioCount=1;offer.video[0]=cfg.video;offer.audio[0]=cfg.audio;
 cfg.sessionId.bytes[0]=1;relaySessionNegatives(cfg,offer);
 for(unsigned hop=0;hop<2;++hop){
  cfg.sessionId.bytes[0]=static_cast<uint8_t>(hop+1);
  for(unsigned role=0;role<2;++role){
   const auto i=hop*2+role;sessions[i]=fl_gst_session_new(3);
   CHECK(fl_gst_session_configure(sessions[i],&cfg,&offer,&offer)==FL_GST_SESSION_READY);
   CHECK(fl_gst_session_register_track(sessions[i],FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
  }
  // Independent credentials and session identities; common source media clock.
  guint8 secret[32]{};secret[0]=static_cast<guint8>(73+hop);auto*key=g_bytes_new(secret,32);
  for(unsigned kind=0;kind<3;++kind){
   GSocketConnection*a=nullptr,*b=nullptr;avStreamPair(&a,&b);gboolean ok=FALSE;
   auto policy=nativeAvPolicy(1,true);
   auto auth=[&](unsigned role,GSocketConnection*c){
    if(kind==2)return fl_gst_session_authenticate_native_memory(sessions[2*hop+role],c,role==0,key,&policy,2000,nullptr,nullptr);
    auto fn=kind?fl_gst_session_authenticate_event_stream:fl_gst_session_authenticate_audio_stream;
    return fn(sessions[2*hop+role],c,role==0,key,2000,nullptr,nullptr);
   };
   std::thread peer([&]{ok=auth(0,a);});CHECK(auth(1,b));peer.join();CHECK(ok);
   g_object_unref(a);g_object_unref(b);
  }
  g_bytes_unref(key);
  flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=hop?8:7;
  mapping.sourceAnchorNs=cfg.epochNs;mapping.targetAnchorNs=hop?1000000:0;
  CHECK(fl_gst_session_set_mapping(sessions[2*hop+1],1,&mapping,0)==FL_GST_SESSION_READY);
  for(unsigned role=0;role<2;++role){
   const auto i=hop*2+role;GError*error=nullptr;
   pipes[i]=gst_parse_launch(role?"framelinkaudiosrc tracks=av ! appsink name=port sync=false async=false enable-last-sample=false":"appsrc name=port is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av",&error);
   CHECK(pipes[i]&&!error);ports[i]=gst_bin_get_by_name(GST_BIN(pipes[i]),"port");CHECK(ports[i]);
   auto*context=fl_gst_session_create_context(sessions[i]);gst_element_set_context(pipes[i],context);gst_context_unref(context);
  }
 }
 flsample::SessionRelayContract contract(sessions[1],sessions[2]);
 CHECK(contract.initialize()==FL_GST_SESSION_READY);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=32;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
 desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
 desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ComPtr<ID3D11Texture2D> textures[2];void*handles[2]{};
 for(unsigned i=0;i<2;++i){CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&textures[i])));ComPtr<IDXGIResource1> resource;
  CHECK(SUCCEEDED(textures[i].As(&resource)));CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handles[i])));}
 auto name="native-relay-av-"+std::to_string(g_get_monotonic_time());
 flChannel *up=nullptr,*writer=nullptr,*down=nullptr,*forward=nullptr;void*reversed[2]={handles[1],handles[0]};
 const auto flags=FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA;
 CHECK(flCreateChannelFromD3D11Handles((name+".up").c_str(),64,32,FL_FORMAT_RGBA8,2,flags,nullptr,handles,&up)==FL_OK);
 CHECK(flCreateChannelFromD3D11Handles((name+".down").c_str(),64,32,FL_FORMAT_RGBA8,2,flags,nullptr,reversed,&down)==FL_OK);
 CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);CHECK(flOpenProducer((name+".down").c_str(),&forward)==FL_OK);
 flsample::SessionNativeRelay relay(sessions[1],sessions[2],up,forward,relaySame);CHECK(relay.initialize()==FL_OK);
 for(auto*session:sessions)CHECK(fl_gst_session_track_ready(session,FL_GST_TRACK_VIDEO,1)==FL_GST_SESSION_READY);
 for(auto i:{3,2,1,0})CHECK(gst_element_set_state(pipes[i],GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 flBuffer spare{};CHECK(flAcquireBuffer(writer,&spare,1000)==FL_OK);
 const unsigned frames=empty?0:16;
 for(unsigned n=0;n<frames;++n){
  if(fault&&n)break;
  flBuffer input{};CHECK(flAcquireBuffer(writer,&input,1000)==FL_OK);
  // Explicit producer upload and final pixel-oracle readback; relay never maps.
  flMappedImage pixels{};CHECK(flImageMapPlanes(input.image,&pixels)==FL_OK);
  for(unsigned y=0;y<32;++y)memset(static_cast<char*>(pixels.plane[0].pixels)+y*pixels.plane[0].stride,n+1,256);
  flImageUnmap(input.image);
  const uint64_t audioPts=n*1000000,videoPts=audioPts+37000000;
  flFrameMetadata metadata{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(videoPts-100),1000000,1,{}};
  if(fault==1)metadata.streamGeneration=2;
  CHECK(flSetFrameMetadata(writer,&input,&metadata)==FL_OK);CHECK(flSubmit(writer,&input,videoPts)==FL_OK);
  flFrame incoming{};CHECK(flAcquireFrame(up,&incoming,1000)==FL_OK);CHECK(contract.check()==FL_GST_SESSION_READY);
  if(fault==2)fl_gst_session_cancel(sessions[1]);
  if(fault==3)fl_gst_session_cancel(sessions[2]);
  if(fault==4){auto replacement=cfg;replacement.sessionId.bytes[0]=2;++replacement.generation;
   CHECK(fl_gst_session_configure(sessions[2],&replacement,&offer,&offer)==FL_GST_SESSION_READY);}
  if(fault&&fault<5){
   CHECK(relay.forward(incoming)==(fault==1?FL_INVALID:FL_DISCONNECTED));
   CHECK(relay.sessionStatus()==(fault==1?FL_GST_SESSION_READY:fault==4?FL_GST_SESSION_STALE:FL_GST_SESSION_CANCELLED));
   CHECK(relay.count()==0&&relay.pending()==0);
   flFrame absent{};CHECK(flAcquireFrame(down,&absent,0)==FL_TIMEOUT);
   CHECK(relay.forward(incoming)==FL_INVALID); // permanent publication stop
   flRelease(up,&incoming);break;
  }
  CHECK(relay.forward(incoming)==FL_OK);
  flFrame output{};CHECK(flAcquireFrame(down,&output,1000)==FL_OK&&output.ptsNs==videoPts&&relaySame(input.image,output.image));
  flFrameMetadata received{};CHECK(flGetFrameMetadata(down,&output,&received)==FL_OK);
  CHECK(received.dtsNs==metadata.dtsNs&&received.durationNs==metadata.durationNs&&received.streamGeneration==1);
  uint64_t lease=0;CHECK(flLeaseFrame(down,&output,&lease)==FL_OK);
  flBuffer blocked{};CHECK(flAcquireBuffer(writer,&blocked,0)==FL_TIMEOUT&&relay.pending()==1);
  if(fault==5){
   fl_gst_session_cancel(sessions[1]);
   CHECK(relay.finish()==FL_BUSY&&relay.pending()==1);
   CHECK(flAcquireBuffer(writer,&blocked,0)==FL_TIMEOUT);
   CHECK(flCompleteReadLease(down,lease)==FL_OK);CHECK(flCollectReadLeases(down)==FL_OK);
   const auto drainDeadline=g_get_monotonic_time()+1000000;
   while(relay.pending()){CHECK(g_get_monotonic_time()<drainDeadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
   break;
  }
  auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(pcm,0,n+31,192);GST_BUFFER_PTS(pcm)=audioPts;GST_BUFFER_DURATION(pcm)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(ports[0]),pcm)==GST_FLOW_OK);
  auto*first=gst_app_sink_try_pull_sample(GST_APP_SINK(ports[1]),2*GST_SECOND);CHECK(first);
  auto*middle=gst_sample_get_buffer(first);CHECK(GST_BUFFER_PTS(middle)==audioPts);
  CHECK(contract.check()==FL_GST_SESSION_READY);
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(ports[2]),gst_buffer_ref(middle))==GST_FLOW_OK);gst_sample_unref(first);
  auto*last=gst_app_sink_try_pull_sample(GST_APP_SINK(ports[3]),2*GST_SECOND);CHECK(last);auto*audio=gst_sample_get_buffer(last);
  gint64 mappedVideo=-1;CHECK(fl_gst_session_map_running_time(sessions[3],1,output.ptsNs,TRUE,&mappedVideo)==FL_GST_SESSION_READY);
  CHECK(GST_BUFFER_PTS(audio)==audioPts+1000000&&uint64_t(mappedVideo)-GST_BUFFER_PTS(audio)==37000000&&GST_BUFFER_DURATION(audio)==1000000);
  guint8 bytes[192];CHECK(gst_buffer_get_size(audio)==sizeof bytes&&gst_buffer_extract(audio,0,bytes,sizeof bytes)==sizeof bytes);
  for(auto value:bytes)CHECK(value==n+31);gst_sample_unref(last);
  CHECK(flImageMapPlanes(output.image,&pixels)==FL_OK);
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<unsigned char*>(pixels.plane[0].pixels)[y*pixels.plane[0].stride+x]==n+1);
  flImageUnmap(output.image);
  CHECK(flCompleteReadLease(down,lease)==FL_OK);CHECK(flCollectReadLeases(down)==FL_OK);
  const auto deadline=g_get_monotonic_time()+1000000;
  while(relay.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(relay.collect()==FL_OK);g_usleep(100);}
  FlGstVideoEvent sent{FL_GST_VIDEO_BARRIER,n+1,videoPts,0},mid{},end{};
  CHECK(fl_gst_session_send_video_event(sessions[0],1,&sent)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_receive_video_event(sessions[1],1,1000,&mid)==FL_GST_SESSION_READY);
  CHECK(mid.kind==sent.kind&&mid.afterFrames==relay.count()&&mid.ptsNs==sent.ptsNs);
  CHECK(contract.check()==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_send_video_event(sessions[2],1,&mid)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_receive_video_event(sessions[3],1,1000,&end)==FL_GST_SESSION_READY);
  CHECK(end.kind==sent.kind&&end.afterFrames==sent.afterFrames&&end.ptsNs==sent.ptsNs&&end.durationNs==0);
 }
 CHECK(relay.count()==(fault?(fault==5?1:0):frames)&&relay.finish()==FL_OK);CHECK(flCancelBuffer(writer,&spare)==FL_OK);
 // PCM EOS authenticates the first-hop video terminal before relay publication.
 if(!fault){
 const gint64 lastPts=frames?gint64(37000000)+gint64(frames-1)*1000000:gint64(-1);
 CHECK(fl_gst_session_video_eos(sessions[0],1,frames,lastPts)==FL_GST_SESSION_READY);
 CHECK(gst_app_src_end_of_stream(GST_APP_SRC(ports[0]))==GST_FLOW_OK);avTerminal(pipes[1],GST_MESSAGE_EOS);
 guint64 count=99;gint64 pts=-2;CHECK(fl_gst_session_wait_video_eos(sessions[1],1,1000,&count,&pts)==FL_GST_SESSION_READY&&count==frames&&pts==lastPts);
 CHECK(contract.check()==FL_GST_SESSION_READY);
 CHECK(fl_gst_session_video_eos(sessions[2],1,count,pts)==FL_GST_SESSION_READY);
 CHECK(gst_app_src_end_of_stream(GST_APP_SRC(ports[2]))==GST_FLOW_OK);avTerminal(pipes[3],GST_MESSAGE_EOS);
 CHECK(fl_gst_session_wait_video_eos(sessions[3],1,1000,&count,&pts)==FL_GST_SESSION_READY&&count==frames&&pts==lastPts);
 }
 for(unsigned i=0;i<4;++i){CHECK(gst_element_set_state(pipes[i],GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(ports[i]);gst_object_unref(pipes[i]);g_object_unref(sessions[i]);}
 flClose(forward);flClose(writer);flClose(down);flClose(up);for(auto handle:handles)CloseHandle(handle);
 if(fault){printf("checked native relay fault %u: no unauthorized publication, retained ownership and safe drain passed\n",fault);return;}
 printf("coordinated native relay: %u same-allocation video frames / %u PCM frames, two authenticated sessions, exact metadata, 37ms offset, ordered barriers, held-reader backpressure and terminal drain passed\n",frames,frames*48);
}

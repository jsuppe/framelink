// Broker-composed process relays: three independent processes (source, relay,
// final reader) under a supervisor that owns the allocations, the test
// credentials and every process lifetime. The supervisor allocates two images
// once and hands the same native handles to both rings. Not a production
// broker, discovery or performance test.
//
// Platform parts are in fl_gst_native_relay_platform.h: Child (with inherited
// allocation handles), Gate, Mailbox, SharedImages, and the reservation owner.
namespace relayprocess {
static constexpr unsigned Frames=16;
static uint64_t frameTime(unsigned n,bool decoder){return decoder?gst_util_uint64_scale(n,GST_SECOND,30):n*1000000ULL;}
static uint64_t frameDuration(unsigned /*n*/,bool decoder){return frameTime(1,decoder);}
static uint64_t pixelHash(const unsigned char*data,size_t stride){
 uint64_t hash=14695981039346656037ULL;
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x){hash^=data[y*stride+x];hash*=1099511628211ULL;}return hash;
}
static bool same(flImage*a,flImage*b){return relaytest::sameAllocation(a,b);}

// Every control signal of one graph, created by the supervisor and inherited
// by each child (one command-line argument).
enum Sig{Held,Checked,Done,RelayDone,FaultChecked,Attached,Drained,ReaderReady,Never,SigCount};
struct Signals {
 std::unique_ptr<relaytest::Gate> gate[SigCount];
 Signals(){for(auto&g:gate)g.reset(new relaytest::Gate());}
 explicit Signals(const char*arg){
  std::string all(arg);size_t at=0;
  for(auto&g:gate){auto end=all.find(';',at);CHECK(end!=std::string::npos||&g==&gate[SigCount-1]);
   g.reset(new relaytest::Gate(all.substr(at,end==std::string::npos?std::string::npos:end-at).c_str()));at=end+1;}
 }
 std::string arg()const{std::string out;for(auto&g:gate){if(!out.empty())out+=";";out+=g->arg();}return out;}
 std::vector<const relaytest::Gate*> all()const{std::vector<const relaytest::Gate*> v;for(auto&g:gate)v.push_back(g.get());return v;}
 relaytest::Gate& operator[](Sig s){return *gate[s];}
};

#ifdef _WIN32
// Explicit upstream decode/color/scale into a reservation, never a relay copy.
// Windows only until the phase 5 NVDEC import (the Linux test skips).
struct Decoder {
 GstElement*pipeline=nullptr,*sink=nullptr;GstD3D11Converter*converter=nullptr;
 GstClockTime origin=GST_CLOCK_TIME_NONE;std::vector<uint64_t> hashes;
 uint64_t fill(GstBuffer*buffer,unsigned n){
  auto*mem=gst_buffer_peek_memory(buffer,0);auto*device=GST_D3D11_MEMORY_CAST(mem)->device;
  if(!pipeline){
   GError*error=nullptr;
   pipeline=gst_parse_launch("videotestsrc num-buffers=16 pattern=snow ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 ! h264parse ! d3d11h264dec ! video/x-raw(memory:D3D11Memory),format=NV12 ! appsink name=decoded sync=false enable-last-sample=false max-buffers=1",&error);
   CHECK(pipeline&&!error);auto*context=gst_d3d11_context_new(device);gst_element_set_context(pipeline,context);gst_context_unref(context);
   sink=gst_bin_get_by_name(GST_BIN(pipeline),"decoded");CHECK(sink);
   CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  }
  auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);CHECK(sample);
  auto*input=gst_sample_get_buffer(sample);CHECK(gst_buffer_n_memory(input)==1);
  auto*memory=gst_buffer_peek_memory(input,0);CHECK(gst_is_d3d11_memory(memory)&&GST_D3D11_MEMORY_CAST(memory)->device==device);
  CHECK(!gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),FrameLinkD3DProducer::key()));
  CHECK(GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(input))&&!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DTS(input)));
  if(!n)origin=GST_BUFFER_PTS(input);
  fprintf(stderr,"DECODED,%u,pts=%llu,duration=%llu,origin=%llu\n",n,(unsigned long long)GST_BUFFER_PTS(input),(unsigned long long)GST_BUFFER_DURATION(input),(unsigned long long)origin);
  CHECK(GST_BUFFER_PTS(input)-origin==frameTime(n,true)&&GST_BUFFER_DURATION(input)==frameDuration(n,true));
  if(!converter){GstVideoInfo in{},out{};CHECK(gst_video_info_from_caps(&in,gst_sample_get_caps(sample)));
   CHECK(GST_VIDEO_INFO_FORMAT(&in)==GST_VIDEO_FORMAT_NV12&&GST_VIDEO_INFO_WIDTH(&in)==320&&GST_VIDEO_INFO_HEIGHT(&in)==240);
   gst_video_info_set_format(&out,GST_VIDEO_FORMAT_RGBA,64,32);out.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;
   converter=gst_d3d11_converter_new(device,&in,&out,nullptr);CHECK(converter);}
  CHECK(gst_d3d11_converter_convert_buffer(converter,input,buffer));
  auto*d=gst_d3d11_device_get_device_handle(device);auto*c=gst_d3d11_device_get_device_context_handle(device);
  D3D11_QUERY_DESC description{D3D11_QUERY_EVENT,0};ID3D11Query*done=nullptr;CHECK(SUCCEEDED(d->CreateQuery(&description,&done)));
  gst_d3d11_device_lock(device);c->End(done);c->Flush();HRESULT result;const auto deadline=g_get_monotonic_time()+2000000;
  do{result=c->GetData(done,nullptr,0,0);if(result==S_FALSE)g_usleep(100);}while(result==S_FALSE&&g_get_monotonic_time()<deadline);
  gst_d3d11_device_unlock(device);done->Release();CHECK(result==S_OK);gst_sample_unref(sample);
  auto*meta=gst_buffer_get_video_meta(buffer);GstMapInfo map{};CHECK(meta&&gst_memory_map(mem,&map,GST_MAP_READ));
  auto*data=map.data+meta->offset[0];for(unsigned y=0;y<32;++y)for(unsigned x=3;x<256;x+=4)CHECK(data[y*meta->stride[0]+x]==255);
  const auto hash=pixelHash(data,meta->stride[0]);gst_memory_unmap(mem,&map);
  for(auto previous:hashes)CHECK(previous!=hash);hashes.push_back(hash);
  fprintf(stderr,"DECODED_PIXELS,%u,%llu\n",n,(unsigned long long)hash);return hash;
 }
 ~Decoder(){if(pipeline){CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND));CHECK(gst_app_sink_is_eos(GST_APP_SINK(sink)));avTerminal(pipeline,GST_MESSAGE_EOS);
  CHECK(gst_element_set_state(pipeline,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(sink);gst_object_unref(pipeline);gst_object_unref(converter);}}
};
#else
struct Decoder {uint64_t fill(GstBuffer*,unsigned){CHECK(!"decoder relay is Windows-only until phase 5");return 0;}};
#endif
static bool decoderAvailable(){
#ifdef _WIN32
 for(auto name:{"videotestsrc","x264enc","h264parse","d3d11h264dec"}){
  auto*factory=gst_element_factory_find(name);if(!factory){fprintf(stderr,"SKIP: required decoder fixture element %s unavailable\n",name);return false;}gst_object_unref(factory);
 }
 return true;
#else
 fprintf(stderr,"SKIP: the hardware-decoder relay (NVDEC to DMA-BUF) is phase 5\n");return false;
#endif
}

static GSocketListener* listener(guint16*port){
 auto*l=g_socket_listener_new();auto*ip=g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
 auto*address=g_inet_socket_address_new(ip,0);GSocketAddress*effective=nullptr;
 CHECK(g_socket_listener_add_address(l,address,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr));
 *port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(effective);g_object_unref(address);g_object_unref(ip);return l;
}
struct Endpoint {
 FlGstSession*session=nullptr;GstElement*pipe=nullptr,*port=nullptr;
 Endpoint(unsigned hop,bool producer,guint16 number,GSocketListener*listen=nullptr,bool decoder=false){
  flSessionConfig cfg{};cfg.version=2;cfg.clockId.bytes[0]=2;cfg.sessionId.bytes[0]=hop+1;cfg.generation=1;cfg.epochNs=5000000;
  cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
  cfg.video={FL_FORMAT_RGBA8,64,32,decoder?30u:1000u,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=5000;
  flSessionCapabilities offer{};offer.version=2;offer.videoCount=offer.audioCount=1;offer.video[0]=cfg.video;offer.audio[0]=cfg.audio;
  session=fl_gst_session_new(3);CHECK(fl_gst_session_configure(session,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_register_track(session,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
  guint8 secret[32]{};secret[0]=73+hop;auto*key=g_bytes_new(secret,32);
  for(unsigned kind=0;kind<3;++kind){
   GSocketConnection*c=nullptr;
   if(listen)c=g_socket_listener_accept(listen,nullptr,nullptr,nullptr);
   else{auto*client=g_socket_client_new();g_socket_client_set_timeout(client,5);c=g_socket_client_connect_to_host(client,"127.0.0.1",number,nullptr,nullptr);g_object_unref(client);}
   CHECK(c);auto policy=nativeAvPolicy(1,true,decoder);
   if(kind==2)CHECK(fl_gst_session_authenticate_native_memory(session,c,producer,key,&policy,5000,nullptr,nullptr));
   else{auto fn=kind?fl_gst_session_authenticate_event_stream:fl_gst_session_authenticate_audio_stream;CHECK(fn(session,c,producer,key,5000,nullptr,nullptr));}
   g_object_unref(c);
  }
  g_bytes_unref(key);
  if(!producer){flClockMapping m{};m.sourceClockId=cfg.clockId;m.targetClockId.bytes[0]=hop?8:7;m.sourceAnchorNs=cfg.epochNs;m.targetAnchorNs=hop?1000000:0;CHECK(fl_gst_session_set_mapping(session,1,&m,0)==FL_GST_SESSION_READY);}
  GError*error=nullptr;
  pipe=gst_parse_launch(producer?"appsrc name=port is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! framelinkaudiosink tracks=av":"framelinkaudiosrc tracks=av ! appsink name=port max-buffers=2 sync=false async=false enable-last-sample=false",&error);
  CHECK(pipe&&!error);port=gst_bin_get_by_name(GST_BIN(pipe),"port");CHECK(port);auto*context=fl_gst_session_create_context(session);gst_element_set_context(pipe,context);gst_context_unref(context);
 }
 void start(){CHECK(fl_gst_session_track_ready(session,FL_GST_TRACK_VIDEO,1)==FL_GST_SESSION_READY);CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);}
 ~Endpoint(){CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(port);gst_object_unref(pipe);g_object_unref(session);}
};
static void logTo(const std::string&path){CHECK(freopen(path.c_str(),"w",stderr));setvbuf(stderr,nullptr,_IONBF,0);}

static int producer(const std::string&name,guint16 port,Signals&sig,bool empty,bool cancelled=false){
 logTo(name+".producer.log");fprintf(stderr,"producer start\n");
 Endpoint e(0,true,port);fprintf(stderr,"producer authenticated\n");flChannel*writer=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);
 sig[Attached].signal();e.start();
 flBuffer slots[2]{};for(auto&slot:slots)CHECK(flAcquireBuffer(writer,&slot,1000)==FL_OK);unsigned count=empty?0:Frames;
 for(unsigned n=0;n<count;++n){
  auto&input=slots[n%2];flMappedImage pixels{};CHECK(flImageMapPlanes(input.image,&pixels)==FL_OK);
  for(unsigned y=0;y<32;++y)memset(static_cast<char*>(pixels.plane[0].pixels)+y*pixels.plane[0].stride,n+1,256);flImageUnmap(input.image);
  const uint64_t pts=37000000+n*1000000;flFrameMetadata metadata{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(pts-100),1000000,1,{}};
  CHECK(flSetFrameMetadata(writer,&input,&metadata)==FL_OK);CHECK(flSubmit(writer,&input,pts)==FL_OK);
  auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(pcm,0,n+31,192);GST_BUFFER_PTS(pcm)=n*1000000;GST_BUFFER_DURATION(pcm)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(e.port),pcm)==GST_FLOW_OK);
  sig[Held].wait();flBuffer blocked{};CHECK(flAcquireBuffer(writer,&blocked,0)==FL_TIMEOUT);sig[Checked].signal();
  flBuffer released{};CHECK(flAcquireBuffer(writer,&released,5000)==FL_OK&&same(input.image,released.image));input=released;
  if(cancelled)break;
  for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent event{kind,n+1,pts,kind==FL_GST_VIDEO_GAP?123u:0u};CHECK(fl_gst_session_send_video_event(e.session,1,&event)==FL_GST_SESSION_READY);}
 }
 for(auto&slot:slots)CHECK(flCancelBuffer(writer,&slot)==FL_OK);
 if(!cancelled){
  FlGstVideoEvent end{FL_GST_VIDEO_END,count,count?37000000+(count-1)*1000000:0,0};CHECK(fl_gst_session_send_video_event(e.session,1,&end)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_video_eos(e.session,1,count,count?gint64(end.ptsNs):-1)==FL_GST_SESSION_READY);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(e.port))==GST_FLOW_OK);
 }
 sig[Done].wait();
 flClose(writer);return 0;
}
// Actual GstMemory reservations in the source process: neither downstream
// completion nor dropping only the GstBuffer permits reuse of retained memory.
static int reservedProducer(const std::string&name,guint16 port,Signals&sig,bool decoder=false,bool cancelled=false){
 logTo(name+".producer.log");
 Endpoint e(0,true,port,nullptr,decoder);Decoder decoded;flChannel*writer=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);
 auto owner=std::make_shared<relaytest::ReservationOwner>(writer);CHECK(owner->valid());
 sig[Attached].signal();e.start();
 GstBuffer*slots[2]={owner->acquire(1000),owner->acquire(1000)};CHECK(slots[0]&&slots[1]&&!owner->acquire(0));
 for(unsigned n=0;n<Frames;++n){
  auto*&buffer=slots[n%2];auto*memory=gst_buffer_peek_memory(buffer,0);
  auto image=relaytest::reservationOf(buffer)->buffer.image;
  uint64_t hash=0;if(decoder)hash=decoded.fill(buffer,n);else relaytest::uploadReservation(*owner,buffer,static_cast<unsigned char>(n+1)); // explicit source upload only
  const uint64_t pts=37000000+frameTime(n,decoder);flFrameMetadata metadata{1,decoder?FL_FRAME_HAS_DURATION:FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,decoder?0:int64_t(pts-100),frameDuration(n,decoder),1,{}};
  CHECK(owner->submit(buffer,pts,&metadata)==FL_OK);CHECK(owner->submit(buffer,pts,&metadata)==FL_INVALID);
  const unsigned pcmBytes=decoder?6400:192;auto*pcm=gst_buffer_new_allocate(nullptr,pcmBytes,nullptr);gst_buffer_memset(pcm,0,n+31,pcmBytes);
  if(decoder)CHECK(gst_buffer_fill(pcm,0,&hash,sizeof hash)==sizeof hash);
  GST_BUFFER_PTS(pcm)=frameTime(n,decoder);GST_BUFFER_DURATION(pcm)=decoder?gst_util_uint64_scale(1600,GST_SECOND,48000):1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(e.port),pcm)==GST_FLOW_OK);
  sig[Held].wait();CHECK(!owner->acquire(0));sig[Checked].signal();
  sig[Drained].wait();CHECK(!owner->acquire(0)); // every remote reader released, local GstMemory still held
  auto*retained=gst_memory_ref(memory);gst_buffer_unref(buffer);buffer=nullptr;CHECK(!owner->acquire(0));
  gst_memory_unref(retained);buffer=owner->acquire(1000);CHECK(buffer);
  CHECK(same(image,relaytest::reservationOf(buffer)->buffer.image));
  if(cancelled)break;
  for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent event{kind,n+1,pts,kind==FL_GST_VIDEO_GAP?123u:0u};CHECK(fl_gst_session_send_video_event(e.session,1,&event)==FL_GST_SESSION_READY);}
 }
 for(auto*buffer:slots)gst_buffer_unref(buffer); // cancel the two unused reservations
 if(!cancelled){
  FlGstVideoEvent end{FL_GST_VIDEO_END,Frames,37000000+frameTime(Frames-1,decoder),0};CHECK(fl_gst_session_send_video_event(e.session,1,&end)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_video_eos(e.session,1,Frames,gint64(end.ptsNs))==FL_GST_SESSION_READY);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(e.port))==GST_FLOW_OK);
 }
 sig[Done].wait();
 owner.reset();
 fprintf(stderr,"source GstMemory reservations: remote drain plus final local reference required; duplicate submit rejected; exact allocation reused\n");return 0;
}
static int relay(const std::string&name,guint16 port,relaytest::SharedImages&images,relaytest::Mailbox&notify,Signals&sig,bool empty,bool decoder=false,unsigned fault=0){
 logTo(name+".relay.log");fprintf(stderr,"relay start\n");
 flChannel*up=images.adopt(name+".up",false,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(decoder?FL_GPU_RENDER:0)),*down=nullptr;
 CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);
 Endpoint output(1,true,port,nullptr,decoder);fprintf(stderr,"relay downstream authenticated\n");guint16 upstreamPort=0;auto*l=listener(&upstreamPort);
 notify.put(upstreamPort);notify.closeWriter();
 Endpoint input(0,false,0,l,decoder);fprintf(stderr,"relay upstream authenticated\n");g_object_unref(l);sig[Attached].wait();
 {
  flsample::SessionNativeRelay video(input.session,output.session,up,down,same);CHECK(video.initialize()==FL_OK);
  flsample::SessionPcmRelay pcm(input.session,output.session,GST_APP_SINK(input.port),GST_APP_SRC(output.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);
  output.start();input.start();unsigned count=empty?0:Frames;
  for(unsigned n=0;n<count;++n){
   flFrame frame{};CHECK(flAcquireFrame(up,&frame,5000)==FL_OK);auto forwarded=video.forward(frame);fprintf(stderr,"frame %u forwarded=%d session=%d\n",n,int(forwarded),int(video.sessionStatus()));CHECK(forwarded==FL_OK);
   auto deadline=g_get_monotonic_time()+5000000;FlGstSessionResult r;
   do{r=pcm.forward(100);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<deadline);}while(r==FL_GST_SESSION_TIMEOUT);
   if(fault){
    const auto began=g_get_monotonic_time();auto result=video.forwardEvent(2000);
    const auto elapsed=g_get_monotonic_time()-began;
    fprintf(stderr,"%s: event result=%d elapsed_us=%lld pending=%u\n",fault==1?"producer death":(fault==3?"downstream death":"downstream cancellation"),int(result),(long long)elapsed,video.pending());
    CHECK(result!=FL_GST_SESSION_READY&&result!=FL_GST_SESSION_TIMEOUT&&elapsed<1500000);
    CHECK(!video.videoEnded()&&video.count()==1&&video.pending()==1);
    if(fault==3){
     // Peer death is NOT consumed-fence completion. Abandon this graph, never
     // synthesize release or recycle an allocation from the dead generation.
     CHECK(video.finish()==FL_DISCONNECTED&&video.pending()==1);
     CHECK(video.collect()==FL_INVALID&&video.pending()==1);
    }else CHECK(video.finish()==FL_BUSY&&video.pending()==1);
    CHECK(video.forward(frame)==FL_INVALID);
    CHECK(pcm.forward(0)!=FL_GST_SESSION_READY);
    CHECK(pcm.blockCount()==1&&pcm.frameCount()==48&&pcm.byteCount()==192);
    sig[FaultChecked].signal();
   }
   if(fault==3)break; // supervisor terminates upstream before graph teardown
   while(video.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(video.collect()==FL_OK);g_usleep(100);}
   if(fault){
    CHECK(video.finish()==FL_OK&&!video.videoEnded());
    if(fault==2)sig[Drained].signal();
    break;
   }
   sig[Drained].signal();
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY);CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY);
  }
  if(!fault){
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY&&video.videoEnded());CHECK(video.finish()==FL_OK);
   avTerminal(input.pipe,GST_MESSAGE_EOS);CHECK(pcm.finish(video,5000)==FL_GST_SESSION_READY);
   CHECK(pcm.frameCount()==count*(decoder?1600:48)&&pcm.byteCount()==count*(decoder?6400:192)&&pcm.blockCount()==count);
  }
  sig[RelayDone].wait();
 }
 flClose(down);flClose(up);return 0;
}
static std::string graphName(const char*kind,const std::string&suffix){
#ifdef _WIN32
 const auto pid=unsigned(GetCurrentProcessId());
#else
 const auto pid=unsigned(getpid());
#endif
 return std::string("gst-relay-")+kind+"-"+std::to_string(pid)+"-"+suffix;
}
static std::vector<relaytest::NativeHandle> inherit(const relaytest::SharedImages&images,const relaytest::Mailbox&notify){
 auto handles=images.inherited();handles.push_back(notify.inherited());return handles;
}
static void run(const std::string&plugin,bool empty,bool reserved=false,bool decoder=false,unsigned fault=0){
 relaytest::SharedImages images;Signals sig;
 auto name=graphName("process",std::to_string(empty)+"-"+std::to_string(reserved)+"-"+std::to_string(decoder)+"-"+std::to_string(fault));
 flChannel*down=images.adopt(name+".down",true,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(decoder?FL_GPU_RENDER:0));
 relaytest::Mailbox notify;guint16 port=0;auto*l=listener(&port);
 relaytest::Child middle({plugin,"--relay-process-child",name,std::to_string(port),images.arg(),notify.arg(),sig.arg(),
  std::to_string(empty),std::to_string(decoder),std::to_string(fault)},sig.all(),inherit(images,notify));
 notify.closeWriter();
 {
  Endpoint receiver(1,false,0,l,decoder);g_object_unref(l);const guint16 upstreamPort=notify.take();
  relaytest::Child first({plugin,decoder?"--relay-decoder-producer":(reserved?"--relay-reserved-producer":"--relay-process-producer"),
   name,std::to_string(upstreamPort),sig.arg(),std::to_string(empty),std::to_string(fault==2)},sig.all());
  receiver.start();
  unsigned count=empty?0:Frames;unsigned seen=0;
  for(unsigned n=0;n<count;++n){
   flFrame frame{};CHECK(flAcquireFrame(down,&frame,5000)==FL_OK&&frame.ptsNs==37000000+frameTime(n,decoder));
   flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&frame,&m)==FL_OK&&(decoder?!(m.flags&FL_FRAME_HAS_DTS):m.dtsNs==int64_t(frame.ptsNs)-100)&&m.durationNs==frameDuration(n,decoder)&&m.streamGeneration==1);
   // The received storage must be one of the original allocations.
   const int which=images.indexOf(frame.image);CHECK(which>=0);seen|=1u<<which;
   uint64_t lease=0;CHECK(flLeaseFrame(down,&frame,&lease)==FL_OK);sig[Held].signal();sig[Checked].wait();
   auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),5*GST_SECOND);CHECK(sample);auto*audio=gst_sample_get_buffer(sample);std::vector<guint8> pcm(decoder?6400:192);gint64 mapped=0;
   CHECK(fl_gst_session_map_running_time(receiver.session,1,frame.ptsNs,TRUE,&mapped)==FL_GST_SESSION_READY);
   CHECK(GST_BUFFER_PTS(audio)==1000000+frameTime(n,decoder)&&mapped-gint64(GST_BUFFER_PTS(audio))==37000000&&GST_BUFFER_DURATION(audio)==(decoder?gst_util_uint64_scale(1600,GST_SECOND,48000):1000000));
   CHECK(gst_buffer_get_size(audio)==pcm.size()&&gst_buffer_extract(audio,0,pcm.data(),pcm.size())==pcm.size());uint64_t expectedHash=0;if(decoder)memcpy(&expectedHash,pcm.data(),sizeof expectedHash);for(size_t i=decoder?8:0;i<pcm.size();++i)CHECK(pcm[i]==n+31);gst_sample_unref(sample);
   if(fault==2){
    // The actual final reader cancels its authenticated session in another
    // process from the relay. Keep its native lease live throughout propagation.
    fl_gst_session_cancel(receiver.session);sig[FaultChecked].wait();
   }else if(fault==1){
    first.terminate();sig[FaultChecked].wait();
   }
   flMappedImage pixels{};CHECK(flImageMapPlanes(frame.image,&pixels)==FL_OK);if(decoder)CHECK(pixelHash(static_cast<unsigned char*>(pixels.plane[0].pixels),pixels.plane[0].stride)==expectedHash);else for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<unsigned char*>(pixels.plane[0].pixels)[y*pixels.plane[0].stride+x]==n+1);flImageUnmap(frame.image);
   CHECK(flCompleteReadLease(down,lease)==FL_OK&&flCollectReadLeases(down)==FL_OK);
   if(fault)break;
   for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent e{};CHECK(fl_gst_session_receive_video_event(receiver.session,1,5000,&e)==FL_GST_SESSION_READY);CHECK(e.kind==kind&&e.afterFrames==n+1&&e.ptsNs==frame.ptsNs&&e.durationNs==(kind==FL_GST_VIDEO_GAP?123:0));}
  }
  if(fault){
   FlGstVideoEvent event{};const auto began=g_get_monotonic_time();
   auto result=fl_gst_session_receive_video_event(receiver.session,1,2000,&event);
   CHECK(result!=FL_GST_SESSION_READY&&result!=FL_GST_SESSION_TIMEOUT&&g_get_monotonic_time()-began<1500000);
   guint64 count=99;gint64 pts=-2;
   CHECK(fl_gst_session_wait_video_eos(receiver.session,1,0,&count,&pts)!=FL_GST_SESSION_READY);
   sig[RelayDone].signal();
   if(fault==2){sig[Done].signal();first.join();}
   middle.join();
   printf("%s (%s): explicit failure, no synthetic END/EOS, held pixels intact, no premature lease completion, bounded native drain\n",fault==1?"producer death":(fault==3?"downstream death":"downstream cancellation"),reserved?"GstMemory":"raw");
  }else{
   CHECK(empty||seen==3);
   FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(receiver.session,1,5000,&end)==FL_GST_SESSION_READY);CHECK(end.kind==FL_GST_VIDEO_END&&end.afterFrames==count&&end.ptsNs==(count?37000000+frameTime(count-1,decoder):0));
   avTerminal(receiver.pipe,GST_MESSAGE_EOS);guint64 finalCount=99;gint64 last=-2;
   CHECK(fl_gst_session_wait_video_eos(receiver.session,1,5000,&finalCount,&last)==FL_GST_SESSION_READY&&finalCount==count&&last==(count?gint64(end.ptsNs):-1));
   CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),0));flFrame extra{};CHECK(flAcquireFrame(down,&extra,0)==FL_TIMEOUT);
   sig[Done].signal();sig[RelayDone].signal();first.join();middle.join();
   printf("three-process coordinated relay: %u same-allocation video frames, %u exact PCM frames; nonempty: both reversed slots / 37ms offset / held-reader exclusion; ordered GAP/barrier/END and authenticated terminal passed\n",count,count*(decoder?1600:48));
  }
 }
 flClose(down);
}

// Separate final reader for actual process-death qualification. The supervisor
// owns the source/relay lifetimes, so killing this process cannot kill the
// other actors or masquerade as graceful cancellation.
static int deathReader(const std::string&name,relaytest::SharedImages&images,relaytest::Mailbox&notify,Signals&sig){
 logTo(name+".reader.log");
 flChannel*down=images.adopt(name+".down",true,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA);
 guint16 port=0;auto*l=listener(&port);notify.put(port);notify.closeWriter();
 Endpoint receiver(1,false,0,l);g_object_unref(l);receiver.start();
 flFrame frame{};CHECK(flAcquireFrame(down,&frame,5000)==FL_OK&&frame.ptsNs==37000000);
 flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&frame,&m)==FL_OK&&m.dtsNs==36999900&&m.durationNs==1000000&&m.streamGeneration==1);
 CHECK(images.indexOf(frame.image)>=0);
 uint64_t lease=0;CHECK(flLeaseFrame(down,&frame,&lease)==FL_OK);
 sig[Held].signal();sig[Checked].wait();
 auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),5*GST_SECOND);CHECK(sample);
 auto*audio=gst_sample_get_buffer(sample);guint8 pcm[192]{};gint64 mapped=0;
 CHECK(fl_gst_session_map_running_time(receiver.session,1,frame.ptsNs,TRUE,&mapped)==FL_GST_SESSION_READY);
 CHECK(GST_BUFFER_PTS(audio)==1000000&&mapped-gint64(GST_BUFFER_PTS(audio))==37000000&&GST_BUFFER_DURATION(audio)==1000000);
 CHECK(gst_buffer_get_size(audio)==192&&gst_buffer_extract(audio,0,pcm,192)==192);for(auto b:pcm)CHECK(b==31);gst_sample_unref(sample);
 flMappedImage pixels{};CHECK(flImageMapPlanes(frame.image,&pixels)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<unsigned char*>(pixels.plane[0].pixels)[y*pixels.plane[0].stride+x]==1);
 flImageUnmap(frame.image);
 fprintf(stderr,"reader verified same allocation, exact pixels/metadata/48 PCM frames, 37ms offset; lease retained; awaiting forced exit\n");
 sig[ReaderReady].signal();
 sig[Never].wait();CHECK(false);return 1;
}
static void downstreamDeath(const std::string&plugin,bool reserved){
 relaytest::SharedImages images;Signals sig;
 auto name=graphName("downstream-death",std::to_string(reserved));
 relaytest::Mailbox readerPort;
 relaytest::Child last({plugin,"--relay-death-reader",name,images.arg(),readerPort.arg(),sig.arg()},sig.all(),inherit(images,readerPort));
 readerPort.closeWriter();const guint16 port=readerPort.take();
 relaytest::Mailbox relayPort;
 relaytest::Child middle({plugin,"--relay-process-child",name,std::to_string(port),images.arg(),relayPort.arg(),sig.arg(),"0","0","3"},
  sig.all(),inherit(images,relayPort));
 relayPort.closeWriter();const guint16 upstream=relayPort.take();
 relaytest::Child first({plugin,reserved?"--relay-reserved-producer":"--relay-process-producer",name,std::to_string(upstream),sig.arg(),"0","0"},sig.all());
 sig[ReaderReady].wait();last.terminate();
 sig[FaultChecked].wait();
 // Source is still fenced out of the dead reader's allocation. Stop the
 // complete old graph before destroying channels/handles; never fake drain.
 CHECK(first.running());first.terminate();
 sig[RelayDone].signal();middle.join();
 printf("downstream process death (%s): reader killed, exact pixels/PCM/37ms offset, bounded failure, lease retained, no synthetic drain; source still fenced then killed, relay exit0; old graph discarded\n",reserved?"GstMemory":"raw");
}

// Child entry points (argv[1] is the plugin, argv[2] the mode).
static bool child(int argc,char**argv,int*code){
 const std::string mode=argv[2];
 if(argc==11&&mode=="--relay-process-child"){
  relaytest::SharedImages images(argv[5]);relaytest::Mailbox notify(argv[6]);Signals sig(argv[7]);
  *code=relay(argv[3],guint16(atoi(argv[4])),images,notify,sig,atoi(argv[8])!=0,atoi(argv[9])!=0,unsigned(atoi(argv[10])));return true;
 }
 if(argc==8&&(mode=="--relay-process-producer"||mode=="--relay-reserved-producer"||mode=="--relay-decoder-producer")){
  Signals sig(argv[5]);const guint16 port=guint16(atoi(argv[4]));const bool empty=atoi(argv[6])!=0,cancelled=atoi(argv[7])!=0;
  if(mode=="--relay-process-producer")*code=producer(argv[3],port,sig,empty,cancelled);
  else *code=reservedProducer(argv[3],port,sig,mode=="--relay-decoder-producer",cancelled);
  return true;
 }
 if(argc==7&&mode=="--relay-death-reader"){
  relaytest::SharedImages images(argv[4]);relaytest::Mailbox notify(argv[5]);Signals sig(argv[6]);
  *code=deathReader(argv[3],images,notify,sig);return true;
 }
 return false;
}
}

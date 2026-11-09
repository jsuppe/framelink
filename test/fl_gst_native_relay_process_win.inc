// Three independent processes, broker-provisioned test credentials and inherited
// allocation handles. Not a production broker, discovery or performance test.
#include <vector>
namespace relayprocess {
static HANDLE childJob=nullptr;
static constexpr unsigned Frames=16;
static HANDLE signal(const std::string& name){auto h=CreateEventA(nullptr,FALSE,FALSE,name.c_str());CHECK(h);return h;}
static void wait(HANDLE h){CHECK(WaitForSingleObject(h,10000)==WAIT_OBJECT_0);}
static PROCESS_INFORMATION spawn(const std::string& args,const std::vector<HANDLE>& handles={}){
 char exe[MAX_PATH]{};CHECK(GetModuleFileNameA(nullptr,exe,MAX_PATH));std::string command="\""+std::string(exe)+"\" "+args;
 STARTUPINFOEXA startup{};startup.StartupInfo.cb=handles.empty()?sizeof(STARTUPINFOA):sizeof(startup);SIZE_T bytes=0;std::vector<unsigned char> storage;
 if(!handles.empty()){
  InitializeProcThreadAttributeList(nullptr,1,0,&bytes);storage.resize(bytes);startup.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();CHECK(InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes));
  for(auto h:handles)CHECK(SetHandleInformation(h,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT));
  CHECK(UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,(void*)handles.data(),handles.size()*sizeof(HANDLE),nullptr,nullptr));
 }
 if(!childJob){childJob=CreateJobObjectA(nullptr,nullptr);CHECK(childJob);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;CHECK(SetInformationJobObject(childJob,JobObjectExtendedLimitInformation,&limits,sizeof limits));}
 PROCESS_INFORMATION p{};CHECK(CreateProcessA(nullptr,command.data(),nullptr,nullptr,!handles.empty(),CREATE_NO_WINDOW|CREATE_SUSPENDED|(handles.empty()?0:EXTENDED_STARTUPINFO_PRESENT),nullptr,nullptr,&startup.StartupInfo,&p));
 CHECK(AssignProcessToJobObject(childJob,p.hProcess));CHECK(ResumeThread(p.hThread)!=DWORD(-1));
 if(!handles.empty()){for(auto h:handles)CHECK(SetHandleInformation(h,HANDLE_FLAG_INHERIT,0));DeleteProcThreadAttributeList(startup.lpAttributeList);}return p;
}
static void join(PROCESS_INFORMATION&p){wait(p.hProcess);DWORD code=1;CHECK(GetExitCodeProcess(p.hProcess,&code)&&code==0);CloseHandle(p.hThread);CloseHandle(p.hProcess);}
static GSocketListener* listener(guint16*port){
 auto*l=g_socket_listener_new();auto*ip=g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
 auto*address=g_inet_socket_address_new(ip,0);GSocketAddress*effective=nullptr;
 CHECK(g_socket_listener_add_address(l,address,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr));
 *port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(effective);g_object_unref(address);g_object_unref(ip);return l;
}
struct Endpoint {
 FlGstSession*session=nullptr;GstElement*pipe=nullptr,*port=nullptr;
 Endpoint(unsigned hop,bool producer,guint16 number,GSocketListener*listen=nullptr){
  flSessionConfig cfg{};cfg.version=2;cfg.clockId.bytes[0]=2;cfg.sessionId.bytes[0]=hop+1;cfg.generation=1;cfg.epochNs=5000000;
  cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
  cfg.video={FL_FORMAT_RGBA8,64,32,1000,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=5000;
  flSessionCapabilities offer{};offer.version=2;offer.videoCount=offer.audioCount=1;offer.video[0]=cfg.video;offer.audio[0]=cfg.audio;
  session=fl_gst_session_new(3);CHECK(fl_gst_session_configure(session,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
  CHECK(fl_gst_session_register_track(session,FL_GST_TRACK_VIDEO)==FL_GST_SESSION_READY);
  guint8 secret[32]{};secret[0]=73+hop;auto*key=g_bytes_new(secret,32);
  for(unsigned kind=0;kind<3;++kind){
   GSocketConnection*c=nullptr;
   if(listen)c=g_socket_listener_accept(listen,nullptr,nullptr,nullptr);
   else{auto*client=g_socket_client_new();g_socket_client_set_timeout(client,5);c=g_socket_client_connect_to_host(client,"127.0.0.1",number,nullptr,nullptr);g_object_unref(client);}
   CHECK(c);auto policy=nativeAvPolicy(1,true);
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
static int producer(const std::string&name,guint16 port,bool empty){
 freopen((name+".producer.log").c_str(),"w",stderr);setvbuf(stderr,nullptr,_IONBF,0);fprintf(stderr,"producer start\n");
 Endpoint e(0,true,port);fprintf(stderr,"producer authenticated\n");flChannel*writer=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&writer)==FL_OK);
 auto attached=signal(name+".attached");CHECK(SetEvent(attached));CloseHandle(attached);
 auto held=signal(name+".held"),checked=signal(name+".checked"),done=signal(name+".done");e.start();
 flBuffer slots[2]{};for(auto&slot:slots)CHECK(flAcquireBuffer(writer,&slot,1000)==FL_OK);unsigned count=empty?0:Frames;
 for(unsigned n=0;n<count;++n){
  auto&input=slots[n%2];flMappedImage pixels{};CHECK(flImageMapPlanes(input.image,&pixels)==FL_OK);
  for(unsigned y=0;y<32;++y)memset(static_cast<char*>(pixels.plane[0].pixels)+y*pixels.plane[0].stride,n+1,256);flImageUnmap(input.image);
  const uint64_t pts=37000000+n*1000000;flFrameMetadata metadata{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,int64_t(pts-100),1000000,1,{}};
  CHECK(flSetFrameMetadata(writer,&input,&metadata)==FL_OK);CHECK(flSubmit(writer,&input,pts)==FL_OK);
  auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(pcm,0,n+31,192);GST_BUFFER_PTS(pcm)=n*1000000;GST_BUFFER_DURATION(pcm)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(e.port),pcm)==GST_FLOW_OK);
  wait(held);flBuffer blocked{};CHECK(flAcquireBuffer(writer,&blocked,0)==FL_TIMEOUT);CHECK(SetEvent(checked));
  flBuffer released{};CHECK(flAcquireBuffer(writer,&released,5000)==FL_OK&&relaySame(input.image,released.image));input=released;
  for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent event{kind,n+1,pts,kind==FL_GST_VIDEO_GAP?123u:0u};CHECK(fl_gst_session_send_video_event(e.session,1,&event)==FL_GST_SESSION_READY);}
 }
 for(auto&slot:slots)CHECK(flCancelBuffer(writer,&slot)==FL_OK);
 FlGstVideoEvent end{FL_GST_VIDEO_END,count,count?37000000+(count-1)*1000000:0,0};CHECK(fl_gst_session_send_video_event(e.session,1,&end)==FL_GST_SESSION_READY);
 CHECK(fl_gst_session_video_eos(e.session,1,count,count?gint64(end.ptsNs):-1)==FL_GST_SESSION_READY);
 CHECK(gst_app_src_end_of_stream(GST_APP_SRC(e.port))==GST_FLOW_OK);wait(done);
 flClose(writer);for(auto h:{held,checked,done})CloseHandle(h);return 0;
}
static int relay(const std::string&name,guint16 port,HANDLE h0,HANDLE h1,HANDLE notify,bool empty){
 freopen((name+".relay.log").c_str(),"w",stderr);setvbuf(stderr,nullptr,_IONBF,0);fprintf(stderr,"relay start\n");
 void*handles[2]={h0,h1};flChannel*up=nullptr,*down=nullptr;
 CHECK(flCreateChannelFromD3D11Handles((name+".up").c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,nullptr,handles,&up)==FL_OK);
 CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);
 Endpoint output(1,true,port);fprintf(stderr,"relay downstream authenticated\n");guint16 upstreamPort=0;auto*l=listener(&upstreamPort);DWORD written=0;
 CHECK(WriteFile(notify,&upstreamPort,sizeof upstreamPort,&written,nullptr)&&written==sizeof upstreamPort);CloseHandle(notify);
 auto attached=signal(name+".attached");
 Endpoint input(0,false,0,l);fprintf(stderr,"relay upstream authenticated\n");g_object_unref(l);wait(attached);CloseHandle(attached);
 {
  flsample::SessionNativeRelay video(input.session,output.session,up,down,relaySame);CHECK(video.initialize()==FL_OK);
  flsample::SessionPcmRelay pcm(input.session,output.session,GST_APP_SINK(input.port),GST_APP_SRC(output.port));CHECK(pcm.initialize()==FL_GST_SESSION_READY);
  output.start();input.start();unsigned count=empty?0:Frames;unsigned seen=0;
  for(unsigned n=0;n<count;++n){
   flFrame frame{};CHECK(flAcquireFrame(up,&frame,5000)==FL_OK);auto forwarded=video.forward(frame);fprintf(stderr,"frame %u forwarded=%d session=%d\n",n,int(forwarded),int(video.sessionStatus()));CHECK(forwarded==FL_OK);
   auto deadline=g_get_monotonic_time()+5000000;FlGstSessionResult r;
   do{r=pcm.forward(100);CHECK(r==FL_GST_SESSION_READY||r==FL_GST_SESSION_TIMEOUT);CHECK(g_get_monotonic_time()<deadline);}while(r==FL_GST_SESSION_TIMEOUT);
   while(video.pending()){CHECK(g_get_monotonic_time()<deadline);CHECK(video.collect()==FL_OK);g_usleep(100);}
   CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY);CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY);
  }
  CHECK(video.forwardEvent(5000)==FL_GST_SESSION_READY&&video.videoEnded());CHECK(video.finish()==FL_OK);
  avTerminal(input.pipe,GST_MESSAGE_EOS);CHECK(pcm.finish(video,5000)==FL_GST_SESSION_READY);
  CHECK(pcm.frameCount()==count*48&&pcm.byteCount()==count*192&&pcm.blockCount()==count);
  auto done=signal(name+".relaydone");wait(done);CloseHandle(done);
 }
 flClose(down);flClose(up);CloseHandle(h0);CloseHandle(h1);return 0;
}
static void run(const std::string&plugin,bool empty){
 using Microsoft::WRL::ComPtr;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
 D3D11_TEXTURE2D_DESC d{};d.Width=64;d.Height=32;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
 ComPtr<ID3D11Texture2D> textures[2];HANDLE handles[2]{};
 for(unsigned i=0;i<2;++i){CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&textures[i])));ComPtr<IDXGIResource1> resource;CHECK(SUCCEEDED(textures[i].As(&resource)));CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handles[i])));}
 auto name="gst-relay-process-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(empty);
 auto held=signal(name+".held"),checked=signal(name+".checked"),done=signal(name+".done"),relaydone=signal(name+".relaydone");
 void*reversed[2]={handles[1],handles[0]};flChannel*down=nullptr;CHECK(flCreateChannelFromD3D11Handles((name+".down").c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,nullptr,reversed,&down)==FL_OK);
 HANDLE read=nullptr,write=nullptr;CHECK(CreatePipe(&read,&write,nullptr,0));guint16 port=0;auto*l=listener(&port);
 auto args="\""+plugin+"\" --relay-process-child "+name+" "+std::to_string(port)+" "+std::to_string((uintptr_t)handles[0])+" "+std::to_string((uintptr_t)handles[1])+" "+std::to_string((uintptr_t)write)+" "+std::to_string(empty);
 auto middle=spawn(args,{handles[0],handles[1],write});CloseHandle(write);
 {
  Endpoint receiver(1,false,0,l);g_object_unref(l);guint16 upstreamPort=0;DWORD bytes=0;
  CHECK(ReadFile(read,&upstreamPort,sizeof upstreamPort,&bytes,nullptr)&&bytes==sizeof upstreamPort);CloseHandle(read);
  auto first=spawn("\""+plugin+"\" --relay-process-producer "+name+" "+std::to_string(upstreamPort)+" "+std::to_string(empty));receiver.start();
  unsigned count=empty?0:Frames;unsigned seen=0;
  for(unsigned n=0;n<count;++n){
   flFrame frame{};CHECK(flAcquireFrame(down,&frame,5000)==FL_OK&&frame.ptsNs==37000000+n*1000000);
   flFrameMetadata m{};CHECK(flGetFrameMetadata(down,&frame,&m)==FL_OK&&m.dtsNs==int64_t(frame.ptsNs)-100&&m.durationNs==1000000&&m.streamGeneration==1);
   // The received NT handle must refer to one of the original allocations.
   using Compare=BOOL(WINAPI*)(HANDLE,HANDLE);auto compare=(Compare)GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CompareObjectHandles");CHECK(compare&&(compare(flImageSharedHandle(frame.image),handles[0])||compare(flImageSharedHandle(frame.image),handles[1])));
   seen|=compare(flImageSharedHandle(frame.image),handles[0])?1u:2u;
   uint64_t lease=0;CHECK(flLeaseFrame(down,&frame,&lease)==FL_OK);CHECK(SetEvent(held));wait(checked);
   auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),5*GST_SECOND);CHECK(sample);auto*audio=gst_sample_get_buffer(sample);guint8 pcm[192];gint64 mapped=0;
   CHECK(fl_gst_session_map_running_time(receiver.session,1,frame.ptsNs,TRUE,&mapped)==FL_GST_SESSION_READY);
   CHECK(GST_BUFFER_PTS(audio)==(n+1)*1000000&&mapped-gint64(GST_BUFFER_PTS(audio))==37000000&&GST_BUFFER_DURATION(audio)==1000000);
   CHECK(gst_buffer_get_size(audio)==sizeof pcm&&gst_buffer_extract(audio,0,pcm,sizeof pcm)==sizeof pcm);for(auto b:pcm)CHECK(b==n+31);gst_sample_unref(sample);
   flMappedImage pixels{};CHECK(flImageMapPlanes(frame.image,&pixels)==FL_OK);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(static_cast<unsigned char*>(pixels.plane[0].pixels)[y*pixels.plane[0].stride+x]==n+1);flImageUnmap(frame.image);
   CHECK(flCompleteReadLease(down,lease)==FL_OK&&flCollectReadLeases(down)==FL_OK);
   for(auto kind:{FL_GST_VIDEO_GAP,FL_GST_VIDEO_BARRIER}){FlGstVideoEvent e{};CHECK(fl_gst_session_receive_video_event(receiver.session,1,5000,&e)==FL_GST_SESSION_READY);CHECK(e.kind==kind&&e.afterFrames==n+1&&e.ptsNs==frame.ptsNs&&e.durationNs==(kind==FL_GST_VIDEO_GAP?123:0));}
  }
  CHECK(empty||seen==3);
  FlGstVideoEvent end{};CHECK(fl_gst_session_receive_video_event(receiver.session,1,5000,&end)==FL_GST_SESSION_READY);CHECK(end.kind==FL_GST_VIDEO_END&&end.afterFrames==count&&end.ptsNs==(count?37000000+(count-1)*1000000:0));
  avTerminal(receiver.pipe,GST_MESSAGE_EOS);guint64 finalCount=99;gint64 last=-2;
  CHECK(fl_gst_session_wait_video_eos(receiver.session,1,5000,&finalCount,&last)==FL_GST_SESSION_READY&&finalCount==count&&last==(count?gint64(end.ptsNs):-1));
  CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(receiver.port),0));flFrame extra{};CHECK(flAcquireFrame(down,&extra,0)==FL_TIMEOUT);
  CHECK(SetEvent(done)&&SetEvent(relaydone));join(first);join(middle);
  printf("three-process coordinated relay: %u same-allocation video frames, %u exact PCM frames; nonempty: both reversed slots / 37ms offset / held-reader exclusion; ordered GAP/barrier/END and authenticated terminal passed\n",count,count*48);
 }
 flClose(down);for(auto h:{handles[0],handles[1],held,checked,done,relaydone})CloseHandle(h);
}
}

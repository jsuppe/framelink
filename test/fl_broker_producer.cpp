#include "av_playback.h"
#include "av_auth.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <utility>
#include <stdexcept>

#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(0)
// Real HMAC, negotiation and GDP on delegated Unix streams; native video still
// uses the normal local ring. This is not cross-UID Android A/V qualification.
struct Pair {
  int fd[2]{-1,-1};
  Pair(){CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fd));}
  ~Pair(){for(int f:fd)if(f>=0)close(f);}
};
void release(GSocketConnection* c) {
  g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);
}
void bounds(const char* plugin) {
  av::ProducerOptions p;p.plugin=plugin;std::string error;
  CHECK(!av::runBrokerProducer(p,error) && error=="incomplete producer provider");
  unsigned resolves=0;
  av::SessionProvider provider;
  provider.resolve=[&](auto&,unsigned,auto&){++resolves;return false;};
  provider.connect=[](auto&,char,unsigned,auto&)->GSocketConnection*{return nullptr;};
  p.sessionProvider=&provider;
  for(auto edit:{0,1,2,3,4,5,6,7,8,9,10,11,12,13}) {
    auto invalid=p;
    switch(edit) {
      case 0:invalid.frames=100001;break;case 1:invalid.frames=0;break;
      case 2:invalid.timeoutMs=99;break;case 3:invalid.timeoutMs=60001;break;
      case 4:invalid.width=4097;break;case 5:invalid.height=2161;break;
      case 6:invalid.fps=12;break;case 7:invalid.fps=241;break;
      case 8:invalid.rate=7999;break;case 9:invalid.channels=3;break;
      case 10:invalid.format="NV12";invalid.width=159;break;
      case 11:invalid.mediaRunMs=300001;break;
      case 12:invalid.mediaRunMs=100;break;
      case 13:invalid.width=1;break;
    }
    CHECK(!av::runBrokerProducer(invalid,error));
  }
  std::atomic<bool> stop{true};p.stop=&stop;
  CHECK(!av::runBrokerProducer(p,error) && error=="producer cancelled");
  CHECK(resolves==0);
  puts("bounds: 14 rejected before resolve; already-cancelled rejected");
}
void session(const char* plugin,const std::string& mode) {
  Pair control,audio;av::Endpoint root{};root.token.fill(0x42);
  int revokeControl=mode=="revoke-startup"?dup(control.fd[0]):-1;
  int revokeAudio=mode=="revoke-startup"?dup(audio.fd[0]):-1;
  const bool adopt=mode=="adopt-video" || mode=="adopt-nv12" || mode=="adopt-refused";
  const bool audioCoordinated=mode.rfind("audio-coordinated",0)==0;
  const bool expected=audioCoordinated || mode=="legacy" || mode=="coordinated-delay" || mode=="finite" || mode=="adopt-video" || mode=="adopt-nv12";
  std::atomic<unsigned> opens{0};
  auto provider=[&](int side) {
    av::SessionProvider p;
    p.resolve=[&,side](av::Endpoint& ep,unsigned,std::string& e){ep=root;if(mode=="wrong-key"&&side==1)ep.token[0]^=1;e.clear();return true;};
    p.connect=[&,side](const av::Endpoint& ep,char role,unsigned timeout,std::string& e) {
      auto* c=av::adoptConnectedStreamFd(std::exchange((role=='C'?control:audio).fd[side],-1),timeout,e);
      if(c && side==1 && role=='A' && (mode=="wrong-audio-key" || mode=="wrong-audio-role")) {
        const bool accepted=av::authenticateClient(c,mode=="wrong-audio-key"?root:ep,mode=="wrong-audio-role"?'C':'A',e);
        release(c);CHECK(!accepted);return static_cast<GSocketConnection*>(nullptr);
      }
      return c;
    };
    if(adopt)p.openVideo=[&,side](const flSessionConfig& cfg,bool producer,unsigned,std::string& e) {
      CHECK(producer==(side==0));++opens;
      if(mode=="adopt-refused"){e="injected video lease refusal";return static_cast<flChannel*>(nullptr);}
      flChannel* channel=nullptr;
      const auto name="broker-adopt-"+std::to_string(getpid());
      auto result=producer?flOpenProducer(name.c_str(),&channel):
        flCreateChannelEx(name.c_str(),cfg.video.width,cfg.video.height,flFormat(cfg.video.format),4,FL_MAP_CPU,&channel);
      CHECK(result==FL_OK);return channel;
    };
    return p;
  };
  auto server=provider(0),client=provider(1);
  std::atomic<bool> producerStop{false},consumerStop{false};
  av::ProducerOptions tx;tx.plugin=plugin;tx.frames=20;tx.timeoutMs=5000;tx.sessionProvider=&server;tx.stop=&producerStop;
  av::PlaybackOptions rx;rx.plugin=plugin;rx.audioSink="fakesink";rx.timeoutMs=5000;rx.recoveryAttempts=0;rx.sessionProvider=&client;
  if(mode!="legacy")rx.clockPolicy=av::PlaybackClockPolicy::ProducerMapped;
  if(audioCoordinated)rx.clockPolicy=av::PlaybackClockPolicy::AudioPreferredCoordinated;
  if(mode=="adopt-nv12")tx.format=rx.format="NV12";
  if(mode=="finite" || mode=="audio-coordinated-finite") {tx.frames=80;tx.timeoutMs=1000;tx.mediaRunMs=1600;rx.mediaRunMs=1600;}
  if(mode=="budget-mismatch"){tx.mediaRunMs=1000;rx.mediaRunMs=2000;}
  if(mode=="media-deadline"){tx.frames=100;tx.timeoutMs=1000;}
  if(mode=="cancel-media")tx.frames=100;
  bool produced=false,received=false;std::string pe,ce;
  unsigned sessions=0,clocks=0,ends=0,frames=0;
  av::PlaybackCallbacks cb;
  cb.onSession=[&](auto&,auto&,auto&){++sessions;};
  cb.onPrepare=[&](unsigned) {
    if(mode=="coordinated-delay" || mode=="audio-coordinated-delay")std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    if(mode=="revoke-startup") {shutdown(revokeControl,SHUT_RDWR);shutdown(revokeAudio,SHUT_RDWR);}
    if(mode=="cancel-startup") {producerStop=true;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
  };
  std::atomic<uint64_t> pcmSamples{0};
  cb.onAudio=[&](GstBuffer* buffer,uint64_t pts){
    CHECK(pts==pcmSamples.load()*GST_SECOND/rx.rate);
    CHECK(gst_buffer_get_size(buffer)%(rx.channels*2)==0);
    pcmSamples+=gst_buffer_get_size(buffer)/(rx.channels*2);
  };
  cb.onVideo=[&](auto*,auto pts,auto&){if(audioCoordinated)CHECK(pts==uint64_t(frames)*GST_SECOND/rx.fps);if(++frames==5 && mode=="cancel-media")producerStop=true;};
  cb.onClock=[&](auto&){++clocks;};cb.onEnd=[&]{++ends;};
  auto start=std::chrono::steady_clock::now();
  long long producerMs=0;
  std::thread producer([&]{produced=av::runBrokerProducer(tx,pe);producerMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();});
  received=av::runPlayback(rx,cb,consumerStop,ce);producer.join();
  if(revokeControl>=0)close(revokeControl);if(revokeAudio>=0)close(revokeAudio);
  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  printf("case=%s producer=%d consumer=%d sessions=%u clocks=%u ends=%u elapsed_ms=%lld producer_ms=%lld producer_error=%s consumer_error=%s\n",mode.c_str(),produced,received,sessions,clocks,ends,(long long)ms,producerMs,pe.c_str(),ce.c_str());
  CHECK(produced==expected && received==expected);
  CHECK(ends==sessions);
  if(expected)CHECK(sessions==1 && clocks==1);
  if(adopt && expected)CHECK(opens==2 && frames==tx.frames);
  if(mode=="adopt-refused")CHECK(sessions==0 && ce=="injected video lease refusal");
  if(mode=="coordinated-delay" || mode=="audio-coordinated-delay")CHECK(ms>=2300);
  if(audioCoordinated)CHECK(frames==tx.frames && pcmSamples==uint64_t(tx.frames)*tx.rate/tx.fps);
  if(mode=="finite" || mode=="audio-coordinated-finite")CHECK(ms>1000);
  if(mode=="media-deadline")CHECK(pe=="producer deadline" && producerMs<2000);
  if(mode=="cancel-startup" || mode=="cancel-media")CHECK(pe=="producer cancelled" && producerMs<2000);
  if(mode=="cancel-startup" || mode=="revoke-startup")CHECK(clocks==0);
  if(mode.rfind("wrong-",0)==0 || mode=="budget-mismatch")CHECK(sessions==0);
}
void silentPeer(const char* plugin) {
  Pair control;av::SessionProvider provider;av::Endpoint root{};
  provider.resolve=[&](auto& ep,unsigned,auto&){ep=root;return true;};
  provider.connect=[&](auto&,char,unsigned timeout,auto& error){return av::adoptConnectedStreamFd(std::exchange(control.fd[0],-1),timeout,error);};
  av::ProducerOptions tx;tx.plugin=plugin;tx.timeoutMs=100;tx.sessionProvider=&provider;
  std::string error;auto start=std::chrono::steady_clock::now();
  CHECK(!av::runBrokerProducer(tx,error) && error=="producer deadline");
  CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
  puts("silent peer: bounded nonfatal producer deadline; process remains alive");
}
int main(int argc,char** argv) {try {
  CHECK(argc==2);bounds(argv[1]);silentPeer(argv[1]);
  for(const char* mode:{"legacy","coordinated-delay","finite","wrong-key","wrong-audio-key","wrong-audio-role","budget-mismatch","revoke-startup","cancel-startup","cancel-media","media-deadline","adopt-video","adopt-nv12","adopt-refused","audio-coordinated","audio-coordinated-delay","audio-coordinated-finite"})session(argv[1],mode);
  puts("broker producer: all real-peer cases passed");return 0;
} catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}

#include "av_playback.h"
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc,char**argv) {
  if(argc!=4) return 2;
  av::PlaybackOptions o; o.plugin=argv[1];o.session=argv[2];o.audioSink="fakesink";
  std::string mode=argv[3]; o.timeoutMs=mode=="bounded"?300:8000;
  o.recoveryAttempts=mode=="bounded"?1:3;
  std::atomic<bool> stop{false},paused{false};o.paused=&paused;
  std::atomic<unsigned> clocks{0},sessions{0},ends{0},retries{0},metrics{0};
  bool invalidMetrics=false;
  av::PlaybackCallbacks cb;
  cb.onMetrics=[&](const av::PlaybackMetrics& m){++metrics; if(m.queueBuffers>4 || m.peakQueueBuffers>4 || m.peakQueueTimeNs>100000000)invalidMetrics=true;};
  cb.onSession=[&](auto&,auto&,auto&){++sessions;};
  cb.onClock=[&](auto&){++clocks;};cb.onEnd=[&]{++ends;};
  cb.onStatus=[&](const std::string&s){printf("status=%s\n",s.c_str());fflush(stdout);if(s.find("session-reconnecting")==0)++retries;};
  std::thread control([&]{
    if(mode!="pause") return;
    while(!clocks && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(stop) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));paused=true;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));paused=false;
  });
  std::string error;auto start=std::chrono::steady_clock::now();
  bool ok=av::runPlayback(o,cb,stop,error);stop=true;control.join();
  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  printf("ok=%d sessions=%u clocks=%u ends=%u retries=%u ms=%lld error=%s\n",ok,sessions.load(),clocks.load(),ends.load(),retries.load(),(long long)ms,error.c_str());
  if(invalidMetrics || (ok && !metrics)) return 1;
  if(mode=="bounded")return !(!ok && retries==1 && ms<3000);
  if(mode=="pause")return !(ok && sessions>=2 && clocks>=2 && ends>=2);
  if(mode=="recover")return !(ok && sessions>=2 && retries>=1);
  return !ok;
}

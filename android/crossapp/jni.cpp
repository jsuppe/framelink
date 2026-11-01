#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <gst/video/video.h>
#include "av_playback.h"
#include "framelink_ahb.h"
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
namespace {
bool diagnostics=false;
bool bufferedDiagnostics=false;
std::vector<std::string> diagnosticLines;
unsigned diagnosticDropped=0;
void diagnostic(const std::string& text) {
 if(bufferedDiagnostics){if(diagnosticLines.size()<2048)diagnosticLines.push_back(text);else ++diagnosticDropped;}
 else __android_log_print(ANDROID_LOG_INFO,"FLCross","%s",text.c_str());
}
void check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
struct Fds {std::vector<int> values;~Fds(){for(int fd:values)if(fd>=0)close(fd);}int take(unsigned i){return std::exchange(values.at(i),-1);}};
struct Window {ANativeWindow* p=nullptr;~Window(){if(p)ANativeWindow_release(p);}};
struct VideoMap {GstVideoFrame f{};bool mapped=false;~VideoMap(){if(mapped)gst_video_frame_unmap(&f);}};
unsigned char clamp(int v){return static_cast<unsigned char>(std::max(0,std::min(255,v)));}
}
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_cross_Native_diagnostics(JNIEnv*,jclass,jint level){
 gst_init(nullptr,nullptr);diagnostics=true;
 bufferedDiagnostics=level<0;
 g_setenv("FRAMELINK_AUDIO_CALIBRATION_TRACE","1",TRUE);
 gst_debug_add_log_function(+[](GstDebugCategory* category,GstDebugLevel level,const gchar*,const gchar*,gint,GObject*,GstDebugMessage* message,gpointer){
  if(level<=gst_debug_category_get_threshold(category))__android_log_print(ANDROID_LOG_INFO,"FLGst","%s: %s",gst_debug_category_get_name(category),gst_debug_message_get(message));
 },nullptr,nullptr);
 gst_debug_set_active(TRUE);
 for(const auto* name:{"audiobasesink","openslessink","openslesringbuffer"})gst_debug_set_threshold_for_name(name,static_cast<GstDebugLevel>(std::max(0,std::min(6,int(level)))));
}
extern "C" JNIEXPORT jintArray JNICALL Java_dev_framelink_cross_Native_pair(JNIEnv* env,jclass){
 int fd[2];if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd))return nullptr;
 auto result=env->NewIntArray(2);if(!result){close(fd[0]);close(fd[1]);return nullptr;}
 env->SetIntArrayRegion(result,0,2,fd);return result;
}
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_cross_Native_shutdown(JNIEnv*,jclass,jint fd){if(fd>=0)shutdown(fd,SHUT_RDWR);}
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_cross_Native_prerollDelay(JNIEnv*,jclass,jint ms){
 if(ms<=0){g_unsetenv("FRAMELINK_TEST_PREROLL_DELAY_MS");return;}
 const auto value=std::to_string(std::min(1000,int(ms)));
 g_setenv("FRAMELINK_TEST_PREROLL_DELAY_MS",value.c_str(),TRUE);
}
extern "C" JNIEXPORT jstring JNICALL Java_dev_framelink_cross_Native_run(JNIEnv* env,jclass,jboolean producer,jintArray descriptors,jbyteArray key,jstring format,jint frames,jint fps,jboolean audible,jboolean producerClock,jobject surface){
 Fds fds;Window window;bool ok=false;uint64_t video=0,posts=0;std::atomic<uint64_t> pcm{0};std::string error;
 try{
  check(descriptors,"missing endpoints");fds.values.resize(env->GetArrayLength(descriptors));
  env->GetIntArrayRegion(descriptors,0,fds.values.size(),fds.values.data());
  check(fds.values.size()==3,"three endpoints required");
  check(key && env->GetArrayLength(key)==32,"invalid lease key");
  check(format && frames>0 && fps>=13 && fps<=120 && frames<=18000,"invalid media bounds");
  av::Endpoint endpoint{};env->GetByteArrayRegion(key,0,32,reinterpret_cast<jbyte*>(endpoint.token.data()));
  const char* text=env->GetStringUTFChars(format,nullptr);check(text,"format string allocation");std::string fmt(text);env->ReleaseStringUTFChars(format,text);
  check(fmt=="BGRA" || fmt=="NV12","invalid media format");
  const auto budget=unsigned((uint64_t(frames)*1000+fps-1)/fps);check(budget<=300000,"media budget too long");
  av::SessionProvider provider;
  provider.resolve=[&](av::Endpoint& ep,unsigned,std::string& e){ep=endpoint;e.clear();return true;};
  provider.connect=[&](const av::Endpoint&,char role,unsigned timeout,std::string& e){return av::adoptConnectedStreamFd(fds.take(role=='C'?0:1),timeout,e);};
  provider.openVideo=[&](const flSessionConfig& cfg,bool isProducer,unsigned timeout,std::string& e){
   flChannel* channel=nullptr;const auto limit=std::min(timeout,2000u);
   auto result=isProducer?flOpenProducerFromSocketFd(fds.take(2),limit,&channel):
    flCreateChannelFromSocketFd(fds.take(2),cfg.video.width,cfg.video.height,flFormat(cfg.video.format),4,FL_MAP_CPU,limit,&channel);
   if(result!=FL_OK)e=flResultString(result);return channel;
  };
  std::atomic<bool> stop{false};
  if(producer){
   av::ProducerOptions p;p.plugin="static";p.format=fmt;p.frames=frames;p.fps=fps;p.mediaRunMs=producerClock?budget:0;p.sessionProvider=&provider;p.stop=&stop;
   ok=av::runBrokerProducer(p,error);
  }else{
   av::PlaybackOptions p;p.plugin="static";p.format=fmt;p.fps=fps;p.mediaRunMs=producerClock?budget:0;p.sessionProvider=&provider;
   p.clockPolicy=producerClock?av::PlaybackClockPolicy::ProducerMapped:av::PlaybackClockPolicy::AudioPreferred;p.audioSink=audible?"openslessink":"fakesink";p.recoveryAttempts=0;
   if(surface)window.p=ANativeWindow_fromSurface(env,surface);
   if(window.p)check(ANativeWindow_setBuffersGeometry(window.p,160,90,WINDOW_FORMAT_RGBA_8888)==0,"surface geometry");
   av::PlaybackCallbacks cb;
   if(diagnostics){
    cb.onClock=[](const av::PlaybackClock& c){diagnostic("clock now="+std::to_string(gst_clock_get_time(c.clock))+" base="+std::to_string(c.baseTimeNs)+" latency="+std::to_string(c.latencyNs));};
    cb.onMetrics=[](const av::PlaybackMetrics& m){diagnostic("audio samples="+std::to_string(m.audioSamples)+" late="+std::to_string(m.lateAudioBlocks)+" queue="+std::to_string(m.queueTimeNs)+" drift="+std::to_string(m.arrivalDriftNs));};
   }
   cb.onAudio=[&](GstBuffer* buffer,uint64_t pts){
    const auto first=pcm.load();check(pts==first*GST_SECOND/48000,"PCM original PTS mismatch");
    GstMapInfo map{};check(gst_buffer_map(buffer,&map,GST_MAP_READ),"PCM map");
    bool valid=map.size%4==0;const auto count=map.size/4;
    for(uint64_t n=0;n<count && valid;++n){const auto expected=int16_t(6000*std::sin(2*3.141592653589793*440*double(first+n)/48000));
     for(unsigned c=0;c<2;++c){auto value=int16_t(GST_READ_UINT16_LE(map.data+n*4+c*2));if(std::abs(int(value)-int(expected))>1)valid=false;}}
    gst_buffer_unmap(buffer,&map);check(valid,"PCM sample mismatch");pcm=first+count;
   };
   cb.onVideo=[&](GstSample* sample,uint64_t pts,const av::PlaybackClock& clock){
    const auto n=video;check(n<unsigned(frames) && pts==n*GST_SECOND/fps,"video original PTS/identity gap");
    GstVideoInfo info{};check(gst_video_info_from_caps(&info,gst_sample_get_caps(sample)),"video caps");
    check(info.width==160 && info.height==90,"video dimensions");
    VideoMap mapped;check(gst_video_frame_map(&mapped.f,&info,gst_sample_get_buffer(sample),GST_MAP_READ),"video map");mapped.mapped=true;
    const bool yuv=GST_VIDEO_INFO_FORMAT(&info)==GST_VIDEO_FORMAT_NV12;
    check(yuv==(fmt=="NV12") && (yuv || GST_VIDEO_INFO_FORMAT(&info)==GST_VIDEO_FORMAT_BGRA),"video format");
    auto* pixels=static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&mapped.f,0));auto stride=GST_VIDEO_FRAME_PLANE_STRIDE(&mapped.f,0);
    for(unsigned y=0;y<90;++y)for(unsigned x=0;x<160;++x){auto* pixel=pixels+y*stride+x*(yuv?1:4);
     check(yuv?pixel[0]==16+(n+x+y)%220:(pixel[0]==uint8_t(n)&&pixel[1]==x&&pixel[2]==y&&pixel[3]==255),"video pixel mismatch");}
    uint8_t* uv=nullptr;int uvStride=0;
    if(yuv){uv=static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&mapped.f,1));uvStride=GST_VIDEO_FRAME_PLANE_STRIDE(&mapped.f,1);
     for(unsigned y=0;y<45;++y)for(unsigned x=0;x<160;x+=2)check(uv[y*uvStride+x]==96+n%64&&uv[y*uvStride+x+1]==160-n%64,"UV pixel mismatch");}
    ++video;
    if(window.p){
     const auto due=clock.baseTimeNs+clock.latencyNs+pts;
     while(gst_clock_get_time(clock.clock)<due)g_usleep(1000);
     ANativeWindow_Buffer out{};if(!ANativeWindow_lock(window.p,&out,nullptr)){
      for(unsigned y=0;y<90;++y)for(unsigned x=0;x<160;++x){auto* d=static_cast<uint8_t*>(out.bits)+(y*out.stride+x)*4;
       if(yuv){int l=int(pixels[y*stride+x])-16,u=int(uv[(y/2)*uvStride+(x&~1)])-128,v=int(uv[(y/2)*uvStride+(x&~1)+1])-128;
        d[0]=clamp((298*l+409*v+128)>>8);d[1]=clamp((298*l-100*u-208*v+128)>>8);d[2]=clamp((298*l+516*u+128)>>8);}
       else{auto* s=pixels+y*stride+x*4;d[0]=s[2];d[1]=s[1];d[2]=s[0];}d[3]=255;}
      if(!ANativeWindow_unlockAndPost(window.p))++posts;
     }
    }
   };
   cb.onStatus=[](const std::string& text){if(text.find("calibration")!=std::string::npos)diagnostic(text);else __android_log_print(ANDROID_LOG_INFO,"FLCross","playback %s",text.c_str());};
   ok=av::runPlayback(p,cb,stop,error);
   ok=ok && video==unsigned(frames) && pcm==uint64_t(frames)*48000/fps && (!window.p || posts==video);
  }
  std::fill(endpoint.token.begin(),endpoint.token.end(),0);
 }catch(const std::exception& e){error=e.what();}
 for(const auto& line:diagnosticLines)__android_log_print(ANDROID_LOG_INFO,"FLCross","%s",line.c_str());diagnosticLines.clear();
 if(bufferedDiagnostics)__android_log_print(ANDROID_LOG_INFO,"FLCross","diagnostic_records_dropped=%u",diagnosticDropped);diagnosticDropped=0;
 if(!error.empty())__android_log_print(ANDROID_LOG_INFO,"FLCross","native failure: %s",error.c_str());
 std::ostringstream result;result<<"{\"ok\":"<<(ok?"true":"false")<<",\"video\":"<<video<<",\"posts\":"<<posts<<",\"pcm_samples\":"<<pcm.load()<<",\"uid\":"<<getuid()<<",\"pid\":"<<getpid()<<"}";
 return env->NewStringUTF(result.str().c_str());
}

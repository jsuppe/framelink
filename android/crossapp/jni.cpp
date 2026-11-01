#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <gst/video/video.h>
#include "av_playback.h"
#include "framelink_ahb.h"
#include "av_memory_ahb.h"
#include "native_renderer.h"
#include <gst/app/gstappsrc.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <mutex>
#include <fstream>
namespace {
bool diagnostics=false;
bool bufferedDiagnostics=false;
std::vector<std::string> diagnosticLines;
unsigned diagnosticDropped=0;
std::mutex diagnosticMutex;
std::string diagnosticFile;
void diagnostic(const std::string& text) {
 if(bufferedDiagnostics){std::lock_guard<std::mutex> lock(diagnosticMutex);if(diagnosticLines.size()<4096)diagnosticLines.push_back("at_ns="+std::to_string(gst_util_get_timestamp())+" "+text);else ++diagnosticDropped;}
 else __android_log_print(ANDROID_LOG_INFO,"FLCross","%s",text.c_str());
}
void check(bool good,const char* text){if(!good)throw std::runtime_error(text);}
struct Fds {std::vector<int> values;~Fds(){for(int fd:values)if(fd>=0)close(fd);}int take(unsigned i){return std::exchange(values.at(i),-1);}};
struct Window {ANativeWindow* p=nullptr;~Window(){if(p)ANativeWindow_release(p);}};
struct VideoMap {GstVideoFrame f{};bool mapped=false;~VideoMap(){if(mapped)gst_video_frame_unmap(&f);}};
unsigned char clamp(int v){return static_cast<unsigned char>(std::max(0,std::min(255,v)));}
}
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_cross_Native_diagnostics(JNIEnv* env,jclass,jint level,jstring path){
 if(path){const char* text=env->GetStringUTFChars(path,nullptr);if(text){diagnosticFile=text;env->ReleaseStringUTFChars(path,text);}}
 gst_init(nullptr,nullptr);diagnostics=true;
 bufferedDiagnostics=level<0;
 g_setenv("FRAMELINK_AUDIO_CALIBRATION_TRACE","1",TRUE);
 gst_debug_add_log_function(+[](GstDebugCategory* category,GstDebugLevel level,const gchar*,const gchar*,gint,GObject*,GstDebugMessage* message,gpointer){
  if(level<=gst_debug_category_get_threshold(category)) {
   const std::string line=std::string(gst_debug_category_get_name(category))+": "+gst_debug_message_get(message);
   if(bufferedDiagnostics)diagnostic(line);
   else __android_log_print(ANDROID_LOG_INFO,"FLGst","%s",line.c_str());
  }
 },nullptr,nullptr);
 gst_debug_set_active(TRUE);
 for(const auto* name:{"audiobasesink","openslessink","opensles_ringbuffer"})gst_debug_set_threshold_for_name(name,static_cast<GstDebugLevel>(level==-2?5:std::max(0,std::min(6,int(level)))));
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
  check(fmt=="BGRA" || fmt=="NV12" || fmt=="RGBA","invalid media format");
  const bool nativeMode=fmt=="RGBA";
  struct Pool{AHardwareBuffer* images[2]{};~Pool(){for(auto*b:images)if(b)AHardwareBuffer_release(b);}} pool;
  const auto budget=unsigned((uint64_t(frames)*1000+fps-1)/fps);check(budget<=300000,"media budget too long");
  av::SessionProvider provider;
  provider.resolve=[&](av::Endpoint& ep,unsigned,std::string& e){ep=endpoint;e.clear();return true;};
  provider.connect=[&](const av::Endpoint&,char role,unsigned timeout,std::string& e){return av::adoptConnectedStreamFd(fds.take(role=='C'?0:1),timeout,e);};
  provider.openVideo=[&](const flSessionConfig& cfg,bool isProducer,unsigned timeout,std::string& e){
   flChannel* channel=nullptr;const auto limit=std::min(timeout,2000u);
   flResult result=FL_INTERNAL;
   if(nativeMode&&!isProducer){
    AHardwareBuffer_Desc d{};d.width=cfg.video.width;d.height=cfg.video.height;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    for(auto&b:pool.images)check(!AHardwareBuffer_allocate(&d,&b),"native pool allocation");
    result=flCreateChannelFromSocketFdWithHardwareBuffers(fds.take(2),d.width,d.height,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE,limit,pool.images,&channel);
   }else result=isProducer?flOpenProducerFromSocketFd(fds.take(2),limit,&channel):
    flCreateChannelFromSocketFd(fds.take(2),cfg.video.width,cfg.video.height,flFormat(cfg.video.format),4,FL_MAP_CPU,limit,&channel);
   if(result!=FL_OK)e=flResultString(result);return channel;
  };
  if(nativeMode)provider.configureMemory=[&](const flSessionConfig&cfg,bool isProducer,flChannel*c,GstElement*element,av::MemoryOffer&o,unsigned,std::string&e){
   flMemoryRequirementsInit(&o.requirements);o.requirements.allowedResourceTypes=FL_MEMORY_AHB;o.requirements.requiredUsage=isProducer?FL_MEMORY_CPU_WRITE:FL_MEMORY_GPU_SAMPLE;
   o.path={1,FL_MEMORY_AHB,o.requirements.requiredUsage,0,{}};
   if(!(isProducer?av::importedAhbBinding(c,cfg,o.resourceBinding,e):av::ahbBinding(cfg,pool.images,2,o.resourceBinding,e)))return false;
   g_object_set(element,"native-memory",TRUE,nullptr);
   if(isProducer){
    auto*parent=gst_object_get_parent(GST_OBJECT(element));auto*source=gst_bin_get_by_name(GST_BIN(parent),"video");gst_object_unref(parent);if(!source){e="native producer appsrc missing";return false;}
    auto*caps=gst_caps_from_string(("video/x-raw(memory:FrameLinkAHardwareBuffer),format=RGBA,width="+std::to_string(cfg.video.width)+",height="+std::to_string(cfg.video.height)+",framerate="+std::to_string(cfg.video.fpsNum)+"/1").c_str());gst_app_src_set_caps(GST_APP_SRC(source),caps);gst_caps_unref(caps);gst_object_unref(source);
    g_object_set(element,"enable-last-sample",FALSE,nullptr);
   }else{
    auto*pad=gst_element_get_static_pad(element,"src");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,+[](GstPad*,GstPadProbeInfo*info,gpointer)->GstPadProbeReturn{
     auto*q=GST_PAD_PROBE_INFO_QUERY(info);if(GST_QUERY_TYPE(q)==GST_QUERY_ALLOCATION){
      if(!gst_mini_object_is_writable(GST_MINI_OBJECT(q)))return GST_PAD_PROBE_DROP;
      flMemoryRequirements r{};flMemoryRequirementsInit(&r);r.requiredUsage=FL_MEMORY_GPU_SAMPLE;r.allowedResourceTypes=FL_MEMORY_AHB;unsigned char wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];if(flMemoryRequirementsEncode(&r,wire,sizeof(wire))!=FL_MEMORY_OK)return GST_PAD_PROBE_DROP;
      auto*bytes=g_bytes_new(wire,sizeof(wire));gst_structure_set(gst_query_writable_structure(q),"framelink-reader-completion-contract",G_TYPE_STRING,"retain-memory-until-gpu-complete-v1","framelink-memory-requirements",G_TYPE_BYTES,bytes,nullptr);g_bytes_unref(bytes);
     }return GST_PAD_PROBE_OK;
    },nullptr,nullptr);gst_object_unref(pad);
   }return true;
  };
  std::atomic<bool> stop{false};
  if(producer){
   av::ProducerOptions p;p.plugin="static";p.format=fmt;p.frames=frames;p.fps=fps;p.mediaRunMs=budget;p.sessionProvider=&provider;p.stop=&stop;
   if(nativeMode)p.videoFrame=[](GstElement*sink,unsigned n,uint64_t pts,uint64_t duration)->GstBuffer*{
    GstBuffer*b=nullptr;g_signal_emit_by_name(sink,"acquire-native-buffer",2000u,&b);check(b,"native source pool timeout");
    struct Guard{GstBuffer*b;~Guard(){if(b)gst_buffer_unref(b);}} guard{b};auto*ahb=gst_framelink_memory_get_hardware_buffer(gst_buffer_peek_memory(b,0));check(ahb,"native source AHB");AHardwareBuffer_Desc d{};AHardwareBuffer_describe(ahb,&d);void*data=nullptr;
    check(!AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data),"native content lock");
    for(unsigned y=0;y<d.height;y++)for(unsigned x=0;x<d.width;x++){auto*v=(uint8_t*)data+(y*d.stride+x)*4;v[0]=uint8_t(n);v[1]=x;v[2]=y;v[3]=255;}
    int ready=-1;check(!AHardwareBuffer_unlock(ahb,&ready),"native content unlock");gboolean accepted=FALSE;g_signal_emit_by_name(sink,"set-native-ready-fd",b,ready,&accepted);if(ready>=0)close(ready);check(accepted,"native content readiness rejected");
    GST_BUFFER_PTS(b)=pts;GST_BUFFER_DURATION(b)=duration;if(!n)GST_BUFFER_FLAG_SET(b,GST_BUFFER_FLAG_DISCONT);guard.b=nullptr;return b;
   };
   ok=av::runBrokerProducer(p,error);
  }else{
   av::PlaybackOptions p;p.plugin="static";p.format=fmt;p.fps=fps;p.mediaRunMs=budget;p.sessionProvider=&provider;
   p.clockPolicy=producerClock?av::PlaybackClockPolicy::ProducerMapped:av::PlaybackClockPolicy::AudioPreferredCoordinated;p.audioSink=audible?"openslessink":"fakesink";p.recoveryAttempts=0;
   if(surface)window.p=ANativeWindow_fromSurface(env,surface);
   if(window.p)check(ANativeWindow_setBuffersGeometry(window.p,160,90,WINDOW_FORMAT_RGBA_8888)==0,"surface geometry");
   NativeRenderer renderer;
   av::PlaybackCallbacks cb;
   if(nativeMode)cb.onPrepare=[&](unsigned){renderer.prepare(window.p);};
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
    const auto n=video;
    if(diagnostics)diagnostic("video received="+std::to_string(n)+" pts="+std::to_string(pts)+" clock="+std::to_string(gst_clock_get_time(clock.clock))+" base="+std::to_string(clock.baseTimeNs)+" latency="+std::to_string(clock.latencyNs));
    check(n<unsigned(frames),"too many video frames");
    if(pts!=n*GST_SECOND/fps)throw std::runtime_error("video original PTS/identity gap: expected="+std::to_string(n*GST_SECOND/fps)+" actual="+std::to_string(pts));
    GstVideoInfo info{};check(gst_video_info_from_caps(&info,gst_sample_get_caps(sample)),"video caps");
    check(info.width==160 && info.height==90,"video dimensions");
    if(nativeMode){check(GST_VIDEO_INFO_FORMAT(&info)==GST_VIDEO_FORMAT_RGBA,"native caps format");
     const auto due=clock.baseTimeNs+clock.latencyNs+pts;const auto end=g_get_monotonic_time()+2000000;
     while(gst_clock_get_time(clock.clock)<due){check(g_get_monotonic_time()<end,"native presentation clock deadline");g_usleep(1000);}
     renderer.render(sample,unsigned(n));++video;if(window.p)++posts;return;
    }
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
     // A stopped device clock must not strand a Surface callback beyond the
     // session's steady-state idle bound (outer receiver checks cannot run here).
     const auto waitEnd=g_get_monotonic_time()+2000000;
     while(gst_clock_get_time(clock.clock)<due){check(g_get_monotonic_time()<waitEnd,"video device-clock idle deadline");g_usleep(1000);}
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
 std::vector<std::string> saved;unsigned lost=0;
 {std::lock_guard<std::mutex> lock(diagnosticMutex);saved.swap(diagnosticLines);lost=std::exchange(diagnosticDropped,0);}
 if(bufferedDiagnostics && !diagnosticFile.empty()) {
  std::ofstream out(diagnosticFile,std::ios::trunc);
  for(const auto& line:saved)out<<line<<'\n';
  out<<"diagnostic_records="<<saved.size()<<" dropped="<<lost<<'\n';out.flush();
  __android_log_print(ANDROID_LOG_INFO,"FLCross","diagnostic_file_saved=%s",out.good()?"true":"false");
 }
 for(const auto& line:saved)__android_log_print(ANDROID_LOG_INFO,"FLCross","%s",line.c_str());
 if(bufferedDiagnostics)__android_log_print(ANDROID_LOG_INFO,"FLCross","diagnostic_records_dropped=%u",lost);
 if(!error.empty())__android_log_print(ANDROID_LOG_INFO,"FLCross","native failure: %s",error.c_str());
 std::ostringstream result;result<<"{\"ok\":"<<(ok?"true":"false")<<",\"video\":"<<video<<",\"posts\":"<<posts<<",\"pcm_samples\":"<<pcm.load()<<",\"uid\":"<<getuid()<<",\"pid\":"<<getpid()<<"}";
 return env->NewStringUTF(result.str().c_str());
}

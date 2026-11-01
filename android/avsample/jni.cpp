#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <gst/video/video.h>
#include "av_playback.h"
#include <mutex>
#include <cstring>
#include <vector>
static std::atomic<bool> stopFlag{false}, pauseFlag{false};
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_av_AvService_control(JNIEnv*,jclass,jboolean stop,jboolean pause) { stopFlag=stop; pauseFlag=pause; }
#ifdef AV_PRODUCER
int flProducerMain(int,char**);
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_av_ProducerService_run(JNIEnv* env,jclass,jstring dir) {
 const char* d=env->GetStringUTFChars(dir,nullptr);std::string path(d);env->ReleaseStringUTFChars(dir,d);
 std::vector<std::string> args={"producer","static","--session",path,"--frames","150","--timeout-ms","15000"};
 std::vector<char*> argv;for(auto& s:args)argv.push_back(s.data());
 argv.push_back(nullptr);
 int rc=flProducerMain(argv.size()-1,argv.data());__android_log_print(ANDROID_LOG_INFO,"FrameLinkAV","producer result=%d",rc);
}
#else
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_av_AvService_run(JNIEnv* env,jclass,jstring dir,jobject surface) {
 const char* d=env->GetStringUTFChars(dir,nullptr);av::PlaybackOptions p;p.session=d;env->ReleaseStringUTFChars(dir,d);
 p.plugin="static";p.audioSink="openslessink";p.paused=&pauseFlag;p.recoveryAttempts=1;
 ANativeWindow* window=ANativeWindow_fromSurface(env,surface);
 if(!window)return;
 uint64_t presented=0, late=0, audioSamples=0;
 av::PlaybackCallbacks cb;
 cb.onMetrics=[&](const av::PlaybackMetrics& m){audioSamples=m.audioSamples;};
 cb.onStatus=[](const std::string& s){__android_log_print(ANDROID_LOG_INFO,"FrameLinkAV","%s",s.c_str());};
 cb.onClock=[](const av::PlaybackClock& c){__android_log_print(ANDROID_LOG_INFO,"FrameLinkAV","clock audio_master=%d",c.audioMaster);};
 cb.onVideo=[&](GstSample* sample,uint64_t pts,const av::PlaybackClock& clock){
  auto due=clock.baseTimeNs+clock.latencyNs+pts;
  while(!stopFlag && !pauseFlag && gst_clock_get_time(clock.clock)<due)g_usleep(1000);
  if(stopFlag || pauseFlag)return;
  if(gst_clock_get_time(clock.clock)>due+40000000){++late;return;}
  GstVideoInfo info;gst_video_info_init(&info);
  if(!gst_video_info_from_caps(&info,gst_sample_get_caps(sample)) || GST_VIDEO_INFO_FORMAT(&info)!=GST_VIDEO_FORMAT_BGRA)return;
  GstVideoFrame f;if(!gst_video_frame_map(&f,&info,gst_sample_get_buffer(sample),GST_MAP_READ))return;
  ANativeWindow_setBuffersGeometry(window,info.width,info.height,WINDOW_FORMAT_RGBA_8888);
  ANativeWindow_Buffer out;
  if(ANativeWindow_lock(window,&out,nullptr)==0){
   for(unsigned y=0;y<info.height;++y){auto* src=(uint8_t*)GST_VIDEO_FRAME_PLANE_DATA(&f,0)+y*GST_VIDEO_FRAME_PLANE_STRIDE(&f,0);auto* dst=(uint8_t*)out.bits+y*out.stride*4;
    for(unsigned x=0;x<info.width;++x){dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4];dst[x*4+3]=255;}}
   ANativeWindow_unlockAndPost(window);++presented;
  }gst_video_frame_unmap(&f);
 };
 std::string error;bool ok=av::runPlayback(p,cb,stopFlag,error);
 __android_log_print(ANDROID_LOG_INFO,"FrameLinkAV","receiver result=%d presented=%llu late=%llu audio_samples=%llu error=%s",ok,(unsigned long long)presented,(unsigned long long)late,(unsigned long long)audioSamples,error.c_str());
 ANativeWindow_release(window);
}
#endif

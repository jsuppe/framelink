// framelinkavsrc: consumer convenience bin composing the validated
// framelinksrc and framelinkaudiosrc with one coordinated session. Separate
// video_src/audio_src pads feed application-chosen rendering; no display or
// audio device is hardwired.
//
// Two modes, chosen while NULL:
//  - session=PATH: the bin performs private local discovery itself as the
//    consumer (fl_gst_session_open_local_av on a worker thread; no state change
//    blocks on the peer). It is a live source: READY->PAUSED returns
//    NO_PREROLL, inner sources stay locked in NULL until the session is ready
//    and the bin is PLAYING, then start and the bin reports ready to the
//    producer. Timeline: exactly fl_gst_session_apply_pipeline_clock(consumer)
//    on the top-level pipeline at NULL->READY (system clock unless the
//    application fixed one, start time NONE, base time 0, latency-ms): mapped
//    PTS are receiver system-clock times. Once every enabled track delivered
//    EOS downstream the bin reports the drain, releasing the producer's EOS.
//    A new session (reconnect) requires NULL, as for the producer bin.
//  - no session: thin composition. The application installs the
//    framelink.session.v1 context, adopts the video channel through the bin's
//    "adopt-channel" action and applies the pipeline clock itself.
#include "framelink_session_transport.h"
#include "fl_gst_session_internal.h"
#include "framelink_memory_policy.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <cstring>

GST_DEBUG_CATEGORY_STATIC(fl_avsrc_debug);
#define GST_CAT_DEFAULT fl_avsrc_debug

namespace {
struct Shared {
 std::mutex mutex;std::condition_variable changed;
 bool setupDone=false,setupOk=false,released=false,readySent=false,cancelled=false,playing=false,failed=false;
 bool eos[2]={false,false};
 flChannel*channel=nullptr;
 GstFrameLinkMemoryPolicy bound{};
};
}

typedef struct _GstFrameLinkAvSrc {
 GstBin parent;
 gchar*sessionPath,*tracks,*videoFormat,*audioFormat;
 guint width,height,fpsN,fpsD,rate,channels,connectMs,idleMs,audioQueueMs,videoQueue,videoPool,latencyMs;gboolean nativeMemory;
 GstElement*video,*audio;GstPad*videoPad,*audioPad;
 FlGstSession*session;gboolean local,sharedCpu,policyBound,nativeAdopted;
 GstFrameLinkMemoryPolicy nativeCandidate;gboolean candidateSet,nativeUsed;
 GCancellable*cancel;std::thread*worker;std::thread*drainer;Shared*shared;gulong probes[2];
} GstFrameLinkAvSrc;
typedef struct _GstFrameLinkAvSrcClass {GstBinClass parent;} GstFrameLinkAvSrcClass;
G_DEFINE_TYPE(GstFrameLinkAvSrc,gst_framelink_av_src,GST_TYPE_BIN)

// ANY features: framelinksrc negotiates native memory (DMABuf, D3D11Memory,
// FrameLinkAHardwareBuffer) or system memory with downstream itself.
static GstStaticPadTemplate videoTemplate=GST_STATIC_PAD_TEMPLATE("video_src",GST_PAD_SRC,GST_PAD_SOMETIMES,
 GST_STATIC_CAPS("video/x-raw(ANY),format={BGRA,RGBA,NV12,RGB10A2_LE}"));
static GstStaticPadTemplate audioTemplate=GST_STATIC_PAD_TEMPLATE("audio_src",GST_PAD_SRC,GST_PAD_SOMETIMES,
 GST_STATIC_CAPS("audio/x-raw,format={S16LE,F32LE},layout=interleaved,rate=[8000,192000],channels=[1,2]"));

enum {PROP_0,PROP_SESSION,PROP_TRACKS,PROP_VIDEO_FORMAT,PROP_WIDTH,PROP_HEIGHT,PROP_FRAMERATE,PROP_AUDIO_FORMAT,PROP_RATE,PROP_CHANNELS,
 PROP_CONNECT,PROP_IDLE,PROP_AUDIO_QUEUE,PROP_VIDEO_QUEUE,PROP_VIDEO_POOL,PROP_LATENCY,PROP_NATIVE,PROP_SHARED_CPU,PROP_ID,PROP_GENERATION,PROP_AUDIO_ID,PROP_VIDEO_ID};

static bool wantVideo(GstFrameLinkAvSrc*s){return s->tracks&&(!strcmp(s->tracks,"av")||!strcmp(s->tracks,"video"));}
static bool wantAudio(GstFrameLinkAvSrc*s){return s->tracks&&(!strcmp(s->tracks,"av")||!strcmp(s->tracks,"audio"));}
static flFormat videoFormat(const char*f){
 if(!f)return FL_FORMAT_BGRA8;if(!strcmp(f,"RGBA"))return FL_FORMAT_RGBA8;if(!strcmp(f,"NV12"))return FL_FORMAT_NV12;
 if(!strcmp(f,"RGB10A2_LE"))return FL_FORMAT_RGB10A2;return FL_FORMAT_BGRA8;}
static bool validVideoFormat(const char*f){return f&&(!strcmp(f,"BGRA")||!strcmp(f,"RGBA")||!strcmp(f,"NV12")||!strcmp(f,"RGB10A2_LE"));}

static bool monotonicSystemClock(GstElement*element){
 GstClock*clock=gst_element_get_clock(element);bool ok=false;
 if(clock&&GST_IS_SYSTEM_CLOCK(clock)){GstClockType type=GST_CLOCK_TYPE_REALTIME;g_object_get(clock,"clock-type",&type,nullptr);ok=type==GST_CLOCK_TYPE_MONOTONIC;}
 if(clock)gst_object_unref(clock);return ok;
}
static constexpr const char* kClockError="framelinkavsrc with session= requires the pipeline system clock (mapped PTS are system-clock times); do not fix another pipeline clock";

static void stopSession(GstFrameLinkAvSrc*s){
 if(s->cancel)g_cancellable_cancel(s->cancel);
 {std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->cancelled=true;s->shared->changed.notify_all();}
 if(s->session)fl_gst_session_cancel(s->session);
}

static void releaseMedia(GstElement*element,gpointer){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(element);auto*sh=s->shared;
 flChannel*channel=nullptr;
 {std::lock_guard<std::mutex> lock(sh->mutex);if(sh->released||sh->cancelled||!sh->setupOk||!sh->playing)return;channel=sh->channel;sh->channel=nullptr;}
 auto fail=[&](const char*why){GST_ELEMENT_ERROR(s,RESOURCE,OPEN_READ,("%s",why),(nullptr));stopSession(s);};
 if(!monotonicSystemClock(element)){if(channel)flClose(channel);return fail(kClockError);}
 if(s->video&&!s->sharedCpu){gboolean adopted=FALSE;g_signal_emit_by_name(s->video,"adopt-channel",channel,&adopted);if(!adopted){if(channel)flClose(channel);return fail("video channel lease rejected");}}
 if(s->candidateSet){
  auto policy=sh->bound;
  std::swap(policy.requirements[0],policy.requirements[1]);std::swap(policy.paths[0],policy.paths[1]);
  gboolean accepted=FALSE;g_signal_emit_by_name(s->video,"apply-memory-policy",&policy,&accepted);
  if(!accepted)return fail("authenticated local native policy rejected");
 }
 auto*context=fl_gst_session_create_context(s->session);
 for(auto*child:{s->video,s->audio}){
  if(!child)continue;gst_element_set_context(child,context);
  gst_element_set_start_time(child,GST_CLOCK_TIME_NONE);gst_element_set_base_time(child,gst_element_get_base_time(element));
  gst_element_set_locked_state(child,FALSE);
  if(gst_element_set_state(child,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){gst_context_unref(context);return fail("inner FrameLink source failed to start");}
 }
 gst_context_unref(context);
 std::lock_guard<std::mutex> lock(sh->mutex);sh->released=true;sh->changed.notify_all();
 GST_INFO_OBJECT(s,"session released");
}

static void startWorker(GstFrameLinkAvSrc*s){
 flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 if(wantVideo(s)){caps.videoCount=1;caps.video[0]={guint32(videoFormat(s->videoFormat)),s->width,s->height,s->fpsN,s->fpsD};}
 if(wantAudio(s)){caps.audioCount=1;caps.audio[0]={guint(s->audioFormat&&!strcmp(s->audioFormat,"F32LE")?FL_PCM_F32:FL_PCM_S16),s->rate,s->channels};}
 // Native output needs deferred read release: downstream holds ring images as
 // GstMemory until it drops them.
 const guint flags=s->nativeMemory?guint(FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA):0u;
 const std::string path(s->sessionPath);
 g_object_ref(s->session);
 s->worker=new std::thread([s,caps,path,flags]{
  auto*sh=s->shared;GError*error=nullptr;flChannel*channel=nullptr;
  bool ok=false;
#if defined(_WIN32)
  if(s->candidateSet)ok=fl_gst_session_open_local_native_epoch(s->session,FALSE,path.c_str(),&caps,s->videoQueue,s->videoPool,s->audioQueueMs,s->connectMs,s->idleMs,
   &s->nativeCandidate,&s->shared->bound,&channel,nullptr,nullptr,s->cancel,&error);
  else
#endif
#if defined(__linux__) || defined(_WIN32)
  if(s->sharedCpu)ok=fl_gst_session_open_local_cpu(s->session,FALSE,path.c_str(),&caps,s->videoQueue,s->videoPool,s->audioQueueMs,s->connectMs,s->idleMs,s->cancel,&error);
  else
#endif
  ok=fl_gst_session_open_local_av(s->session,FALSE,path.c_str(),&caps,s->videoQueue,s->videoPool,s->audioQueueMs,s->connectMs,s->idleMs,flags,
   &channel,s->cancel,&error);
  {std::lock_guard<std::mutex> lock(sh->mutex);sh->setupDone=true;sh->setupOk=ok;sh->channel=channel;sh->changed.notify_all();}
  if(ok){
   gst_element_call_async(GST_ELEMENT(s),releaseMedia,nullptr,nullptr);
   // Report ready only once the inner sources run (the producer then releases
   // its media); waits for PLAYING like a live source without preroll.
   {std::unique_lock<std::mutex> lock(sh->mutex);sh->changed.wait(lock,[&]{return sh->released||sh->cancelled;});ok=sh->released;}
   if(ok){ok=fl_gst_session_local_ready_cancellable(s->session,FALSE,s->connectMs,s->cancel,&error);
    std::lock_guard<std::mutex> lock(sh->mutex);sh->readySent=ok;sh->changed.notify_all();}
  }
  bool post;{std::lock_guard<std::mutex> lock(sh->mutex);post=!ok&&!sh->cancelled;}
  if(post)GST_ELEMENT_ERROR(s,RESOURCE,OPEN_READ,("%s",error?error->message:"session setup failed"),("framelinkavsrc local session setup"));
  g_clear_error(&error);g_object_unref(s->session);
 });
}

// All enabled tracks delivered EOS: every buffer left the transport, so the
// producer may close. Rendering the tail is downstream's business. GstBaseSrc
// also pushes EOS after a fatal flow error; a failed session reports no drain,
// exactly like an application that stops on the error.
static GstPadProbeReturn eosProbe(GstPad*pad,GstPadProbeInfo*info,gpointer data){
 auto*s=static_cast<GstFrameLinkAvSrc*>(data);auto*sh=s->shared;auto*event=GST_PAD_PROBE_INFO_EVENT(info);
 if(GST_EVENT_TYPE(event)!=GST_EVENT_EOS)return GST_PAD_PROBE_OK;
 std::lock_guard<std::mutex> lock(sh->mutex);sh->eos[pad==s->videoPad?0:1]=true;
 if(sh->failed||(s->videoPad&&!sh->eos[0])||(s->audioPad&&!sh->eos[1])||s->drainer)return GST_PAD_PROBE_OK;
 g_object_ref(s->session);
 s->drainer=new std::thread([s,sh]{
  {std::unique_lock<std::mutex> lock(sh->mutex);sh->changed.wait(lock,[&]{return sh->readySent||sh->cancelled;});
   if(sh->failed||sh->cancelled){g_object_unref(s->session);return;}}
  GError*error=nullptr;const bool ok=fl_gst_session_finish_local_cancellable(s->session,FALSE,s->connectMs,s->cancel,&error);
  if(!ok&&!g_cancellable_is_cancelled(s->cancel))GST_ELEMENT_ERROR(s,RESOURCE,READ,("%s",error?error->message:"drain report failed"),(nullptr));
  g_clear_error(&error);g_object_unref(s->session);
 });
 return GST_PAD_PROBE_OK;
}

static void joinThreads(GstFrameLinkAvSrc*s){
 for(auto**t:{&s->worker,&s->drainer})if(*t){if((*t)->get_id()==std::this_thread::get_id())(*t)->detach();else (*t)->join();delete *t;*t=nullptr;}
}
static void resetSession(GstFrameLinkAvSrc*s){
 joinThreads(s);
 if(s->shared->channel)flClose(s->shared->channel);
 delete s->shared;s->shared=new Shared;
 g_clear_object(&s->cancel);
 if(s->local){GST_OBJECT_LOCK(s);auto*session=s->session;s->session=nullptr;GST_OBJECT_UNLOCK(s);if(session)g_object_unref(session);}
 s->local=FALSE;
}

static void rebuild(GstFrameLinkAvSrc*s){
 for(auto**pad:{&s->videoPad,&s->audioPad})if(*pad){gst_element_remove_pad(GST_ELEMENT(s),*pad);*pad=nullptr;}
 for(auto**child:{&s->video,&s->audio})if(*child){gst_bin_remove(GST_BIN(s),*child);*child=nullptr;}
 auto add=[&](const char*factory,const char*name,GstStaticPadTemplate*templ,GstElement**child,GstPad**pad){
  *child=gst_element_factory_make(factory,name);if(!*child)return;
  gst_bin_add(GST_BIN(s),*child);
  auto*target=gst_element_get_static_pad(*child,"src");
  *pad=gst_ghost_pad_new_from_template(templ->name_template,target,gst_static_pad_template_get(templ));gst_object_unref(target);
  gst_element_add_pad(GST_ELEMENT(s),*pad);
 };
 if(wantVideo(s))add("framelinksrc","video",&videoTemplate,&s->video,&s->videoPad);
 if(wantAudio(s)){add("framelinkaudiosrc","audio",&audioTemplate,&s->audio,&s->audioPad);if(s->audio)g_object_set(s->audio,"tracks",wantVideo(s)?"av":"audio",nullptr);}
}

// Inner elements carry the negotiated geometry and PCM shape in both modes.
static void configureChildren(GstFrameLinkAvSrc*s){
 if(s->video&&!s->policyBound)g_object_set(s->video,"width",gint(s->width),"height",gint(s->height),"format",s->videoFormat,"native-memory",s->nativeMemory,nullptr);
 if(s->audio)g_object_set(s->audio,"format",s->audioFormat,"rate",s->rate,"channels",s->channels,"audio-queue-ms",s->audioQueueMs,
  "connect-timeout-ms",s->connectMs,"idle-timeout-ms",s->idleMs,nullptr);
}

static GstStateChangeReturn changeState(GstElement*element,GstStateChange transition){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(element);
 switch(transition){
 case GST_STATE_CHANGE_NULL_TO_READY:{
  if(s->candidateSet&&s->nativeUsed){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("local native bins are single-session; create a new bin for reconnect"),(nullptr));return GST_STATE_CHANGE_FAILURE;}

  if(s->sharedCpu&&(!wantVideo(s)||s->nativeMemory)){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("shared-cpu requires a video track and forbids native memory"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
#if !defined(__linux__) && !defined(_WIN32)
  if(s->sharedCpu&&s->sessionPath&&*s->sessionPath){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("local shared-cpu discovery unavailable on this platform"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
#endif
  if(s->video)g_object_set(s->video,"shared-cpu",s->sharedCpu,nullptr);
  if(!s->tracks||(!wantVideo(s)&&!wantAudio(s))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("tracks must be audio, video or av"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if((wantVideo(s)&&!s->video)||(wantAudio(s)&&!s->audio)){GST_ELEMENT_ERROR(s,CORE,MISSING_PLUGIN,("FrameLink video/audio sources unavailable"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(wantVideo(s)&&(!validVideoFormat(s->videoFormat)||!s->fpsN||!s->fpsD)){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("unsupported video-format or framerate"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(wantAudio(s)&&(!s->audioFormat||(strcmp(s->audioFormat,"S16LE")&&strcmp(s->audioFormat,"F32LE")))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("unsupported audio-format"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  configureChildren(s);
  if(s->sessionPath&&*s->sessionPath){
   GstElement*top=element;while(auto*parent=GST_ELEMENT(gst_element_get_parent(top))){if(top!=element)gst_object_unref(top);top=parent;}
   if(top==element||!GST_IS_PIPELINE(top)){if(top!=element)gst_object_unref(top);GST_ELEMENT_ERROR(s,CORE,STATE_CHANGE,("framelinkavsrc with session= must be inside a GstPipeline"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
   auto*session=fl_gst_session_new((wantVideo(s)?FL_GST_TRACK_VIDEO:0u)|(wantAudio(s)?FL_GST_TRACK_AUDIO:0u));
   GST_OBJECT_LOCK(s);s->session=session;GST_OBJECT_UNLOCK(s);s->local=TRUE;s->cancel=g_cancellable_new();
   // Exactly fl_gst_session_apply_pipeline_clock(consumer). A clock the
   // application fixed is respected here and refused at PLAYING if it is not
   // the system clock (an audio sink's clock would otherwise be selected).
   if(!GST_OBJECT_FLAG_IS_SET(top,GST_PIPELINE_FLAG_FIXED_CLOCK)){auto*clock=gst_system_clock_obtain();gst_pipeline_use_clock(GST_PIPELINE(top),clock);gst_object_unref(clock);}
   gst_element_set_start_time(top,GST_CLOCK_TIME_NONE);gst_element_set_base_time(top,0);
   gst_pipeline_set_latency(GST_PIPELINE(top),GstClockTime(s->latencyMs)*GST_MSECOND);gst_object_unref(top);
   GST_INFO_OBJECT(s,"pipeline system clock, start time NONE, base time 0, latency %ums",s->latencyMs);
   for(auto*child:{s->video,s->audio})if(child)gst_element_set_locked_state(child,TRUE);
   GstPad*pads[2]={s->videoPad,s->audioPad};
   for(int i=0;i<2;i++)s->probes[i]=pads[i]?gst_pad_add_probe(pads[i],GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,eosProbe,s,nullptr):0;
   if(s->candidateSet)s->nativeUsed=TRUE;
   startWorker(s);
  }
  break;}
 case GST_STATE_CHANGE_READY_TO_PAUSED:
  if(s->local){bool cancelled;{std::lock_guard<std::mutex> lock(s->shared->mutex);cancelled=s->shared->cancelled;}
   if(cancelled){GST_ELEMENT_ERROR(s,CORE,STATE_CHANGE,("framelinkavsrc session ended; set the bin to NULL to start a new session"),(nullptr));return GST_STATE_CHANGE_FAILURE;}}
  break;
 case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
  if(s->local){
   if(!monotonicSystemClock(element)){GST_ELEMENT_ERROR(s,CORE,CLOCK,("%s",kClockError),(nullptr));stopSession(s);return GST_STATE_CHANGE_FAILURE;}
   bool released,ready;
   {std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->playing=true;released=s->shared->released;ready=s->shared->setupDone&&s->shared->setupOk;}
   if(!released&&ready)gst_element_call_async(element,releaseMedia,nullptr,nullptr);
  }
  break;
 case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
  if(s->local){std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->playing=false;}
  break;
 case GST_STATE_CHANGE_PAUSED_TO_READY:if(s->local)stopSession(s);break;
 default:break;
 }
 auto result=GST_ELEMENT_CLASS(gst_framelink_av_src_parent_class)->change_state(element,transition);
 // A live source: no preroll, also while the inner sources are still locked.
 if(s->local&&result!=GST_STATE_CHANGE_FAILURE&&(transition==GST_STATE_CHANGE_READY_TO_PAUSED||transition==GST_STATE_CHANGE_PLAYING_TO_PAUSED))
  result=GST_STATE_CHANGE_NO_PREROLL;
 if(s->local&&(transition==GST_STATE_CHANGE_READY_TO_NULL||(transition==GST_STATE_CHANGE_NULL_TO_READY&&result==GST_STATE_CHANGE_FAILURE))){
  stopSession(s);
  for(auto*child:{s->video,s->audio})if(child){gst_element_set_state(child,GST_STATE_NULL);gst_element_set_locked_state(child,FALSE);}
  GstPad*pads[2]={s->videoPad,s->audioPad};
  for(int i=0;i<2;i++)if(pads[i]&&s->probes[i]){gst_pad_remove_probe(pads[i],s->probes[i]);s->probes[i]=0;}
  resetSession(s);
 }
 return result;
}

static void handleMessage(GstBin*bin,GstMessage*message){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(bin);
 // Inner errors are posted before GstBaseSrc pushes its EOS: no drain report.
 if(s->local&&GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->failed=true;}
 GST_BIN_CLASS(gst_framelink_av_src_parent_class)->handle_message(bin,message);
}

static gboolean adoptChannel(GstFrameLinkAvSrc*s,flChannel*channel){
 if(s->candidateSet||s->sharedCpu||s->local||!s->video||GST_STATE(s)!=GST_STATE_NULL)return FALSE;
 configureChildren(s);
 gboolean adopted=FALSE;g_signal_emit_by_name(s->video,"adopt-channel",channel,&adopted);if(adopted)s->nativeAdopted=TRUE;return adopted;
}

// Endpoint zero is the receiver, exactly as for the thin source action.
// The trusted caller authenticates the policy and validates the adopted resource.
static gboolean applyMemoryPolicy(GstFrameLinkAvSrc*s,GstFrameLinkMemoryPolicy*policy){
 if(s->candidateSet||s->sharedCpu||s->local||!s->video||s->policyBound||
    (s->sessionPath&&*s->sessionPath)||GST_STATE(s)!=GST_STATE_NULL)return FALSE;
 configureChildren(s);
 gboolean accepted=FALSE;g_signal_emit_by_name(s->video,"apply-memory-policy",policy,&accepted);
 if(accepted)s->policyBound=TRUE;return accepted;
}

// Provisional producer-ordered template, never an authenticated policy. Copy
// it while NULL; the worker authenticates BOTH peers before allocating a channel.
static gboolean setLocalNativeCandidate(GstFrameLinkAvSrc*s,GstFrameLinkMemoryPolicy*policy){
#if defined(_WIN32)
 if(!policy||s->candidateSet||s->nativeAdopted||s->policyBound||s->local||s->sharedCpu||!wantVideo(s)||!s->video||
    !s->sessionPath||!*s->sessionPath||GST_STATE(s)!=GST_STATE_NULL)return FALSE;
 const bool nativeReceiver=policy->paths[1].supportedUsage==FL_MEMORY_GPU_SAMPLE&&policy->paths[1].operations==0;
 const bool readbackReceiver=policy->paths[1].supportedUsage==FL_MEMORY_CPU_READ&&policy->paths[1].operations==(FL_MEMORY_COPY|FL_MEMORY_READBACK);
 flMemoryDecision decisions[2]{};
 if(policy->version!=1||policy->reserved||policy->sessionGeneration||
    flMemoryEvaluateBoundary(policy->requirements,policy->paths,2,decisions)!=FL_MEMORY_OK||
    policy->paths[0].resourceType!=FL_MEMORY_D3D11||policy->paths[1].resourceType!=FL_MEMORY_D3D11||
    policy->paths[0].operations||(!nativeReceiver&&!readbackReceiver)||
    (policy->paths[0].supportedUsage!=FL_MEMORY_GPU_SAMPLE&&
     policy->paths[0].supportedUsage!=(FL_MEMORY_GPU_SAMPLE|FL_MEMORY_GPU_RENDER))||bool(s->nativeMemory)!=nativeReceiver)return FALSE;
 s->nativeCandidate=*policy;s->candidateSet=TRUE;return TRUE;
#else
 (void)s;(void)policy;return FALSE;
#endif
}

static void setProperty(GObject*object,guint id,const GValue*value,GParamSpec*spec){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(object);
 if(s->candidateSet||s->policyBound){g_warning("framelinkavsrc: authenticated properties are immutable; create a new bin/session");return;}
 if(GST_STATE(s)!=GST_STATE_NULL){g_warning("framelinkavsrc: %s can only change in NULL",spec->name);return;}
 switch(id){
 case PROP_SHARED_CPU:s->sharedCpu=g_value_get_boolean(value);break;
 case PROP_SESSION:g_free(s->sessionPath);s->sessionPath=g_value_dup_string(value);break;
 case PROP_TRACKS:{const char*t=g_value_get_string(value);if(!t||(strcmp(t,"av")&&strcmp(t,"audio")&&strcmp(t,"video"))){g_warning("framelinkavsrc: tracks must be audio, video or av");break;}
  g_free(s->tracks);s->tracks=g_strdup(t);rebuild(s);break;}
 case PROP_VIDEO_FORMAT:g_free(s->videoFormat);s->videoFormat=g_value_dup_string(value);break;
 case PROP_WIDTH:s->width=g_value_get_uint(value);break;case PROP_HEIGHT:s->height=g_value_get_uint(value);break;
 case PROP_FRAMERATE:s->fpsN=guint(gst_value_get_fraction_numerator(value));s->fpsD=guint(gst_value_get_fraction_denominator(value));break;
 case PROP_AUDIO_FORMAT:g_free(s->audioFormat);s->audioFormat=g_value_dup_string(value);break;
 case PROP_RATE:s->rate=g_value_get_uint(value);break;case PROP_CHANNELS:s->channels=g_value_get_uint(value);break;
 case PROP_CONNECT:s->connectMs=g_value_get_uint(value);break;case PROP_IDLE:s->idleMs=g_value_get_uint(value);break;
 case PROP_AUDIO_QUEUE:s->audioQueueMs=g_value_get_uint(value);break;case PROP_VIDEO_QUEUE:s->videoQueue=g_value_get_uint(value);break;
 case PROP_VIDEO_POOL:s->videoPool=g_value_get_uint(value);break;case PROP_LATENCY:s->latencyMs=g_value_get_uint(value);break;
 case PROP_NATIVE:s->nativeMemory=g_value_get_boolean(value);break;
 default:G_OBJECT_WARN_INVALID_PROPERTY_ID(object,id,spec);
 }
}
static void getProperty(GObject*object,guint id,GValue*value,GParamSpec*spec){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(object);flSessionConfig c{};
 if(id>=PROP_ID){FlGstSession*session=nullptr;GST_OBJECT_LOCK(s);if(s->session)session=static_cast<FlGstSession*>(g_object_ref(s->session));GST_OBJECT_UNLOCK(s);
  if(session){if(fl_gst_session_get_config(session,&c)!=FL_GST_SESSION_READY)c={};g_object_unref(session);}}
 switch(id){
 case PROP_SHARED_CPU:g_value_set_boolean(value,s->sharedCpu);break;
 case PROP_SESSION:g_value_set_string(value,s->sessionPath);break;case PROP_TRACKS:g_value_set_string(value,s->tracks);break;
 case PROP_VIDEO_FORMAT:g_value_set_string(value,s->videoFormat);break;
 case PROP_WIDTH:g_value_set_uint(value,s->width);break;case PROP_HEIGHT:g_value_set_uint(value,s->height);break;
 case PROP_FRAMERATE:gst_value_set_fraction(value,gint(s->fpsN),gint(s->fpsD));break;
 case PROP_AUDIO_FORMAT:g_value_set_string(value,s->audioFormat);break;
 case PROP_RATE:g_value_set_uint(value,s->rate);break;case PROP_CHANNELS:g_value_set_uint(value,s->channels);break;
 case PROP_CONNECT:g_value_set_uint(value,s->connectMs);break;case PROP_IDLE:g_value_set_uint(value,s->idleMs);break;
 case PROP_AUDIO_QUEUE:g_value_set_uint(value,s->audioQueueMs);break;case PROP_VIDEO_QUEUE:g_value_set_uint(value,s->videoQueue);break;
 case PROP_VIDEO_POOL:g_value_set_uint(value,s->videoPool);break;case PROP_LATENCY:g_value_set_uint(value,s->latencyMs);break;
 case PROP_NATIVE:g_value_set_boolean(value,s->nativeMemory);break;
 case PROP_GENERATION:g_value_set_uint64(value,c.generation);break;
 case PROP_AUDIO_ID:g_value_set_uint(value,c.audioTrackId);break;case PROP_VIDEO_ID:g_value_set_uint(value,c.videoTrackId);break;
 case PROP_ID:{gchar text[33]{};if(c.generation)for(unsigned i=0;i<16;i++)g_snprintf(text+2*i,3,"%02x",c.sessionId.bytes[i]);g_value_set_string(value,c.generation?text:nullptr);break;}
 default:G_OBJECT_WARN_INVALID_PROPERTY_ID(object,id,spec);
 }
}
static void dispose(GObject*object){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(object);
 if(s->shared){stopSession(s);joinThreads(s);}
 G_OBJECT_CLASS(gst_framelink_av_src_parent_class)->dispose(object);
}
static void finalize(GObject*object){
 auto*s=reinterpret_cast<GstFrameLinkAvSrc*>(object);
 if(s->shared->channel)flClose(s->shared->channel);delete s->shared;
 g_clear_object(&s->cancel);g_clear_object(&s->session);
 g_free(s->sessionPath);g_free(s->tracks);g_free(s->videoFormat);g_free(s->audioFormat);
 G_OBJECT_CLASS(gst_framelink_av_src_parent_class)->finalize(object);
}
static void gst_framelink_av_src_init(GstFrameLinkAvSrc*s){
 s->tracks=g_strdup("av");s->videoFormat=g_strdup("BGRA");s->audioFormat=g_strdup("S16LE");
 s->width=1280;s->height=720;s->fpsN=30;s->fpsD=1;s->rate=48000;s->channels=2;
 s->connectMs=15000;s->idleMs=15000;s->audioQueueMs=80;s->videoQueue=2;s->videoPool=8;s->latencyMs=0;s->nativeMemory=FALSE;
 s->shared=new Shared;
 rebuild(s);
}
static void gst_framelink_av_src_class_init(GstFrameLinkAvSrcClass*klass){
 GST_DEBUG_CATEGORY_INIT(fl_avsrc_debug,"framelinkavsrc",0,"FrameLink consumer A/V bin");
 auto*o=G_OBJECT_CLASS(klass);o->set_property=setProperty;o->get_property=getProperty;o->dispose=dispose;o->finalize=finalize;
 GST_ELEMENT_CLASS(klass)->change_state=changeState;GST_BIN_CLASS(klass)->handle_message=handleMessage;auto*e=GST_ELEMENT_CLASS(klass);
 gst_element_class_set_static_metadata(e,"FrameLink A/V consumer","Source/Video/Audio",
  "Composes framelinksrc and framelinkaudiosrc on one coordinated session; separate video_src/audio_src pads, no display or audio device","FrameLink");
 gst_element_class_add_static_pad_template(e,&videoTemplate);gst_element_class_add_static_pad_template(e,&audioTemplate);
 const auto rw=GParamFlags(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY);
 const auto ro=GParamFlags(G_PARAM_READABLE|G_PARAM_STATIC_STRINGS);
 g_object_class_install_property(o,PROP_SESSION,g_param_spec_string("session","Session","Private current-user local discovery directory (not a secret or URL); unset: use an installed framelink.session.v1 context",nullptr,rw));
 g_object_class_install_property(o,PROP_TRACKS,g_param_spec_string("tracks","Tracks","Required track set: audio, video or av; only enabled pads exist","av",rw));
 g_object_class_install_property(o,PROP_VIDEO_FORMAT,g_param_spec_string("video-format","Video format","Accepted video format (BGRA, RGBA, NV12, RGB10A2_LE)","BGRA",rw));
 g_object_class_install_property(o,PROP_WIDTH,g_param_spec_uint("width","Width","Accepted video width",16,8192,1280,rw));
 g_object_class_install_property(o,PROP_HEIGHT,g_param_spec_uint("height","Height","Accepted video height",16,8192,720,rw));
 g_object_class_install_property(o,PROP_FRAMERATE,gst_param_spec_fraction("framerate","Framerate","Accepted video frame rate with session=",1,1,240,1,30,1,rw));
 g_object_class_install_property(o,PROP_AUDIO_FORMAT,g_param_spec_string("audio-format","Audio format","Accepted PCM format (S16LE, F32LE)","S16LE",rw));
 g_object_class_install_property(o,PROP_RATE,g_param_spec_uint("rate","Sample rate","Accepted PCM rate",8000,192000,48000,rw));
 g_object_class_install_property(o,PROP_CHANNELS,g_param_spec_uint("channels","Channels","Accepted PCM channels",1,2,2,rw));
 g_object_class_install_property(o,PROP_CONNECT,g_param_spec_uint("connect-timeout-ms","Connect timeout","Session setup and ready deadline",1,60000,15000,rw));
 g_object_class_install_property(o,PROP_IDLE,g_param_spec_uint("idle-timeout-ms","Idle timeout","Bounded transport idle deadline",1,60000,15000,rw));
 g_object_class_install_property(o,PROP_AUDIO_QUEUE,g_param_spec_uint("audio-queue-ms","Audio queue","Hard audio queue budget, not target latency",1,2000,80,rw));
 g_object_class_install_property(o,PROP_VIDEO_QUEUE,g_param_spec_uint("video-queue-frames","Video queue","Coordinated video queue budget",1,16,2,rw));
 g_object_class_install_property(o,PROP_VIDEO_POOL,g_param_spec_uint("video-pool","Video pool","Ring images this consumer allocates with session= (0: 4); cover ceil(latency*fps)+2",0,FL_MAX_POOL_SIZE,8,rw));
 g_object_class_install_property(o,PROP_LATENCY,g_param_spec_uint("latency-ms","Latency","Explicit pipeline latency with session=; zero for transport, set required final-playback buffering explicitly",0,10000,0,rw));
 g_object_class_install_property(o,PROP_NATIVE,g_param_spec_boolean("native-memory","Native memory","Output platform GPU memory (DMABuf, D3D11Memory, FrameLinkAHardwareBuffer) from the ring; with session= the ring defers read release",FALSE,rw));
 g_object_class_install_property(o,PROP_SHARED_CPU,g_param_spec_boolean("shared-cpu","Shared CPU","Explicit CPU video copy path; broker context or current-user session= discovery, no native fallback",FALSE,rw));
 g_object_class_install_property(o,PROP_ID,g_param_spec_string("session-id","Session identity","Negotiated identity, not a secret",nullptr,ro));
 g_object_class_install_property(o,PROP_GENERATION,g_param_spec_uint64("generation","Generation","Negotiated session generation",0,G_MAXUINT64,0,ro));
 g_object_class_install_property(o,PROP_AUDIO_ID,g_param_spec_uint("audio-track-id","Audio track","Negotiated audio identity, zero when absent",0,G_MAXUINT,0,ro));
 g_object_class_install_property(o,PROP_VIDEO_ID,g_param_spec_uint("video-track-id","Video track","Negotiated video identity, zero when absent",0,G_MAXUINT,0,ro));
 g_signal_new_class_handler("set-local-native-candidate",G_TYPE_FROM_CLASS(klass),GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(setLocalNativeCandidate),
  nullptr,nullptr,nullptr,G_TYPE_BOOLEAN,1,G_TYPE_POINTER);
 g_signal_new_class_handler("apply-memory-policy",G_TYPE_FROM_CLASS(klass),GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(applyMemoryPolicy),
  nullptr,nullptr,nullptr,G_TYPE_BOOLEAN,1,G_TYPE_POINTER);
 g_signal_new_class_handler("adopt-channel",G_TYPE_FROM_CLASS(klass),GSignalFlags(G_SIGNAL_RUN_LAST|G_SIGNAL_ACTION),G_CALLBACK(adoptChannel),
  nullptr,nullptr,nullptr,G_TYPE_BOOLEAN,1,G_TYPE_POINTER);
}
gboolean fl_register_av_src(GstPlugin*plugin){return gst_element_register(plugin,"framelinkavsrc",GST_RANK_NONE,gst_framelink_av_src_get_type());}

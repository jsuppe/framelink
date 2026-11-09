// framelinkavsink: producer convenience bin composing the validated
// framelinksink and framelinkaudiosink with one coordinated session.
//
// Two modes, chosen while NULL:
//  - session=PATH: the bin performs private local discovery itself
//    (fl_gst_session_open_local_av on a worker thread; no state change blocks on
//    the peer). Inner sinks stay locked in NULL and media waits at the ghost
//    pads (bounded by connect-timeout-ms, woken by flush/state change) until the
//    session is ready, the consumer reported ready and the bin is PLAYING.
//    Timeline: exactly fl_gst_session_apply_pipeline_clock(producer). At
//    NULL->READY the bin sets its top-level pipeline's start time to NONE and
//    base time to the current system-clock time, and proposes that instant as
//    the session epoch. Running time is then clock - epoch on both tracks with
//    no offset or rebasing; a producer pause is a media gap and consumer
//    latency is unchanged. The bin selects the system clock unless the
//    application fixed a pipeline clock, which must then be the system clock.
//    Child EOS is held until the consumer confirms its drain.
//  - no session: thin composition. The application installs the
//    framelink.session.v1 context, adopts the video channel through the bin's
//    "adopt-channel" action and applies the pipeline clock itself.
#include "framelink_session_transport.h"
#include "fl_gst_session_internal.h"
#include "framelink_memory_policy.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstring>

GST_DEBUG_CATEGORY_STATIC(fl_avsink_debug);
#define GST_CAT_DEFAULT fl_avsink_debug

namespace {
struct Shared {
 std::mutex mutex;std::condition_variable changed;
 bool setupDone=false,setupOk=false,released=false,cancelled=false,playing=false;
 bool flushing[2]={false,false};
 guint eosSeen=0; // protected with heldEos and drainer publication by mutex
 gint64 epoch=-1;
 GstFrameLinkMemoryPolicy bound{};
 std::string error;flChannel*channel=nullptr;
};
}

typedef struct _GstFrameLinkAvSink {
 GstBin parent;
 gchar*sessionPath,*tracks,*videoFormat,*audioFormat;
 guint width,height,fpsN,fpsD,rate,channels,connectMs,idleMs,audioQueueMs,videoQueue,videoPool,bufferWaitMs;
 GstElement*video,*audio;GstPad*videoPad,*audioPad;
 FlGstSession*session;gboolean errorOnRingFull,local,sharedCpu,nativeMemory,policyBound,nativeAdopted,savedLastSample,lastSampleScoped;
 GstFrameLinkMemoryPolicy nativeCandidate;gboolean candidateSet,nativeUsed,channelAdopted;
 GCancellable*cancel;std::thread*worker;std::thread*drainer;Shared*shared;
 std::vector<GstMessage*>*heldEos;guint eosNeeded;gulong probes[2];
} GstFrameLinkAvSink;
typedef struct _GstFrameLinkAvSinkClass {GstBinClass parent;} GstFrameLinkAvSinkClass;
G_DEFINE_TYPE(GstFrameLinkAvSink,gst_framelink_av_sink,GST_TYPE_BIN)

#ifdef FL_GST_D3D11
#define FL_AV_SINK_NATIVE_CAPS ";video/x-raw(memory:D3D11Memory),format={BGRA,RGBA,NV12,RGB10A2_LE}"
#else
#define FL_AV_SINK_NATIVE_CAPS ""
#endif
static GstStaticPadTemplate videoTemplate=GST_STATIC_PAD_TEMPLATE("video_sink",GST_PAD_SINK,GST_PAD_SOMETIMES,
 GST_STATIC_CAPS("video/x-raw,format={BGRA,RGBA,NV12,RGB10A2_LE}" FL_AV_SINK_NATIVE_CAPS));
static GstStaticPadTemplate audioTemplate=GST_STATIC_PAD_TEMPLATE("audio_sink",GST_PAD_SINK,GST_PAD_SOMETIMES,
 GST_STATIC_CAPS("audio/x-raw,format={S16LE,F32LE},layout=interleaved,rate=[8000,192000],channels=[1,2]"));

enum {PROP_0,PROP_SESSION,PROP_TRACKS,PROP_VIDEO_FORMAT,PROP_WIDTH,PROP_HEIGHT,PROP_FRAMERATE,PROP_AUDIO_FORMAT,PROP_RATE,PROP_CHANNELS,
 PROP_CONNECT,PROP_IDLE,PROP_AUDIO_QUEUE,PROP_VIDEO_QUEUE,PROP_VIDEO_POOL,PROP_SHARED_CPU,PROP_NATIVE,PROP_BUFFER_WAIT,PROP_ERROR_ON_RING_FULL,PROP_ID,PROP_GENERATION,PROP_AUDIO_ID,PROP_VIDEO_ID};

static bool wantVideo(GstFrameLinkAvSink*s){return s->tracks&&(!strcmp(s->tracks,"av")||!strcmp(s->tracks,"video"));}
static bool wantAudio(GstFrameLinkAvSink*s){return s->tracks&&(!strcmp(s->tracks,"av")||!strcmp(s->tracks,"audio"));}
static flFormat videoFormat(const char*f){
 if(!f)return FL_FORMAT_BGRA8;if(!strcmp(f,"RGBA"))return FL_FORMAT_RGBA8;if(!strcmp(f,"NV12"))return FL_FORMAT_NV12;
 if(!strcmp(f,"RGB10A2_LE"))return FL_FORMAT_RGB10A2;return FL_FORMAT_BGRA8;}
static bool validVideoFormat(const char*f){return f&&(!strcmp(f,"BGRA")||!strcmp(f,"RGBA")||!strcmp(f,"NV12")||!strcmp(f,"RGB10A2_LE"));}

// Media waits here until released; data is never queued inside the bin.
static GstPadProbeReturn holdProbe(GstPad*pad,GstPadProbeInfo*info,gpointer data){
 auto*s=static_cast<GstFrameLinkAvSink*>(data);auto*sh=s->shared;const int track=pad==s->videoPad?0:1;
 if(info->type&GST_PAD_PROBE_TYPE_EVENT_FLUSH){
  auto*e=GST_PAD_PROBE_INFO_EVENT(info);std::lock_guard<std::mutex> lock(sh->mutex);
  if(GST_EVENT_TYPE(e)==GST_EVENT_FLUSH_START)sh->flushing[track]=true;else if(GST_EVENT_TYPE(e)==GST_EVENT_FLUSH_STOP)sh->flushing[track]=false;
  sh->changed.notify_all();return GST_PAD_PROBE_OK;
 }
 std::unique_lock<std::mutex> lock(sh->mutex);
 // Setup (discovery, negotiation, consumer ready) is bounded by two connect
 // deadlines. Once set up, a non-PLAYING pipeline waits like a sink preroll.
 const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(2ull*s->connectMs+1000);
 while(!sh->released&&!sh->cancelled&&!sh->flushing[track]&&!(sh->setupDone&&!sh->setupOk)){
  if(sh->setupDone)sh->changed.wait(lock);
  else if(sh->changed.wait_until(lock,until)==std::cv_status::timeout)break;
 }
 if(!sh->released){
  const bool failed=sh->setupDone&&!sh->setupOk;const bool flushing=sh->cancelled||sh->flushing[track];lock.unlock();
  // Events go on to the locked (flushing) inner sink and fail there, as with
  // any failed sink; dropping sticky events would break pad invariants.
  if(info->type&GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM){if(!failed&&!flushing)GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("FrameLink session was not ready within connect-timeout-ms"),(nullptr));return GST_PAD_PROBE_OK;}
  if(!failed&&!flushing)GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("FrameLink session was not ready within connect-timeout-ms"),(nullptr));
  GST_PAD_PROBE_INFO_FLOW_RETURN(info)=flushing?GST_FLOW_FLUSHING:GST_FLOW_ERROR;
  return GST_PAD_PROBE_HANDLED;
 }
 return GST_PAD_PROBE_OK;
}

static gint64 chooseEpoch(gpointer data,GstClockTime){
 // The pipeline base time set at NULL->READY: running time is session time.
 auto*s=static_cast<GstFrameLinkAvSink*>(data);std::lock_guard<std::mutex> lock(s->shared->mutex);return s->shared->epoch;
}

// The epoch is on the monotonic system clock; any other pipeline clock (an
// audio device clock, a test clock) puts base time on a different time base.
static bool monotonicSystemClock(GstElement*element){
 GstClock*clock=gst_element_get_clock(element);bool ok=false;
 if(clock&&GST_IS_SYSTEM_CLOCK(clock)){GstClockType type=GST_CLOCK_TYPE_REALTIME;g_object_get(clock,"clock-type",&type,nullptr);ok=type==GST_CLOCK_TYPE_MONOTONIC;}
 if(clock)gst_object_unref(clock);return ok;
}
static constexpr const char* kClockError="framelinkavsink with session= requires the pipeline system clock; do not fix another pipeline clock";

static void releaseMedia(GstElement*element,gpointer){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(element);auto*sh=s->shared;
 flChannel*channel=nullptr;
 {std::lock_guard<std::mutex> lock(sh->mutex);if(sh->released||sh->cancelled||!sh->setupOk||!sh->playing)return;channel=sh->channel;sh->channel=nullptr;}
 auto fail=[&](const char*why){GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("%s",why),(nullptr));fl_gst_session_cancel(s->session);
  std::lock_guard<std::mutex> lock(sh->mutex);sh->setupOk=false;sh->changed.notify_all();};
 if(!monotonicSystemClock(element)){if(channel)flClose(channel);return fail(kClockError);}
 const GstClockTime base=gst_element_get_base_time(element);gint64 epoch;
 {std::lock_guard<std::mutex> lock(sh->mutex);epoch=sh->epoch;}
 if(gint64(base)!=epoch){if(channel)flClose(channel);return fail("pipeline base time changed after framelinkavsink set it to the session epoch");}
 if(s->video&&!s->sharedCpu){gboolean adopted=FALSE;g_signal_emit_by_name(s->video,"adopt-channel",channel,&adopted);if(!adopted){if(channel)flClose(channel);return fail("video channel lease rejected");}}
 if(s->candidateSet){
  auto policy=sh->bound;
  gboolean accepted=FALSE;g_signal_emit_by_name(s->video,"apply-memory-policy",&policy,&accepted);
  if(!accepted)return fail("authenticated local native policy rejected");
 }
 auto*context=fl_gst_session_create_context(s->session);
 for(auto*child:{s->video,s->audio}){
  if(!child)continue;gst_element_set_context(child,context);
  gst_element_set_start_time(child,GST_CLOCK_TIME_NONE);gst_element_set_base_time(child,base);
  gst_element_set_locked_state(child,FALSE);
  if(gst_element_set_state(child,GST_STATE(element))==GST_STATE_CHANGE_FAILURE){gst_context_unref(context);return fail("inner FrameLink sink failed to start");}
 }
 gst_context_unref(context);
 std::lock_guard<std::mutex> lock(sh->mutex);sh->released=true;sh->changed.notify_all();
 GST_INFO_OBJECT(s,"session released: epoch %" G_GINT64_FORMAT,epoch);
}

static void startWorker(GstFrameLinkAvSink*s){
 flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 if(wantVideo(s)){caps.videoCount=1;caps.video[0]={guint32(videoFormat(s->videoFormat)),s->width,s->height,s->fpsN,s->fpsD};}
 if(wantAudio(s)){caps.audioCount=1;caps.audio[0]={guint(s->audioFormat&&!strcmp(s->audioFormat,"F32LE")?FL_PCM_F32:FL_PCM_S16),s->rate,s->channels};}
 const std::string path(s->sessionPath);
 g_object_ref(s->session);
 s->worker=new std::thread([s,caps,path]{
  GError*error=nullptr;flChannel*channel=nullptr;
  bool ok=false;
#if defined(_WIN32)
  if(s->candidateSet)ok=fl_gst_session_open_local_native_epoch(s->session,TRUE,path.c_str(),&caps,s->videoQueue,s->videoPool,s->audioQueueMs,s->connectMs,s->idleMs,
   &s->nativeCandidate,&s->shared->bound,&channel,chooseEpoch,s,s->cancel,&error);
  else
#endif
#if defined(__linux__) || defined(_WIN32)
  if(s->sharedCpu)ok=fl_gst_session_open_local_cpu_epoch(s->session,TRUE,path.c_str(),&caps,s->videoQueue,s->videoPool,s->audioQueueMs,s->connectMs,s->idleMs,chooseEpoch,s,s->cancel,&error);
  else
#endif
  ok=fl_gst_session_open_local_av_epoch(s->session,TRUE,path.c_str(),&caps,s->videoQueue,s->videoPool,s->audioQueueMs,s->connectMs,s->idleMs,0,
   &channel,chooseEpoch,s,s->cancel,&error);
  // Do not release media until the consumer's sinks and devices exist.
  if(ok)ok=fl_gst_session_local_ready_cancellable(s->session,TRUE,s->connectMs,s->cancel,&error);
  const std::string why=error?error->message:"session setup failed";g_clear_error(&error);
  bool post=false;
  {std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->setupDone=true;s->shared->setupOk=ok;s->shared->channel=channel;
   if(!ok){s->shared->error=why;post=!s->shared->cancelled;}s->shared->changed.notify_all();}
  if(post)GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("%s",why.c_str()),("framelinkavsink local session setup"));
  if(ok)gst_element_call_async(GST_ELEMENT(s),releaseMedia,nullptr,nullptr);
  g_object_unref(s->session);
 });
}

static void stopSession(GstFrameLinkAvSink*s){
 if(s->cancel)g_cancellable_cancel(s->cancel);
 {std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->cancelled=true;s->shared->changed.notify_all();}
 if(s->session)fl_gst_session_cancel(s->session);
}
static void joinThreads(GstFrameLinkAvSink*s){
 for(auto**t:{&s->worker,&s->drainer})if(*t){if((*t)->get_id()==std::this_thread::get_id())(*t)->detach();else (*t)->join();delete *t;*t=nullptr;}
}
static void resetSession(GstFrameLinkAvSink*s){
 joinThreads(s);
 for(auto*m:*s->heldEos)gst_message_unref(m);s->heldEos->clear();
 if(s->shared->channel)flClose(s->shared->channel);
 delete s->shared;s->shared=new Shared;
 g_clear_object(&s->cancel);
 if(s->local){GST_OBJECT_LOCK(s);auto*session=s->session;s->session=nullptr;GST_OBJECT_UNLOCK(s);if(session)g_object_unref(session);}
 s->local=FALSE;
}

static void rebuild(GstFrameLinkAvSink*s){
 for(auto**pad:{&s->videoPad,&s->audioPad})if(*pad){gst_element_remove_pad(GST_ELEMENT(s),*pad);*pad=nullptr;}
 for(auto**child:{&s->video,&s->audio})if(*child){gst_bin_remove(GST_BIN(s),*child);*child=nullptr;}
 auto add=[&](const char*factory,const char*name,GstStaticPadTemplate*templ,GstElement**child,GstPad**pad){
  *child=gst_element_factory_make(factory,name);if(!*child)return;
  gst_bin_add(GST_BIN(s),*child);
  auto*target=gst_element_get_static_pad(*child,"sink");
  *pad=gst_ghost_pad_new_from_template(templ->name_template,target,gst_static_pad_template_get(templ));gst_object_unref(target);
  gst_element_add_pad(GST_ELEMENT(s),*pad);
 };
 if(wantVideo(s))add("framelinksink","video",&videoTemplate,&s->video,&s->videoPad);
 if(s->video)g_object_set(s->video,"native-memory",s->nativeMemory,"buffer-wait-ms",s->bufferWaitMs,"error-on-ring-full",s->errorOnRingFull,nullptr);
 if(wantAudio(s)){add("framelinkaudiosink","audio",&audioTemplate,&s->audio,&s->audioPad);if(s->audio)g_object_set(s->audio,"tracks",wantVideo(s)?"av":"audio",nullptr);}
}

static GstStateChangeReturn changeState(GstElement*element,GstStateChange transition){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(element);
 switch(transition){
 case GST_STATE_CHANGE_NULL_TO_READY:{
  if(s->candidateSet&&s->nativeUsed){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("local native bins are single-session; create a new bin for reconnect"),(nullptr));return GST_STATE_CHANGE_FAILURE;}

  if(s->nativeMemory&&(!wantVideo(s)||s->sharedCpu||(!s->candidateSet&&(!s->policyBound||(s->sessionPath&&*s->sessionPath))))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("native-memory requires broker policy or an explicit local native candidate and forbids shared-cpu"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(s->sharedCpu&&(!wantVideo(s))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("shared-cpu requires a video track and forbids native memory"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
#if !defined(__linux__) && !defined(_WIN32)
  if(s->sharedCpu&&s->sessionPath&&*s->sessionPath){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("local shared-cpu discovery unavailable on this platform"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
#endif
  if(s->video)g_object_set(s->video,"shared-cpu",s->sharedCpu,nullptr);
  if(!s->tracks||(!wantVideo(s)&&!wantAudio(s))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("tracks must be audio, video or av"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if((wantVideo(s)&&!s->video)||(wantAudio(s)&&!s->audio)){GST_ELEMENT_ERROR(s,CORE,MISSING_PLUGIN,("FrameLink video/audio sinks unavailable"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(s->sessionPath&&*s->sessionPath){
   if(wantVideo(s)&&(!validVideoFormat(s->videoFormat)||!s->fpsN||!s->fpsD)){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("unsupported video-format or framerate"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
   if(wantAudio(s)&&(!s->audioFormat||(strcmp(s->audioFormat,"S16LE")&&strcmp(s->audioFormat,"F32LE")))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("unsupported audio-format"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
   auto*session=fl_gst_session_new((wantVideo(s)?FL_GST_TRACK_VIDEO:0u)|(wantAudio(s)?FL_GST_TRACK_AUDIO:0u));
   GST_OBJECT_LOCK(s);s->session=session;GST_OBJECT_UNLOCK(s);s->local=TRUE;
   s->cancel=g_cancellable_new();s->eosNeeded=(s->video?1u:0u)+(s->audio?1u:0u);
   // Like fl_gst_session_apply_pipeline_clock: wall-clock running time from the
   // session epoch (start time NONE, base time = epoch).
   GstElement*top=element;while(auto*parent=GST_ELEMENT(gst_element_get_parent(top))){if(top!=element)gst_object_unref(top);top=parent;}
   if(top==element||!GST_IS_PIPELINE(top)){if(top!=element)gst_object_unref(top);GST_ELEMENT_ERROR(s,CORE,STATE_CHANGE,("framelinkavsink with session= must be inside a GstPipeline"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
   {auto*clock=gst_system_clock_obtain();const GstClockTime now=gst_clock_get_time(clock);
    // A clock the application fixed is respected and refused at PLAYING if it
    // is not the system clock (a capture device clock would otherwise win).
    if(!GST_OBJECT_FLAG_IS_SET(top,GST_PIPELINE_FLAG_FIXED_CLOCK))gst_pipeline_use_clock(GST_PIPELINE(top),clock);
    gst_object_unref(clock);
    gst_element_set_start_time(top,GST_CLOCK_TIME_NONE);gst_element_set_base_time(top,now);gst_object_unref(top);
    std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->epoch=gint64(now);}
   GST_INFO_OBJECT(s,"pipeline start time NONE, base time = session epoch");
   for(auto*child:{s->video,s->audio})if(child)gst_element_set_locked_state(child,TRUE);
   if(s->video)g_object_set(s->video,"async",FALSE,nullptr); // no re-preroll inside a PLAYING pipeline
   GstPad*pads[2]={s->videoPad,s->audioPad};
   for(int i=0;i<2;i++)s->probes[i]=pads[i]?gst_pad_add_probe(pads[i],GstPadProbeType(GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM|GST_PAD_PROBE_TYPE_EVENT_FLUSH),holdProbe,s,nullptr):0;
   if(s->candidateSet)s->nativeUsed=TRUE;
   startWorker(s);
  }
  break;}
 case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
  if(s->local){
   // Refuse before the base time can influence the session epoch.
   if(!monotonicSystemClock(element)){GST_ELEMENT_ERROR(s,CORE,CLOCK,("%s",kClockError),(nullptr));stopSession(s);return GST_STATE_CHANGE_FAILURE;}
   bool released,ready;
   {std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->playing=true;released=s->shared->released;ready=s->shared->setupDone&&s->shared->setupOk;}
   if(!released&&ready)gst_element_call_async(element,releaseMedia,nullptr,nullptr);
  }
  break;
 case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
  if(s->local){std::lock_guard<std::mutex> lock(s->shared->mutex);s->shared->playing=false;}
  break;
 case GST_STATE_CHANGE_READY_TO_PAUSED:
  if(s->local){bool cancelled;{std::lock_guard<std::mutex> lock(s->shared->mutex);cancelled=s->shared->cancelled;}
   if(cancelled){GST_ELEMENT_ERROR(s,CORE,STATE_CHANGE,("framelinkavsink session ended; set the bin to NULL to start a new session"),(nullptr));return GST_STATE_CHANGE_FAILURE;}}
  break;
 case GST_STATE_CHANGE_PAUSED_TO_READY:if(s->local)stopSession(s);break;
 default:break;
 }
 // The CPU transport has copied the input when render returns. Retaining a
 // display-style last sample pins an unrelated upstream pool buffer and can
 // force first-touch allocation inside the next frame's deadline. Scope this
 // to our internal CPU child; preserve thin-sink and native-path preferences.
 if(transition==GST_STATE_CHANGE_NULL_TO_READY&&s->sharedCpu&&s->video){
  g_object_get(s->video,"enable-last-sample",&s->savedLastSample,nullptr);
  g_object_set(s->video,"enable-last-sample",FALSE,nullptr);s->lastSampleScoped=TRUE;
 }
 auto result=GST_ELEMENT_CLASS(gst_framelink_av_sink_parent_class)->change_state(element,transition);
 if(s->local&&(transition==GST_STATE_CHANGE_READY_TO_NULL||(transition==GST_STATE_CHANGE_NULL_TO_READY&&result==GST_STATE_CHANGE_FAILURE))){
  stopSession(s);
  for(auto*child:{s->video,s->audio})if(child){gst_element_set_state(child,GST_STATE_NULL);gst_element_set_locked_state(child,FALSE);}
  GstPad*pads[2]={s->videoPad,s->audioPad};
  for(int i=0;i<2;i++)if(pads[i]&&s->probes[i]){gst_pad_remove_probe(pads[i],s->probes[i]);s->probes[i]=0;}
  if(s->video)g_object_set(s->video,"async",TRUE,nullptr);
  resetSession(s);
 }
 if(s->lastSampleScoped&&(transition==GST_STATE_CHANGE_READY_TO_NULL||
    (transition==GST_STATE_CHANGE_NULL_TO_READY&&result==GST_STATE_CHANGE_FAILURE))){
  if(s->video)g_object_set(s->video,"enable-last-sample",s->savedLastSample,nullptr);
  s->lastSampleScoped=FALSE;
 }
 return result;
}

static void handleMessage(GstBin*bin,GstMessage*message){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(bin);
 if(s->local&&GST_MESSAGE_TYPE(message)==GST_MESSAGE_EOS&&GST_MESSAGE_SRC(message)!=GST_OBJECT(bin)){
  // Audio/video post from independent streaming threads. Serialize both the
  // collection and single drainer publication; never forward while locked.
  std::lock_guard<std::mutex> lock(s->shared->mutex);
  const guint track=GST_MESSAGE_SRC(message)==GST_OBJECT(s->video)?1u:
                    GST_MESSAGE_SRC(message)==GST_OBJECT(s->audio)?2u:0u;
  if(s->shared->cancelled || !track || (s->shared->eosSeen&track)){
   gst_message_unref(message);return;
  }
  s->shared->eosSeen|=track;
  // Hold sink EOS until the consumer confirms it rendered the tail.
  s->heldEos->push_back(message);
  if(s->heldEos->size()==s->eosNeeded&&!s->drainer){
   g_object_ref(s->session);
   s->drainer=new std::thread([s]{
    GError*error=nullptr;const bool ok=fl_gst_session_finish_local_cancellable(s->session,TRUE,s->connectMs+s->idleMs,s->cancel,&error);
    if(!ok&&!g_cancellable_is_cancelled(s->cancel))GST_ELEMENT_ERROR(s,RESOURCE,WRITE,("%s",error?error->message:"consumer drain failed"),(nullptr));
    g_clear_error(&error);
    if(ok){std::vector<GstMessage*> held;{std::lock_guard<std::mutex> lock(s->shared->mutex);held.swap(*s->heldEos);}for(auto*m:held)GST_BIN_CLASS(gst_framelink_av_sink_parent_class)->handle_message(GST_BIN(s),m);}
    g_object_unref(s->session);
   });
  }
  return;
 }
 GST_BIN_CLASS(gst_framelink_av_sink_parent_class)->handle_message(bin,message);
}

static gboolean adoptChannel(GstFrameLinkAvSink*s,flChannel*channel){
 if(s->candidateSet||s->sharedCpu||s->local||!s->video||GST_STATE(s)!=GST_STATE_NULL)return FALSE;
 gboolean adopted=FALSE;g_signal_emit_by_name(s->video,"adopt-channel",channel,&adopted);if(adopted)s->channelAdopted=TRUE;if(adopted&&s->nativeMemory)s->nativeAdopted=TRUE;return adopted;
}

static gboolean applyMemoryPolicy(GstFrameLinkAvSink*s,GstFrameLinkMemoryPolicy*policy){
 if(s->candidateSet||s->sharedCpu||s->local||!s->video||!s->nativeMemory||s->policyBound||
    (s->sessionPath&&*s->sessionPath)||GST_STATE(s)!=GST_STATE_NULL)return FALSE;
 gboolean accepted=FALSE;g_signal_emit_by_name(s->video,"apply-memory-policy",policy,&accepted);
 if(accepted)s->policyBound=TRUE;return accepted;
}

// Provisional producer-ordered template, never an authenticated policy. Copy
// it while NULL; the worker authenticates BOTH peers before allocating a channel.
static gboolean setLocalNativeCandidate(GstFrameLinkAvSink*s,GstFrameLinkMemoryPolicy*policy){
#if defined(_WIN32)
 if(!policy||s->candidateSet||s->policyBound||s->local||s->sharedCpu||!wantVideo(s)||!s->video||
    !s->sessionPath||!*s->sessionPath||GST_STATE(s)!=GST_STATE_NULL||!s->nativeMemory||s->channelAdopted)return FALSE;
 const bool nativeReceiver=policy->paths[1].supportedUsage==FL_MEMORY_GPU_SAMPLE&&policy->paths[1].operations==0;
 const bool readbackReceiver=policy->paths[1].supportedUsage==FL_MEMORY_CPU_READ&&policy->paths[1].operations==(FL_MEMORY_COPY|FL_MEMORY_READBACK);
 flMemoryDecision decisions[2]{};
 if(policy->version!=1||policy->reserved||policy->sessionGeneration||
    flMemoryEvaluateBoundary(policy->requirements,policy->paths,2,decisions)!=FL_MEMORY_OK||
    policy->paths[0].resourceType!=FL_MEMORY_D3D11||policy->paths[1].resourceType!=FL_MEMORY_D3D11||
    policy->paths[0].operations||(!nativeReceiver&&!readbackReceiver)||
    policy->paths[0].supportedUsage!=FL_MEMORY_GPU_SAMPLE)return FALSE;
 s->nativeCandidate=*policy;s->candidateSet=TRUE;return TRUE;
#else
 (void)s;(void)policy;return FALSE;
#endif
}

static void setProperty(GObject*object,guint id,const GValue*value,GParamSpec*spec){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(object);
 if(s->candidateSet||s->policyBound||s->nativeAdopted){g_warning("framelinkavsink: authenticated properties are immutable; create a new bin/session");return;}
 if(GST_STATE(s)!=GST_STATE_NULL){g_warning("framelinkavsink: %s can only change in NULL",spec->name);return;}
 switch(id){
 case PROP_NATIVE:s->nativeMemory=g_value_get_boolean(value);if(s->video)g_object_set(s->video,"native-memory",s->nativeMemory,nullptr);break;
 case PROP_BUFFER_WAIT:s->bufferWaitMs=g_value_get_uint(value);if(s->video)g_object_set(s->video,"buffer-wait-ms",s->bufferWaitMs,nullptr);break;
 case PROP_ERROR_ON_RING_FULL:s->errorOnRingFull=g_value_get_boolean(value);if(s->video)g_object_set(s->video,"error-on-ring-full",s->errorOnRingFull,nullptr);break;
 case PROP_SHARED_CPU:s->sharedCpu=g_value_get_boolean(value);break;
 case PROP_SESSION:g_free(s->sessionPath);s->sessionPath=g_value_dup_string(value);break;
 case PROP_TRACKS:{const char*t=g_value_get_string(value);if(!t||(strcmp(t,"av")&&strcmp(t,"audio")&&strcmp(t,"video"))){g_warning("framelinkavsink: tracks must be audio, video or av");break;}
  g_free(s->tracks);s->tracks=g_strdup(t);rebuild(s);break;}
 case PROP_VIDEO_FORMAT:g_free(s->videoFormat);s->videoFormat=g_value_dup_string(value);break;
 case PROP_WIDTH:s->width=g_value_get_uint(value);break;case PROP_HEIGHT:s->height=g_value_get_uint(value);break;
 case PROP_FRAMERATE:s->fpsN=guint(gst_value_get_fraction_numerator(value));s->fpsD=guint(gst_value_get_fraction_denominator(value));break;
 case PROP_AUDIO_FORMAT:g_free(s->audioFormat);s->audioFormat=g_value_dup_string(value);break;
 case PROP_RATE:s->rate=g_value_get_uint(value);break;case PROP_CHANNELS:s->channels=g_value_get_uint(value);break;
 case PROP_CONNECT:s->connectMs=g_value_get_uint(value);break;case PROP_IDLE:s->idleMs=g_value_get_uint(value);break;
 case PROP_AUDIO_QUEUE:s->audioQueueMs=g_value_get_uint(value);break;case PROP_VIDEO_QUEUE:s->videoQueue=g_value_get_uint(value);break;
 case PROP_VIDEO_POOL:s->videoPool=g_value_get_uint(value);break;
 default:G_OBJECT_WARN_INVALID_PROPERTY_ID(object,id,spec);
 }
}
static void getProperty(GObject*object,guint id,GValue*value,GParamSpec*spec){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(object);flSessionConfig c{};
 if(id>=PROP_ID){FlGstSession*session=nullptr;GST_OBJECT_LOCK(s);if(s->session)session=static_cast<FlGstSession*>(g_object_ref(s->session));GST_OBJECT_UNLOCK(s);
  if(session){if(fl_gst_session_get_config(session,&c)!=FL_GST_SESSION_READY)c={};g_object_unref(session);}}
 switch(id){
 case PROP_NATIVE:g_value_set_boolean(value,s->nativeMemory);break;
 case PROP_BUFFER_WAIT:g_value_set_uint(value,s->bufferWaitMs);break;
 case PROP_ERROR_ON_RING_FULL:g_value_set_boolean(value,s->errorOnRingFull);break;
 case PROP_SHARED_CPU:g_value_set_boolean(value,s->sharedCpu);break;
 case PROP_SESSION:g_value_set_string(value,s->sessionPath);break;case PROP_TRACKS:g_value_set_string(value,s->tracks);break;
 case PROP_VIDEO_FORMAT:g_value_set_string(value,s->videoFormat);break;
 case PROP_WIDTH:g_value_set_uint(value,s->width);break;case PROP_HEIGHT:g_value_set_uint(value,s->height);break;
 case PROP_FRAMERATE:gst_value_set_fraction(value,gint(s->fpsN),gint(s->fpsD));break;
 case PROP_AUDIO_FORMAT:g_value_set_string(value,s->audioFormat);break;
 case PROP_RATE:g_value_set_uint(value,s->rate);break;case PROP_CHANNELS:g_value_set_uint(value,s->channels);break;
 case PROP_CONNECT:g_value_set_uint(value,s->connectMs);break;case PROP_IDLE:g_value_set_uint(value,s->idleMs);break;
 case PROP_AUDIO_QUEUE:g_value_set_uint(value,s->audioQueueMs);break;case PROP_VIDEO_QUEUE:g_value_set_uint(value,s->videoQueue);break;
 case PROP_VIDEO_POOL:g_value_set_uint(value,s->videoPool);break;
 case PROP_GENERATION:g_value_set_uint64(value,c.generation);break;
 case PROP_AUDIO_ID:g_value_set_uint(value,c.audioTrackId);break;case PROP_VIDEO_ID:g_value_set_uint(value,c.videoTrackId);break;
 case PROP_ID:{gchar text[33]{};if(c.generation)for(unsigned i=0;i<16;i++)g_snprintf(text+2*i,3,"%02x",c.sessionId.bytes[i]);g_value_set_string(value,c.generation?text:nullptr);break;}
 default:G_OBJECT_WARN_INVALID_PROPERTY_ID(object,id,spec);
 }
}
static void dispose(GObject*object){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(object);
 if(s->shared){stopSession(s);joinThreads(s);}
 G_OBJECT_CLASS(gst_framelink_av_sink_parent_class)->dispose(object);
}
static void finalize(GObject*object){
 auto*s=reinterpret_cast<GstFrameLinkAvSink*>(object);
 for(auto*m:*s->heldEos)gst_message_unref(m);delete s->heldEos;
 if(s->shared->channel)flClose(s->shared->channel);delete s->shared;
 g_clear_object(&s->cancel);g_clear_object(&s->session);
 g_free(s->sessionPath);g_free(s->tracks);g_free(s->videoFormat);g_free(s->audioFormat);
 G_OBJECT_CLASS(gst_framelink_av_sink_parent_class)->finalize(object);
}
static void gst_framelink_av_sink_init(GstFrameLinkAvSink*s){
 s->tracks=g_strdup("av");s->videoFormat=g_strdup("BGRA");s->audioFormat=g_strdup("S16LE");
 s->width=1280;s->height=720;s->fpsN=30;s->fpsD=1;s->rate=48000;s->channels=2;
 s->connectMs=15000;s->idleMs=15000;s->audioQueueMs=80;s->videoQueue=2;s->videoPool=0;s->bufferWaitMs=5;s->errorOnRingFull=FALSE;
 s->shared=new Shared;s->heldEos=new std::vector<GstMessage*>;
 rebuild(s);
}
static void gst_framelink_av_sink_class_init(GstFrameLinkAvSinkClass*klass){
 GST_DEBUG_CATEGORY_INIT(fl_avsink_debug,"framelinkavsink",0,"FrameLink producer A/V bin");
 auto*o=G_OBJECT_CLASS(klass);o->set_property=setProperty;o->get_property=getProperty;o->dispose=dispose;o->finalize=finalize;
 auto*e=GST_ELEMENT_CLASS(klass);e->change_state=changeState;GST_BIN_CLASS(klass)->handle_message=handleMessage;
 gst_element_class_set_static_metadata(e,"FrameLink A/V producer","Sink/Video/Audio",
  "Composes framelinksink and framelinkaudiosink on one coordinated session; separate video_sink/audio_sink pads, not a multiplexed format","FrameLink");
 gst_element_class_add_static_pad_template(e,&videoTemplate);gst_element_class_add_static_pad_template(e,&audioTemplate);
 const auto rw=GParamFlags(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS|GST_PARAM_MUTABLE_READY);
 const auto ro=GParamFlags(G_PARAM_READABLE|G_PARAM_STATIC_STRINGS);
 // Additive controls preserve the thin sink's existing defaults. They select
 // video copy-in ring exhaustion only; PCM and native ownership stay strict.
 g_object_class_install_property(o,PROP_BUFFER_WAIT,g_param_spec_uint("buffer-wait-ms","Video buffer wait budget","Maximum video allocation wait; zero is nonblocking, not a presentation delay; set before channel/policy binding",0,1000,5,rw));
 g_object_class_install_property(o,PROP_ERROR_ON_RING_FULL,g_param_spec_boolean("error-on-ring-full","Reject video overload","Fail video copy-in on exhausted ring instead of drop-and-count; does not relax PCM or native ownership rules",FALSE,rw));
 g_object_class_install_property(o,PROP_SESSION,g_param_spec_string("session","Session","Private current-user local discovery directory (not a secret or URL); unset: use an installed framelink.session.v1 context",nullptr,rw));
 g_object_class_install_property(o,PROP_TRACKS,g_param_spec_string("tracks","Tracks","Required track set: audio, video or av; only enabled pads exist","av",rw));
 g_object_class_install_property(o,PROP_VIDEO_FORMAT,g_param_spec_string("video-format","Video format","Offered video format with session= (BGRA, RGBA, NV12, RGB10A2_LE)","BGRA",rw));
 g_object_class_install_property(o,PROP_WIDTH,g_param_spec_uint("width","Width","Offered video width with session=",16,8192,1280,rw));
 g_object_class_install_property(o,PROP_HEIGHT,g_param_spec_uint("height","Height","Offered video height with session=",16,8192,720,rw));
 g_object_class_install_property(o,PROP_FRAMERATE,gst_param_spec_fraction("framerate","Framerate","Offered video frame rate with session=",1,1,240,1,30,1,rw));
 g_object_class_install_property(o,PROP_AUDIO_FORMAT,g_param_spec_string("audio-format","Audio format","Offered PCM format with session= (S16LE, F32LE)","S16LE",rw));
 g_object_class_install_property(o,PROP_RATE,g_param_spec_uint("rate","Sample rate","Offered PCM rate with session=",8000,192000,48000,rw));
 g_object_class_install_property(o,PROP_CHANNELS,g_param_spec_uint("channels","Channels","Offered PCM channels with session=",1,2,2,rw));
 g_object_class_install_property(o,PROP_CONNECT,g_param_spec_uint("connect-timeout-ms","Connect timeout","Session setup and media hold deadline",1,60000,15000,rw));
 g_object_class_install_property(o,PROP_IDLE,g_param_spec_uint("idle-timeout-ms","Idle timeout","Bounded transport idle deadline",1,60000,15000,rw));
 g_object_class_install_property(o,PROP_AUDIO_QUEUE,g_param_spec_uint("audio-queue-ms","Audio queue","Hard audio queue budget, not target latency",1,2000,80,rw));
 g_object_class_install_property(o,PROP_VIDEO_QUEUE,g_param_spec_uint("video-queue-frames","Video queue","Coordinated video queue budget",1,16,2,rw));
 g_object_class_install_property(o,PROP_VIDEO_POOL,g_param_spec_uint("video-pool","Video pool","Consumer ring images requested (0: consumer default)",0,FL_MAX_POOL_SIZE,0,rw));
 g_object_class_install_property(o,PROP_NATIVE,g_param_spec_boolean("native-memory","Native memory","Strict native producer; broker-bound policy or session= with explicit set-local-native-candidate; no implicit fallback",FALSE,rw));
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
gboolean fl_register_av_sink(GstPlugin*plugin){return gst_element_register(plugin,"framelinkavsink",GST_RANK_NONE,gst_framelink_av_sink_get_type());}

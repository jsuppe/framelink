#define FL_GST_SESSION_BUILD
#include "framelink_session_context.h"
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "framelink_shared_cpu_win.h"
#include "fl_gst_cpu_pipe_auth_win.h"
#endif
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#endif
#include "fl_gst_session_internal.h"
#include "fl_gst_event_channel.hpp"
struct _FlGstSession {
 GObject parent;
 std::shared_ptr<FlGstEventChannel>* events;
 flSharedCpuChannel* cpuChannel;
#ifdef _WIN32
 HANDLE cpuCancelEvent;
#else
 int cpuShutdownFd;
#endif
 gboolean cpuOpening,cpuClaimed,cpuProducer;
 GMutex mutex;
 GCond changed;
 guint required,registered,ready;
 gboolean configured,cancelled,mapped,videoEnded;
 guint64 videoFrames;gint64 videoLastPts;
 flClockMapping mapping;
 guint64 maxUncertainty;
 GSocketConnection* audioConnection;
 gboolean audioProducer,audioClaimed;
 flSessionConfig config;
};
typedef struct { GObjectClass parent; } FlGstSessionClass;
G_DEFINE_TYPE(FlGstSession,fl_gst_session,G_TYPE_OBJECT)
static bool valid(FlGstSession* s) {return s&&G_TYPE_CHECK_INSTANCE_TYPE(s,fl_gst_session_get_type());}
static void resetCpu(FlGstSession*s){
#ifdef _WIN32
 if(s->cpuCancelEvent){SetEvent(s->cpuCancelEvent);CloseHandle(s->cpuCancelEvent);s->cpuCancelEvent=nullptr;}
#else
 if(s->cpuShutdownFd>=0){shutdown(s->cpuShutdownFd,SHUT_RDWR);close(s->cpuShutdownFd);s->cpuShutdownFd=-1;}
#endif
 if(s->cpuChannel){flSharedCpuClose(s->cpuChannel);s->cpuChannel=nullptr;}
 // Do not reset cpuOpening: an old setup must return before a new one starts.
}
static void finalize(GObject* object) {
 auto* s=reinterpret_cast<FlGstSession*>(object);
 resetCpu(s);
 delete s->events;
 if(s->audioConnection)g_object_unref(s->audioConnection);
 g_cond_clear(&s->changed);g_mutex_clear(&s->mutex);
 G_OBJECT_CLASS(fl_gst_session_parent_class)->finalize(object);
}
static void fl_gst_session_class_init(FlGstSessionClass* c){G_OBJECT_CLASS(c)->finalize=finalize;}
static void fl_gst_session_init(FlGstSession* s){g_mutex_init(&s->mutex);g_cond_init(&s->changed);s->events=new std::shared_ptr<FlGstEventChannel>();
#ifndef _WIN32
 s->cpuShutdownFd=-1;
#endif
}
namespace { struct Lock {GMutex* m; explicit Lock(GMutex* p):m(p){g_mutex_lock(m);}~Lock(){g_mutex_unlock(m);}}; }
FlGstSession* fl_gst_session_new(guint required) {
 if(!required||(required&~3u))return nullptr;
 auto* s=static_cast<FlGstSession*>(g_object_new(fl_gst_session_get_type(),nullptr));s->required=required;return s;
}
GstContext* fl_gst_session_create_context(FlGstSession* s) {
 if(!valid(s))return nullptr;
 auto* c=gst_context_new(FL_GST_SESSION_CONTEXT,TRUE);
 gst_structure_set(gst_context_writable_structure(c),"coordinator",fl_gst_session_get_type(),s,nullptr);return c;
}
FlGstSession* fl_gst_session_from_context(const GstContext* c) {
 if(!c||!gst_context_has_context_type(c,FL_GST_SESSION_CONTEXT))return nullptr;
 const auto* v=gst_structure_get_value(gst_context_get_structure(c),"coordinator");
 if(!v||!G_VALUE_HOLDS(v,fl_gst_session_get_type()))return nullptr;
 auto* s=static_cast<FlGstSession*>(g_value_get_object(v));
 return valid(s)?static_cast<FlGstSession*>(g_object_ref(s)):nullptr;
}
FlGstSessionResult fl_gst_session_configure(FlGstSession* s,const flSessionConfig* c,
 const flSessionCapabilities* p,const flSessionCapabilities* q) {
 if(!valid(s)||flSessionValidateSelection(c,p,q)!=FL_SESSION_OK)return FL_GST_SESSION_INVALID;
 const guint tracks=(c->videoTrackId?1u:0u)|(c->audioTrackId?2u:0u);
 Lock lock(&s->mutex);
 if(tracks!=s->required)return FL_GST_SESSION_INVALID;
 if(s->configured){auto r=flSessionValidateReplacement(&s->config,c);
  if(r!=FL_SESSION_OK)return r==FL_SESSION_STALE?FL_GST_SESSION_STALE:FL_GST_SESSION_INVALID;
 }
 resetCpu(s);s->cpuClaimed=FALSE;
 if(s->audioConnection){g_socket_shutdown(g_socket_connection_get_socket(s->audioConnection),TRUE,TRUE,nullptr);g_object_unref(s->audioConnection);s->audioConnection=nullptr;}
 if(*s->events)(*s->events)->cancel();s->events->reset();
 s->audioClaimed=FALSE;s->videoEnded=FALSE;s->videoFrames=0;s->videoLastPts=-1;s->config=*c;s->mapped=FALSE;s->configured=TRUE;s->cancelled=FALSE;s->ready=0;g_cond_broadcast(&s->changed);return FL_GST_SESSION_READY;
}
FlGstSessionResult fl_gst_session_register_track(FlGstSession* s,guint track) {
 if(!valid(s)||(track!=1&&track!=2))return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(!(s->required&track)||(s->registered&track))return FL_GST_SESSION_INVALID;
 s->registered|=track;return FL_GST_SESSION_READY;
}
FlGstSessionResult fl_gst_session_track_ready(FlGstSession* s,guint track,guint64 generation) {
 if(!valid(s)||(track!=1&&track!=2))return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(s->cancelled)return FL_GST_SESSION_CANCELLED;
 if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
 if(!(s->registered&track))return FL_GST_SESSION_INVALID;
 s->ready|=track;g_cond_broadcast(&s->changed);return FL_GST_SESSION_READY;
}
FlGstSessionResult fl_gst_session_wait(FlGstSession* s,guint64 generation,guint timeout,flSessionConfig* out) {
 if(!valid(s)||!out||!generation||!timeout||timeout>60000)return FL_GST_SESSION_INVALID;
 const gint64 deadline=g_get_monotonic_time()+gint64(timeout)*1000;
 Lock lock(&s->mutex);
 while(true) {
  if(s->cancelled)return FL_GST_SESSION_CANCELLED;
  if(s->configured&&s->config.generation!=generation)return FL_GST_SESSION_STALE;
  if(s->configured&&s->ready==s->required){*out=s->config;return FL_GST_SESSION_READY;}
  if(!g_cond_wait_until(&s->changed,&s->mutex,deadline))return FL_GST_SESSION_TIMEOUT;
 }
}
void fl_gst_session_cancel(FlGstSession* s) {
 if(!valid(s))return;
 Lock lock(&s->mutex);s->cancelled=TRUE;s->ready=0;
 resetCpu(s);
 if(*s->events)(*s->events)->cancel();
 if(s->audioConnection)g_socket_shutdown(g_socket_connection_get_socket(s->audioConnection),TRUE,TRUE,nullptr);
 g_cond_broadcast(&s->changed);
}

FlGstSessionResult fl_gst_session_set_mapping(FlGstSession* s,guint64 generation,const flClockMapping* m,guint64 maxError) {
 if(!valid(s)||!m)return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(s->cancelled)return FL_GST_SESSION_CANCELLED;
 if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
 gint64 result=0;
 if(flSessionMapPts(&s->config,m,maxError,0,&result)!=FL_SESSION_OK)return FL_GST_SESSION_INVALID;
 if(s->mapped&&(memcmp(m->sourceClockId.bytes,s->mapping.sourceClockId.bytes,16)||memcmp(m->targetClockId.bytes,s->mapping.targetClockId.bytes,16)||m->sourceAnchorNs!=s->mapping.sourceAnchorNs||m->targetAnchorNs!=s->mapping.targetAnchorNs||m->uncertaintyNs!=s->mapping.uncertaintyNs||maxError!=s->maxUncertainty))return FL_GST_SESSION_INVALID;
 s->mapping=*m;s->maxUncertainty=maxError;s->mapped=TRUE;return FL_GST_SESSION_READY;
}
FlGstSessionResult fl_gst_session_map_pts(FlGstSession* s,guint64 generation,gint64 pts,gint64* out) {
 if(!valid(s)||!out)return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(s->cancelled)return FL_GST_SESSION_CANCELLED;
 if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
 if(!s->mapped)return FL_GST_SESSION_INVALID;
 return flSessionMapPts(&s->config,&s->mapping,s->maxUncertainty,pts,out)==FL_SESSION_OK?FL_GST_SESSION_READY:FL_GST_SESSION_INVALID;
}

FlGstSessionResult fl_gst_session_get_config(FlGstSession* s,flSessionConfig* out) {
 if(!valid(s)||!out)return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(*s->events&&(*s->events)->failed()){s->cancelled=TRUE;s->ready=0;
 resetCpu(s);
 if(s->audioConnection)g_socket_shutdown(g_socket_connection_get_socket(s->audioConnection),TRUE,TRUE,nullptr);g_cond_broadcast(&s->changed);}
 if(s->cancelled)return FL_GST_SESSION_CANCELLED;
 if(!s->configured)return FL_GST_SESSION_STALE;
 *out=s->config;return FL_GST_SESSION_READY;
}

gboolean fl_gst_session_publish_audio(FlGstSession* s,const flSessionConfig* config,GSocketConnection* stream,gboolean producer){
 if(!valid(s)||!config||!stream)return FALSE;
 Lock lock(&s->mutex);
 guint8 expected[FL_SESSION_WIRE_SIZE],current[FL_SESSION_WIRE_SIZE];
 if(s->cancelled||!s->configured||s->audioConnection||flSessionEncode(config,expected,sizeof expected)!=FL_SESSION_OK||flSessionEncode(&s->config,current,sizeof current)!=FL_SESSION_OK||memcmp(expected,current,sizeof expected))return FALSE;
 s->audioConnection=static_cast<GSocketConnection*>(g_object_ref(stream));s->audioProducer=producer;s->audioClaimed=FALSE;return TRUE;
}
GSocketConnection* fl_gst_session_claim_audio_stream(FlGstSession* s,gboolean producer,guint64 generation){
 if(!valid(s))return nullptr;
 Lock lock(&s->mutex);
 if(s->cancelled||!s->configured||s->config.generation!=generation||!s->audioConnection||s->audioProducer!=producer||s->audioClaimed)return nullptr;
 s->audioClaimed=TRUE;return static_cast<GSocketConnection*>(g_object_ref(s->audioConnection));
}

gboolean fl_gst_segment_running_time(const GstSegment* segment,GstClockTime pts,GstClockTime* out){
 if(!segment||!out||segment->format!=GST_FORMAT_TIME||segment->rate!=1.0||segment->applied_rate!=1.0||!GST_CLOCK_TIME_IS_VALID(pts))return FALSE;
 auto running=gst_segment_to_running_time(segment,GST_FORMAT_TIME,pts);
 if(!GST_CLOCK_TIME_IS_VALID(running)||running>G_MAXINT64)return FALSE;
 *out=running;return TRUE;
}
FlGstSessionResult fl_gst_session_map_running_time(FlGstSession*s,guint64 generation,gint64 pts,gboolean required,gint64*out){
 if(!valid(s)||!out||pts<0)return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(s->cancelled)return FL_GST_SESSION_CANCELLED;
 if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
 if(!s->mapped){if(required)return FL_GST_SESSION_INVALID;*out=pts;return FL_GST_SESSION_READY;}
 return flSessionMapPts(&s->config,&s->mapping,s->maxUncertainty,pts,out)==FL_SESSION_OK?FL_GST_SESSION_READY:FL_GST_SESSION_INVALID;
}
FlGstSessionResult fl_gst_session_video_eos(FlGstSession*s,guint64 generation,guint64 frames,gint64 pts){
 if(!valid(s)||(frames?pts<0:pts!=-1))return FL_GST_SESSION_INVALID;
 Lock lock(&s->mutex);
 if(s->cancelled)return FL_GST_SESSION_CANCELLED;
 if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
 if(!s->config.videoTrackId)return FL_GST_SESSION_INVALID;
 if(s->videoEnded&&(s->videoFrames!=frames||s->videoLastPts!=pts))return FL_GST_SESSION_INVALID;
 s->videoEnded=TRUE;s->videoFrames=frames;s->videoLastPts=pts;g_cond_broadcast(&s->changed);return FL_GST_SESSION_READY;
}
FlGstSessionResult fl_gst_session_wait_video_eos(FlGstSession*s,guint64 generation,guint timeout,guint64*frames,gint64*pts){
 if(!valid(s)||!frames||!pts||timeout>60000)return FL_GST_SESSION_INVALID;
 const auto deadline=g_get_monotonic_time()+gint64(timeout)*1000;Lock lock(&s->mutex);
 while(true){if(s->cancelled)return FL_GST_SESSION_CANCELLED;
  if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
  if(!s->config.videoTrackId)return FL_GST_SESSION_INVALID;
  if(s->videoEnded){*frames=s->videoFrames;*pts=s->videoLastPts;return FL_GST_SESSION_READY;}
  if(!timeout||!g_cond_wait_until(&s->changed,&s->mutex,deadline))return FL_GST_SESSION_TIMEOUT;
 }
}

gboolean fl_gst_session_publish_events(FlGstSession*s,const flSessionConfig*c,GSocketConnection*stream,gboolean producer){
 if(!valid(s)||!c||!stream)return FALSE;Lock lock(&s->mutex);guint8 a[FL_SESSION_WIRE_SIZE],b[FL_SESSION_WIRE_SIZE];
 if(s->cancelled||!s->configured||*s->events||flSessionEncode(c,a,sizeof a)!=FL_SESSION_OK||flSessionEncode(&s->config,b,sizeof b)!=FL_SESSION_OK||memcmp(a,b,sizeof a))return FALSE;
 *s->events=std::make_shared<FlGstEventChannel>(stream,producer,c->generation,c->idleTimeoutMs);return TRUE;
}
static std::shared_ptr<FlGstEventChannel> eventChannel(FlGstSession*s,guint64 generation){
 if(!valid(s))return {};Lock lock(&s->mutex);if(s->cancelled||!s->configured||s->config.generation!=generation)return {};return *s->events;
}
gboolean fl_gst_session_has_event_stream(FlGstSession*s,guint64 generation){return bool(eventChannel(s,generation));}
FlGstSessionResult fl_gst_session_send_video_event(FlGstSession*s,guint64 generation,const FlGstVideoEvent*event){auto channel=eventChannel(s,generation);return channel&&event?channel->send(*event):FL_GST_SESSION_INVALID;}
FlGstSessionResult fl_gst_session_peek_video_end(FlGstSession*s,guint64 generation,FlGstVideoEvent*event){auto channel=eventChannel(s,generation);if(!channel||!event)return FL_GST_SESSION_INVALID;return channel->peekEnd(event)?FL_GST_SESSION_READY:FL_GST_SESSION_TIMEOUT;}
FlGstSessionResult fl_gst_session_receive_video_event(FlGstSession*s,guint64 generation,guint timeout,FlGstVideoEvent*event){auto channel=eventChannel(s,generation);return channel?channel->receive(timeout,event):FL_GST_SESSION_INVALID;}

#ifndef _WIN32
FlGstSessionResult fl_gst_session_bind_shared_cpu_fd(FlGstSession*s,gboolean producer,
 guint64 generation,int fd,guint timeout,const flMemoryRequirements*requirements,
 guint operations,const flSharedCpuDescriptor*desc,gsize count){
 struct Fd{int value;~Fd(){if(value>=0)close(value);}} owned{fd};
 if(!valid(s)||fd<0||!requirements||!timeout||timeout>60000||
    (producer?(desc||count):(!desc||count<2||count>FL_MAX_POOL_SIZE)))return FL_GST_SESSION_INVALID;
 flSessionConfig config{};
 {
  Lock lock(&s->mutex);
  if(s->cancelled)return FL_GST_SESSION_CANCELLED;
  if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
  if(!s->config.videoTrackId||s->cpuOpening||s->cpuChannel||s->cpuClaimed)return FL_GST_SESSION_INVALID;
  s->cpuShutdownFd=fcntl(fd,F_DUPFD_CLOEXEC,0);if(s->cpuShutdownFd<0)return FL_GST_SESSION_INVALID;
  s->cpuOpening=TRUE;config=s->config;
 }
 flSharedCpuChannel*channel=nullptr;
 owned.value=-1; // factories consume fd even on validation failure
 const guint flags=FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS;
 auto result=producer?flSharedCpuOpenProducerFdEx(fd,timeout,&config,requirements,operations,flags,&channel):
  flSharedCpuOpenConsumerFdEx(fd,timeout,&config,requirements,operations,desc,count,flags,&channel);
 Lock lock(&s->mutex);s->cpuOpening=FALSE;
 // Configure enforces a strictly newer generation. No successful old setup can
 // publish into a replacement generation, even if its peer accepted the pool.
 auto status=s->cancelled?FL_GST_SESSION_CANCELLED:
  (!s->configured||s->config.generation!=generation?FL_GST_SESSION_STALE:
   (result==FL_OK?FL_GST_SESSION_READY:FL_GST_SESSION_INVALID));
 if(status!=FL_GST_SESSION_READY){if(channel)flSharedCpuClose(channel);resetCpu(s);return status;}
 s->cpuChannel=channel;s->cpuProducer=!!producer;return FL_GST_SESSION_READY;
}
#else
static FlGstSessionResult bindCpuPipe(FlGstSession*s,gboolean producer,
 guint64 generation,void* pipe,void* peer,guint timeout,const flMemoryRequirements*requirements,
 guint operations,const flSharedCpuDescriptor*desc,gsize count,GBytes* secret,bool authenticate){
 struct Handle{HANDLE value;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} owned{pipe},cancel{nullptr};
 if(!valid(s)||!pipe||pipe==INVALID_HANDLE_VALUE||!peer||!requirements||!timeout||timeout>60000||
    (producer?(desc||count):(!desc||count<2||count>FL_MAX_POOL_SIZE)))return FL_GST_SESSION_INVALID;
 flSessionConfig config{};
 {
  Lock lock(&s->mutex);
  if(s->cancelled)return FL_GST_SESSION_CANCELLED;
  if(!s->configured||s->config.generation!=generation)return FL_GST_SESSION_STALE;
  if(!s->config.videoTrackId||s->cpuOpening||s->cpuChannel||s->cpuClaimed)return FL_GST_SESSION_INVALID;
  s->cpuCancelEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!s->cpuCancelEvent)return FL_GST_SESSION_INVALID;
  // Keep a local duplicate alive while the factory duplicates it outside the
  // session lock. Concurrent reset signals the same event but may close its handle.
  if(!DuplicateHandle(GetCurrentProcess(),s->cpuCancelEvent,GetCurrentProcess(),&cancel.value,0,FALSE,DUPLICATE_SAME_ACCESS)){
   resetCpu(s);return FL_GST_SESSION_INVALID;
  }
  s->cpuOpening=TRUE;config=s->config;
 }
 flSharedCpuChannel*channel=nullptr;
 auto result=authenticate?flAuthenticateCpuPipe(pipe,peer,cancel.value,timeout,config,producer,secret):FL_OK;
 const guint flags=FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS;
 if(result==FL_OK){owned.value=nullptr;
 result=producer?flSharedCpuOpenProducerPipe(pipe,peer,cancel.value,timeout,&config,requirements,operations,flags,&channel):
  flSharedCpuOpenConsumerPipe(pipe,peer,cancel.value,timeout,&config,requirements,operations,desc,count,flags,&channel);
 }
 Lock lock(&s->mutex);s->cpuOpening=FALSE;
 auto status=s->cancelled?FL_GST_SESSION_CANCELLED:
  (!s->configured||s->config.generation!=generation?FL_GST_SESSION_STALE:
   (result==FL_OK?FL_GST_SESSION_READY:FL_GST_SESSION_INVALID));
 if(status!=FL_GST_SESSION_READY){if(channel)flSharedCpuClose(channel);resetCpu(s);return status;}
 s->cpuChannel=channel;s->cpuProducer=!!producer;return FL_GST_SESSION_READY;
}
FlGstSessionResult fl_gst_session_bind_shared_cpu_pipe(FlGstSession*s,gboolean producer,
 guint64 generation,void* pipe,void* peer,guint timeout,const flMemoryRequirements* req,
 guint operations,const flSharedCpuDescriptor* desc,gsize count){
 return bindCpuPipe(s,producer,generation,pipe,peer,timeout,req,operations,desc,count,nullptr,false);
}
FlGstSessionResult fl_gst_session_bind_authenticated_cpu_pipe(FlGstSession*s,gboolean producer,
 guint64 generation,void* pipe,void* peer,GBytes* secret,guint timeout,const flMemoryRequirements* req,
 guint operations,const flSharedCpuDescriptor* desc,gsize count){
 return bindCpuPipe(s,producer,generation,pipe,peer,timeout,req,operations,desc,count,secret,true);
}
#endif
flSharedCpuChannel* fl_gst_session_claim_shared_cpu(FlGstSession*s,gboolean producer,guint64 generation){
 if(!valid(s))return nullptr;Lock lock(&s->mutex);
 if(s->cancelled||!s->configured||s->config.generation!=generation||s->cpuClaimed||
    s->cpuProducer!=!!producer||!s->cpuChannel)return nullptr;
 auto*channel=s->cpuChannel;s->cpuChannel=nullptr;s->cpuClaimed=TRUE;return channel;
}

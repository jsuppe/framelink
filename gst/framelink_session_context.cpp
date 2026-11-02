#define FL_GST_SESSION_BUILD
#include "framelink_session_context.h"
#include <cstring>
#include "fl_gst_session_internal.h"
struct _FlGstSession {
 GObject parent;
 GMutex mutex;
 GCond changed;
 guint required,registered,ready;
 gboolean configured,cancelled,mapped;
 flClockMapping mapping;
 guint64 maxUncertainty;
 GSocketConnection* audioConnection;
 gboolean audioProducer,audioClaimed;
 flSessionConfig config;
};
typedef struct { GObjectClass parent; } FlGstSessionClass;
G_DEFINE_TYPE(FlGstSession,fl_gst_session,G_TYPE_OBJECT)
static bool valid(FlGstSession* s) {return s&&G_TYPE_CHECK_INSTANCE_TYPE(s,fl_gst_session_get_type());}
static void finalize(GObject* object) {
 auto* s=reinterpret_cast<FlGstSession*>(object);
 if(s->audioConnection)g_object_unref(s->audioConnection);
 g_cond_clear(&s->changed);g_mutex_clear(&s->mutex);
 G_OBJECT_CLASS(fl_gst_session_parent_class)->finalize(object);
}
static void fl_gst_session_class_init(FlGstSessionClass* c){G_OBJECT_CLASS(c)->finalize=finalize;}
static void fl_gst_session_init(FlGstSession* s){g_mutex_init(&s->mutex);g_cond_init(&s->changed);}
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
 if(s->audioConnection){g_socket_shutdown(g_socket_connection_get_socket(s->audioConnection),TRUE,TRUE,nullptr);g_object_unref(s->audioConnection);s->audioConnection=nullptr;}
 s->audioClaimed=FALSE;s->config=*c;s->mapped=FALSE;s->configured=TRUE;s->cancelled=FALSE;s->ready=0;g_cond_broadcast(&s->changed);return FL_GST_SESSION_READY;
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

#include "framelink_session_transport.h"
#include "fl_gst_audio_startup.hpp"
#include <gst/gst.h>
#include <cstring>
#include <initializer_list>

typedef struct {
 GstBin parent;
 GstElement *filter,*queue,*pay,*writer;
 FlGstSession* session;
 GSocketConnection* connection;
 std::shared_ptr<FlAudioStartup>* startup;
 gchar* sessionPath;
 gchar* pcmFormat;
 gchar* tracks;
 guint rate,channels,queueMs,connectMs,idleMs;
 gboolean localSession;
 flSessionConfig config;
 gboolean registered,failed,haveSegment;
 GstSegment segment;
 guint64 blocks,bytes,samples,totalSamples,origin;
} GstFrameLinkAudioSink;
typedef struct {GstBinClass parent;} GstFrameLinkAudioSinkClass;
G_DEFINE_TYPE(GstFrameLinkAudioSink,gst_framelink_audio_sink,GST_TYPE_BIN)
static GstStaticPadTemplate sinkTemplate=GST_STATIC_PAD_TEMPLATE("sink",GST_PAD_SINK,GST_PAD_ALWAYS,
 GST_STATIC_CAPS("audio/x-raw,format=(string){S16LE,F32LE},layout=(string)interleaved,rate=(int)[8000,192000],channels=(int){1,2}"));
static GstPadProbeReturn reject(GstFrameLinkAudioSink*s,const char*why){
 if(!s->failed){s->failed=TRUE;GST_ELEMENT_ERROR(s,STREAM,FORMAT,("%s",why),("FrameLink audio rejected before serialization"));fl_gst_session_cancel(s->session);}
 return GST_PAD_PROBE_DROP;
}
static GstPadProbeReturn inspect(GstPad*,GstPadProbeInfo*info,gpointer data){
 auto*s=static_cast<GstFrameLinkAudioSink*>(data);
 if(s->failed)return GST_PAD_PROBE_DROP;
 if(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM){
  auto*event=GST_PAD_PROBE_INFO_EVENT(info);
  if(GST_EVENT_TYPE(event)==GST_EVENT_CAPS){
   GstCaps*caps=nullptr;gst_event_parse_caps(event,&caps);auto*st=gst_caps_get_structure(caps,0);
   const GValue*mask=gst_structure_get_value(st,"channel-mask");
   if(mask&&(!GST_VALUE_HOLDS_BITMASK(mask)||(s->config.audio.layout==2?gst_value_get_bitmask(mask)!=3:(gst_value_get_bitmask(mask)!=0&&gst_value_get_bitmask(mask)!=4))))return reject(s,"unsupported PCM channel mask");
  }else if(GST_EVENT_TYPE(event)==GST_EVENT_SEGMENT){const GstSegment*seg=nullptr;gst_event_parse_segment(event,&seg);
   if(seg->format!=GST_FORMAT_TIME||seg->rate!=1.0||seg->applied_rate!=1.0)return reject(s,"only forward rate1 TIME segments supported");
   s->segment=*seg;s->haveSegment=TRUE;
  }else if(GST_EVENT_TYPE(event)==GST_EVENT_EOS){
   gst_element_post_message(GST_ELEMENT(s),gst_message_new_element(GST_OBJECT(s),gst_structure_new("framelink-audio-sink-stats",
    "blocks",G_TYPE_UINT64,s->blocks,"pcm-bytes",G_TYPE_UINT64,s->bytes,"sample-frames",G_TYPE_UINT64,s->totalSamples,
    "generation",G_TYPE_UINT64,s->config.generation,"shared-pcm",G_TYPE_BOOLEAN,FALSE,nullptr)));
  }
  return GST_PAD_PROBE_OK;
 }
 auto*b=GST_PAD_PROBE_INFO_BUFFER(info);if(!b)return GST_PAD_PROBE_OK;
 if(*s->startup){GError*error=nullptr;bool ok=(*s->startup)->wait(nullptr,&error);
  if(!ok){auto result=reject(s,error?error->message:"audio startup failed");g_clear_error(&error);return result;}
  if(fl_gst_session_get_config(s->session,&s->config)!=FL_GST_SESSION_READY)return reject(s,"audio session cancelled");
 }
 if(!s->session||!s->haveSegment||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(b))||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(b)))return reject(s,"PCM requires valid PTS, duration and TIME segment");
 auto running=gst_segment_to_running_time(&s->segment,GST_FORMAT_TIME,GST_BUFFER_PTS(b));
 if(!GST_CLOCK_TIME_IS_VALID(running)||running>G_MAXINT64||GST_BUFFER_DURATION(b)>GST_CLOCK_TIME_NONE-1-running)return reject(s,"PCM is outside its TIME segment");
 const guint bpf=(s->config.audio.pcm==FL_PCM_S16?2u:4u)*s->config.audio.layout;
 const guint64 size=gst_buffer_get_size(b),count=size/bpf;
 const guint64 budget=((guint64(s->config.audio.rate)*s->config.audioQueueMs+999)/1000)*bpf;
 if(!size||size%bpf||size>budget)return reject(s,"PCM block is misaligned, empty or exceeds bounded queue budget");
 if(GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT)){s->samples=0;s->origin=GST_CLOCK_TIME_NONE;}
 if(s->origin==GST_CLOCK_TIME_NONE)s->origin=running;
 gint64 first=0,end=0;
 if(count>G_MAXUINT64-s->samples||flSessionSampleOffset(s->samples,s->config.audio.rate,&first)!=FL_SESSION_OK||flSessionSampleOffset(s->samples+count,s->config.audio.rate,&end)!=FL_SESSION_OK||guint64(first)>GST_CLOCK_TIME_NONE-1-s->origin||running!=s->origin+guint64(first)||GST_BUFFER_DURATION(b)!=guint64(end-first))return reject(s,"PCM sample duration/continuity mismatch");
 flSessionConfig ready{};
 if(fl_gst_session_wait(s->session,s->config.generation,s->config.connectTimeoutMs,&ready)!=FL_GST_SESSION_READY)return reject(s,"required session tracks not ready or cancelled");
 s->samples+=count;s->totalSamples+=count;++s->blocks;s->bytes+=size;return GST_PAD_PROBE_OK;
}
static void setContext(GstElement*element,GstContext*context){
 auto*s=reinterpret_cast<GstFrameLinkAudioSink*>(element);
 if(gst_context_has_context_type(context,FL_GST_SESSION_CONTEXT)){
  auto*session=fl_gst_session_from_context(context);
  if(session){
   if(GST_STATE(element)!=GST_STATE_NULL){g_object_unref(session);GST_ELEMENT_ERROR(s,CORE,STATE_CHANGE,("session context is immutable outside NULL"),(nullptr));return;}
   if(s->session!=session){if(s->session)g_object_unref(s->session);s->session=session;s->registered=FALSE;}
   else g_object_unref(session);
  }
 }
 GST_ELEMENT_CLASS(gst_framelink_audio_sink_parent_class)->set_context(element,context);
}
static GstStateChangeReturn changeState(GstElement*element,GstStateChange transition){
 auto*s=reinterpret_cast<GstFrameLinkAudioSink*>(element);
 if(transition==GST_STATE_CHANGE_PAUSED_TO_READY){if(*s->startup)(*s->startup)->cancel();else if(s->session)fl_gst_session_cancel(s->session);}
 if(transition==GST_STATE_CHANGE_NULL_TO_READY){
  if(!s->filter||!s->queue||!s->pay||!s->writer||!s->tracks||(strcmp(s->tracks,"audio")&&strcmp(s->tracks,"av"))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("unsupported tracks or missing GDP/GIO elements"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(!s->session&&s->sessionPath&&*s->sessionPath&&(!s->pcmFormat||(strcmp(s->pcmFormat,"S16LE")&&strcmp(s->pcmFormat,"F32LE"))||strcmp(s->tracks,"audio"))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("standalone mode requires audio tracks and supported PCM format"),("coordinated A/V requires a shared context"));return GST_STATE_CHANGE_FAILURE;}
  if(!s->session&&s->sessionPath&&*s->sessionPath){
   s->session=fl_gst_session_new(FL_GST_TRACK_AUDIO);s->localSession=TRUE;
   s->config={};s->config.audioTrackId=2;s->config.audio={guint(s->pcmFormat&&!strcmp(s->pcmFormat,"F32LE")?FL_PCM_F32:FL_PCM_S16),s->rate,s->channels};
   s->config.audioQueueMs=s->queueMs;s->config.connectTimeoutMs=s->connectMs;s->config.idleTimeoutMs=s->idleMs;
   *s->startup=std::make_shared<FlAudioStartup>(s->session,element,s->sessionPath,s->config.audio,s->queueMs,s->connectMs,s->idleMs);
  }
  if(!s->filter||!s->queue||!s->pay||!s->writer||!s->session||(!s->localSession&&fl_gst_session_get_config(s->session,&s->config)!=FL_GST_SESSION_READY)||!s->config.audioTrackId){
   GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("authenticated FrameLink audio session context required"),("GDP/GIO/core plugins must be installed"));return GST_STATE_CHANGE_FAILURE;
  }
  if(!s->localSession&&bool(s->config.videoTrackId)!=(!strcmp(s->tracks,"av"))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("configured session does not match requested tracks"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(!s->localSession)s->connection=fl_gst_session_claim_audio_stream(s->session,TRUE,s->config.generation);
  if(!s->localSession&&!s->connection){GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("no unclaimed authenticated producer audio stream"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(!s->localSession&&!s->registered){if(fl_gst_session_register_track(s->session,FL_GST_TRACK_AUDIO)!=FL_GST_SESSION_READY){fl_gst_session_cancel(s->session);g_clear_object(&s->connection);return GST_STATE_CHANGE_FAILURE;}s->registered=TRUE;}
  auto*caps=gst_caps_new_simple("audio/x-raw","format",G_TYPE_STRING,s->config.audio.pcm==FL_PCM_S16?"S16LE":"F32LE",
   "layout",G_TYPE_STRING,"interleaved","rate",G_TYPE_INT,int(s->config.audio.rate),"channels",G_TYPE_INT,int(s->config.audio.layout),nullptr);
  if(s->config.audio.layout==2)gst_caps_set_simple(caps,"channel-mask",GST_TYPE_BITMASK,guint64(3),nullptr);
  g_object_set(s->filter,"caps",caps,nullptr);gst_caps_unref(caps);
  guint64 bytes=((guint64(s->config.audio.rate)*s->config.audioQueueMs+999)/1000)*(s->config.audio.pcm==FL_PCM_S16?2u:4u)*s->config.audio.layout;
  g_object_set(s->queue,"max-size-buffers",0u,"max-size-bytes",guint(bytes),"max-size-time",guint64(s->config.audioQueueMs)*GST_MSECOND,nullptr);
  if(s->connection)g_socket_set_timeout(g_socket_connection_get_socket(s->connection),(s->config.idleTimeoutMs+999)/1000);
  auto*output=s->localSession?fl_deferred_audio_output(*s->startup):G_OUTPUT_STREAM(g_object_ref(g_io_stream_get_output_stream(G_IO_STREAM(s->connection))));
  g_object_set(s->writer,"stream",output,"sync",FALSE,"async",FALSE,"close-on-stop",FALSE,"enable-last-sample",FALSE,nullptr);
  g_object_unref(output);
  s->failed=FALSE;s->haveSegment=FALSE;s->blocks=s->bytes=s->samples=s->totalSamples=0;s->origin=GST_CLOCK_TIME_NONE;
  if(!s->localSession&&fl_gst_session_track_ready(s->session,FL_GST_TRACK_AUDIO,s->config.generation)!=FL_GST_SESSION_READY){fl_gst_session_cancel(s->session);g_object_set(s->writer,"stream",nullptr,nullptr);g_clear_object(&s->connection);return GST_STATE_CHANGE_FAILURE;}
 }
 auto result=GST_ELEMENT_CLASS(gst_framelink_audio_sink_parent_class)->change_state(element,transition);
 if(transition==GST_STATE_CHANGE_READY_TO_NULL||(transition==GST_STATE_CHANGE_NULL_TO_READY&&result==GST_STATE_CHANGE_FAILURE)){
  if(*s->startup){(*s->startup)->cancel();g_object_set(s->writer,"stream",nullptr,nullptr);s->startup->reset();}
  if(s->connection){fl_gst_session_cancel(s->session);g_object_set(s->writer,"stream",nullptr,nullptr);g_clear_object(&s->connection);}
  if(s->localSession){g_clear_object(&s->session);s->localSession=FALSE;}
 }
 return result;
}
static void finalize(GObject*object){auto*s=reinterpret_cast<GstFrameLinkAudioSink*>(object);
 if(*s->startup)(*s->startup)->cancel();delete s->startup;
 if(s->connection){fl_gst_session_cancel(s->session);g_object_unref(s->connection);}if(s->session)g_object_unref(s->session);
 g_free(s->sessionPath);g_free(s->pcmFormat);g_free(s->tracks);
 G_OBJECT_CLASS(gst_framelink_audio_sink_parent_class)->finalize(object);
}
enum { PROP_NONE,PROP_SESSION,PROP_FORMAT,PROP_RATE,PROP_CHANNELS,PROP_QUEUE,PROP_CONNECT,PROP_IDLE,PROP_TRACKS,PROP_ID,PROP_GENERATION,PROP_AUDIO_ID,PROP_VIDEO_ID };
static void setProperty(GObject*object,guint property,const GValue*value,GParamSpec*spec){
 auto*s=reinterpret_cast<GstFrameLinkAudioSink*>(object);
 if(GST_STATE(s)!=GST_STATE_NULL){GST_WARNING_OBJECT(s,"configuration is immutable outside NULL");return;}
 switch(property){
 case PROP_SESSION:g_free(s->sessionPath);s->sessionPath=g_value_dup_string(value);break;
 case PROP_FORMAT:g_free(s->pcmFormat);s->pcmFormat=g_value_dup_string(value);break;
 case PROP_TRACKS:g_free(s->tracks);s->tracks=g_value_dup_string(value);break;
 case PROP_RATE:s->rate=g_value_get_uint(value);break;case PROP_CHANNELS:s->channels=g_value_get_uint(value);break;
 case PROP_QUEUE:s->queueMs=g_value_get_uint(value);break;case PROP_CONNECT:s->connectMs=g_value_get_uint(value);break;case PROP_IDLE:s->idleMs=g_value_get_uint(value);break;
 default:G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property,spec);
 }
}
static void getProperty(GObject*object,guint property,GValue*value,GParamSpec*spec){auto*s=reinterpret_cast<GstFrameLinkAudioSink*>(object);
 flSessionConfig selected{};if(s->session)fl_gst_session_get_config(s->session,&selected);
 switch(property){case PROP_TRACKS:g_value_set_string(value,s->tracks);break;
 case PROP_GENERATION:g_value_set_uint64(value,selected.generation);break;
 case PROP_AUDIO_ID:g_value_set_uint(value,selected.audioTrackId);break;case PROP_VIDEO_ID:g_value_set_uint(value,selected.videoTrackId);break;
 case PROP_ID:{gchar id[33]{};if(selected.generation)for(unsigned i=0;i<16;i++)g_snprintf(id+2*i,3,"%02x",selected.sessionId.bytes[i]);g_value_set_string(value,id);break;}
 case PROP_SESSION:g_value_set_string(value,s->sessionPath);break;case PROP_FORMAT:g_value_set_string(value,s->pcmFormat);break;
 case PROP_RATE:g_value_set_uint(value,s->rate);break;case PROP_CHANNELS:g_value_set_uint(value,s->channels);break;case PROP_QUEUE:g_value_set_uint(value,s->queueMs);break;
 case PROP_CONNECT:g_value_set_uint(value,s->connectMs);break;case PROP_IDLE:g_value_set_uint(value,s->idleMs);break;default:G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property,spec);}
}
static void gst_framelink_audio_sink_class_init(GstFrameLinkAudioSinkClass*c){
 auto*object=G_OBJECT_CLASS(c);object->set_property=setProperty;object->get_property=getProperty;
 const auto flags=GParamFlags(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS);
 g_object_class_install_property(object,PROP_TRACKS,g_param_spec_string("tracks","Tracks","audio standalone or av with shared session context","audio",flags));
 const auto read=GParamFlags(G_PARAM_READABLE|G_PARAM_STATIC_STRINGS);
 g_object_class_install_property(object,PROP_ID,g_param_spec_string("session-id","Session identity","Negotiated identity, not a secret",nullptr,read));
 g_object_class_install_property(object,PROP_GENERATION,g_param_spec_uint64("generation","Generation","Negotiated session generation",0,G_MAXUINT64,0,read));
 g_object_class_install_property(object,PROP_AUDIO_ID,g_param_spec_uint("audio-track-id","Audio track","Negotiated audio identity",0,G_MAXUINT,0,read));
 g_object_class_install_property(object,PROP_VIDEO_ID,g_param_spec_uint("video-track-id","Video track","Negotiated video identity, zero when absent",0,G_MAXUINT,0,read));
 g_object_class_install_property(object,PROP_SESSION,g_param_spec_string("session","Session","Private local audio-only rendezvous path, or use an authenticated context",nullptr,flags));
 g_object_class_install_property(object,PROP_FORMAT,g_param_spec_string("format","PCM format","Standalone S16LE or F32LE","S16LE",flags));
 g_object_class_install_property(object,PROP_RATE,g_param_spec_uint("rate","Sample rate","Standalone PCM sample rate",8000,192000,48000,flags));
 g_object_class_install_property(object,PROP_CHANNELS,g_param_spec_uint("channels","Channels","Standalone mono or stereo",1,2,2,flags));
 g_object_class_install_property(object,PROP_QUEUE,g_param_spec_uint("audio-queue-ms","Audio queue","Hard queue budget, not target latency",1,2000,80,flags));
 g_object_class_install_property(object,PROP_CONNECT,g_param_spec_uint("connect-timeout-ms","Connect timeout","Whole standalone startup deadline",1,60000,15000,flags));
 g_object_class_install_property(object,PROP_IDLE,g_param_spec_uint("idle-timeout-ms","Idle timeout","Bounded transport idle deadline",1,60000,15000,flags));
 auto*e=GST_ELEMENT_CLASS(c);e->set_context=setContext;e->change_state=changeState;G_OBJECT_CLASS(c)->finalize=finalize;
 gst_element_class_set_static_metadata(e,"FrameLink audio producer","Sink/Audio","Bounded authenticated PCM transport (serialized, not shared memory)","FrameLink");
 gst_element_class_add_static_pad_template(e,&sinkTemplate);
}
static void gst_framelink_audio_sink_init(GstFrameLinkAudioSink*s){
 s->startup=new std::shared_ptr<FlAudioStartup>();s->tracks=g_strdup("audio");s->pcmFormat=g_strdup("S16LE");s->rate=48000;s->channels=2;s->queueMs=80;s->connectMs=s->idleMs=15000;
 s->filter=gst_element_factory_make("capsfilter",nullptr);s->queue=gst_element_factory_make("queue",nullptr);
 s->pay=gst_element_factory_make("gdppay",nullptr);s->writer=gst_element_factory_make("giostreamsink",nullptr);
 // Missing factories must fail at state change, not crash plugin discovery.
 if(!s->filter||!s->queue||!s->pay||!s->writer){for(auto*e:{s->filter,s->queue,s->pay,s->writer})if(e)gst_object_unref(e);s->filter=s->queue=s->pay=s->writer=nullptr;return;}
 gst_bin_add_many(GST_BIN(s),s->filter,s->queue,s->pay,s->writer,nullptr);
 // GDP's outer byte segment requests seeks on GIO. Socket output is not seekable;
 // adapt only this serialized transport envelope, never the PCM segment/PTS
 // inside GDP. Same requirement as the established sample transport.
 auto*wirePad=gst_element_get_static_pad(s->writer,"sink");
 gst_pad_add_probe(wirePad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
  [](GstPad*,GstPadProbeInfo*info,gpointer)->GstPadProbeReturn{
   auto*event=GST_PAD_PROBE_INFO_EVENT(info);if(GST_EVENT_TYPE(event)==GST_EVENT_SEGMENT){
    GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);auto*replacement=gst_event_new_segment(&segment);
    gst_event_set_seqnum(replacement,gst_event_get_seqnum(event));gst_event_unref(event);GST_PAD_PROBE_INFO_DATA(info)=replacement;
   }return GST_PAD_PROBE_OK;
  },nullptr,nullptr);gst_object_unref(wirePad);
 if(!gst_element_link_many(s->filter,s->queue,s->pay,s->writer,nullptr)){s->failed=TRUE;return;}
 // GDP forwards EOS locally without serializing it. Send an explicit
 // terminal record after queue drain, then preserve local EOS. GDP stores event
 // types in 16 bits; the receiver recognizes this private record by name/schema.
 auto*terminal=gst_element_get_static_pad(s->pay,"sink");
 gst_pad_add_probe(terminal,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
  [](GstPad*pad,GstPadProbeInfo*info,gpointer data)->GstPadProbeReturn{
   auto*self=static_cast<GstFrameLinkAudioSink*>(data);
   if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info))==GST_EVENT_EOS&&!self->failed){
    auto*marker=gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,gst_structure_new("framelink-audio-eos",
     "version",G_TYPE_UINT,1u,"generation",G_TYPE_UINT64,self->config.generation,
     "blocks",G_TYPE_UINT64,self->blocks,"pcm-bytes",G_TYPE_UINT64,self->bytes,nullptr));
    if(!gst_pad_send_event(pad,marker))return reject(self,"audio terminal record could not be sent");
   }return GST_PAD_PROBE_OK;
  },s,nullptr);gst_object_unref(terminal);
 auto*pad=gst_element_get_static_pad(s->filter,"sink");
 gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),inspect,s,nullptr);
 auto*ghost=gst_ghost_pad_new_from_template("sink",pad,gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(s),"sink"));gst_object_unref(pad);gst_element_add_pad(GST_ELEMENT(s),ghost);
}
gboolean fl_register_audio_sink(GstPlugin*plugin){return gst_element_register(plugin,"framelinkaudiosink",GST_RANK_NONE,gst_framelink_audio_sink_get_type());}

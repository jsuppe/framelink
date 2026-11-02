#include "framelink_session_transport.h"
#include <gst/gst.h>
#include <cstring>
#include <initializer_list>

typedef struct {
 GstBin parent;
 GstElement *filter,*queue,*pay,*writer;
 FlGstSession* session;
 GSocketConnection* connection;
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
 if(!s->session||!s->haveSegment||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(b))||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(b)))return reject(s,"PCM requires valid PTS, duration and TIME segment");
 auto running=gst_segment_to_running_time(&s->segment,GST_FORMAT_TIME,GST_BUFFER_PTS(b));
 if(!GST_CLOCK_TIME_IS_VALID(running)||GST_BUFFER_DURATION(b)>GST_CLOCK_TIME_NONE-1-running)return reject(s,"PCM is outside its TIME segment");
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
 if(transition==GST_STATE_CHANGE_NULL_TO_READY){
  if(!s->filter||!s->queue||!s->pay||!s->writer||!s->session||fl_gst_session_get_config(s->session,&s->config)!=FL_GST_SESSION_READY||!s->config.audioTrackId){
   GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("authenticated FrameLink audio session context required"),("GDP/GIO/core plugins must be installed"));return GST_STATE_CHANGE_FAILURE;
  }
  s->connection=fl_gst_session_claim_audio_stream(s->session,TRUE,s->config.generation);
  if(!s->connection){GST_ELEMENT_ERROR(s,RESOURCE,OPEN_WRITE,("no unclaimed authenticated producer audio stream"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(!s->registered){if(fl_gst_session_register_track(s->session,FL_GST_TRACK_AUDIO)!=FL_GST_SESSION_READY){fl_gst_session_cancel(s->session);g_clear_object(&s->connection);return GST_STATE_CHANGE_FAILURE;}s->registered=TRUE;}
  auto*caps=gst_caps_new_simple("audio/x-raw","format",G_TYPE_STRING,s->config.audio.pcm==FL_PCM_S16?"S16LE":"F32LE",
   "layout",G_TYPE_STRING,"interleaved","rate",G_TYPE_INT,int(s->config.audio.rate),"channels",G_TYPE_INT,int(s->config.audio.layout),nullptr);
  if(s->config.audio.layout==2)gst_caps_set_simple(caps,"channel-mask",GST_TYPE_BITMASK,guint64(3),nullptr);
  g_object_set(s->filter,"caps",caps,nullptr);gst_caps_unref(caps);
  guint64 bytes=((guint64(s->config.audio.rate)*s->config.audioQueueMs+999)/1000)*(s->config.audio.pcm==FL_PCM_S16?2u:4u)*s->config.audio.layout;
  g_object_set(s->queue,"max-size-buffers",0u,"max-size-bytes",guint(bytes),"max-size-time",guint64(s->config.audioQueueMs)*GST_MSECOND,nullptr);
  g_socket_set_timeout(g_socket_connection_get_socket(s->connection),(s->config.idleTimeoutMs+999)/1000);
  g_object_set(s->writer,"stream",g_io_stream_get_output_stream(G_IO_STREAM(s->connection)),"sync",FALSE,"async",FALSE,"close-on-stop",FALSE,"enable-last-sample",FALSE,nullptr);
  s->failed=FALSE;s->haveSegment=FALSE;s->blocks=s->bytes=s->samples=s->totalSamples=0;s->origin=GST_CLOCK_TIME_NONE;
  if(fl_gst_session_track_ready(s->session,FL_GST_TRACK_AUDIO,s->config.generation)!=FL_GST_SESSION_READY)return GST_STATE_CHANGE_FAILURE;
 }
 auto result=GST_ELEMENT_CLASS(gst_framelink_audio_sink_parent_class)->change_state(element,transition);
 if(transition==GST_STATE_CHANGE_READY_TO_NULL||(transition==GST_STATE_CHANGE_NULL_TO_READY&&result==GST_STATE_CHANGE_FAILURE)){
  if(s->connection){fl_gst_session_cancel(s->session);g_object_set(s->writer,"stream",nullptr,nullptr);g_clear_object(&s->connection);}
 }
 return result;
}
static void finalize(GObject*object){auto*s=reinterpret_cast<GstFrameLinkAudioSink*>(object);
 if(s->connection){fl_gst_session_cancel(s->session);g_object_unref(s->connection);}if(s->session)g_object_unref(s->session);
 G_OBJECT_CLASS(gst_framelink_audio_sink_parent_class)->finalize(object);
}
static void gst_framelink_audio_sink_class_init(GstFrameLinkAudioSinkClass*c){
 auto*e=GST_ELEMENT_CLASS(c);e->set_context=setContext;e->change_state=changeState;G_OBJECT_CLASS(c)->finalize=finalize;
 gst_element_class_set_static_metadata(e,"FrameLink audio producer","Sink/Audio","Bounded authenticated PCM transport (serialized, not shared memory)","FrameLink");
 gst_element_class_add_static_pad_template(e,&sinkTemplate);
}
static void gst_framelink_audio_sink_init(GstFrameLinkAudioSink*s){
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
 auto*pad=gst_element_get_static_pad(s->filter,"sink");
 gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),inspect,s,nullptr);
 auto*ghost=gst_ghost_pad_new_from_template("sink",pad,gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(s),"sink"));gst_object_unref(pad);gst_element_add_pad(GST_ELEMENT(s),ghost);
}
gboolean fl_register_audio_sink(GstPlugin*plugin){return gst_element_register(plugin,"framelinkaudiosink",GST_RANK_NONE,gst_framelink_audio_sink_get_type());}

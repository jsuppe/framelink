#include "fl_gst_audio_startup.hpp"
#include <gst/base/gstpushsrc.h>
#include <cstring>
#include <algorithm>

// Private byte reader: g_input_stream_read returns available bytes, not a full
// requested block. A small final GDP packet must not wait for later PCM or EOF.
typedef struct {
 GstPushSrc parent; FlGstSession* session; GSocketConnection* connection;
 std::shared_ptr<FlAudioStartup>* startup; GCancellable* cancel; gint eos;
 guint8 header[62];guint headerUsed,payloadRemaining;
} FlAudioReader;
typedef struct {GstPushSrcClass parent;} FlAudioReaderClass;
G_DEFINE_TYPE(FlAudioReader,fl_audio_reader,GST_TYPE_PUSH_SRC)
static GstStaticPadTemplate wireTemplate=GST_STATIC_PAD_TEMPLATE("src",GST_PAD_SRC,GST_PAD_ALWAYS,GST_STATIC_CAPS("application/x-gdp"));
// Validate declared payload sizes before gdpdepay can accumulate them. GDP
// itself still verifies the full header/payload CRC and reconstructs metadata.
static bool boundedWire(FlAudioReader*s,const guint8*data,gsize size){
 flSessionConfig c{};if(fl_gst_session_get_config(s->session,&c)!=FL_GST_SESSION_READY)return false;
 const guint64 bpf=(c.audio.pcm==FL_PCM_S16?2u:4u)*c.audio.layout;
 const guint64 budget=((guint64(c.audio.rate)*c.audioQueueMs+999)/1000)*bpf;
 while(size){
  if(s->payloadRemaining){auto n=std::min<gsize>(size,s->payloadRemaining);s->payloadRemaining-=guint(n);data+=n;size-=n;continue;}
  auto n=std::min<gsize>(size,sizeof(s->header)-s->headerUsed);memcpy(s->header+s->headerUsed,data,n);s->headerUsed+=guint(n);data+=n;size-=n;
  if(s->headerUsed==sizeof(s->header)){
   guint type=GST_READ_UINT16_BE(s->header+4),length=GST_READ_UINT32_BE(s->header+6);
   if(s->header[0]!=1||s->header[1]!=0||(type==1?(!length||length%bpf||length>budget):(length>65536||type<2)))return false;
   s->headerUsed=0;s->payloadRemaining=length;
  }
 }
 return true;
}
static GstFlowReturn readChunk(GstPushSrc*base,GstBuffer**out){
 auto*s=reinterpret_cast<FlAudioReader*>(base);GError*error=nullptr;
 if(g_atomic_int_get(&s->eos))return GST_FLOW_EOS;
 if(!s->connection){
  if(*s->startup)s->connection=(*s->startup)->connection(s->cancel,&error);
  else {flSessionConfig config{};if(fl_gst_session_get_config(s->session,&config)==FL_GST_SESSION_READY){
   if(fl_gst_session_wait(s->session,config.generation,config.connectTimeoutMs,&config)==FL_GST_SESSION_READY)
    s->connection=fl_gst_session_claim_audio_stream(s->session,FALSE,config.generation);
  }}
  if(!s->connection){if(g_cancellable_is_cancelled(s->cancel)){g_clear_error(&error);return GST_FLOW_FLUSHING;}
   GST_ELEMENT_ERROR(s,RESOURCE,OPEN_READ,("%s",error?error->message:"no authenticated consumer audio stream"),(nullptr));g_clear_error(&error);return GST_FLOW_ERROR;}
  flSessionConfig config{};fl_gst_session_get_config(s->session,&config);
  g_socket_set_timeout(g_socket_connection_get_socket(s->connection),(config.idleTimeoutMs+999)/1000);
 }
 auto*b=gst_buffer_new_allocate(nullptr,4096,nullptr);GstMapInfo map{};gst_buffer_map(b,&map,GST_MAP_WRITE);
 auto count=g_input_stream_read(g_io_stream_get_input_stream(G_IO_STREAM(s->connection)),map.data,map.size,s->cancel,&error);
 bool bounded=count<=0||boundedWire(s,map.data,gsize(count));gst_buffer_unmap(b,&map);
 if(!bounded){gst_buffer_unref(b);GST_ELEMENT_ERROR(s,STREAM,FORMAT,("GDP payload exceeds negotiated bounds or invalid wire header"),(nullptr));fl_gst_session_cancel(s->session);return GST_FLOW_ERROR;}
 if(count<=0){gst_buffer_unref(b);
  if(g_cancellable_is_cancelled(s->cancel)){g_clear_error(&error);return GST_FLOW_FLUSHING;}
  if(count==0&&g_atomic_int_get(&s->eos)){g_clear_error(&error);return GST_FLOW_EOS;}
  GST_ELEMENT_ERROR(s,RESOURCE,READ,("%s",error?error->message:"audio peer closed before GDP EOS"),(nullptr));g_clear_error(&error);return GST_FLOW_ERROR;
 }
 gst_buffer_resize(b,0,count);*out=b;return GST_FLOW_OK;
}
static gboolean unlockReader(GstBaseSrc*base){auto*s=reinterpret_cast<FlAudioReader*>(base);g_cancellable_cancel(s->cancel);if(s->session)fl_gst_session_cancel(s->session);return TRUE;}
static gboolean unlockStopReader(GstBaseSrc*){return TRUE;}
static gboolean startReader(GstBaseSrc*base){auto*s=reinterpret_cast<FlAudioReader*>(base);g_cancellable_reset(s->cancel);g_atomic_int_set(&s->eos,0);s->headerUsed=s->payloadRemaining=0;return TRUE;}
static gboolean stopReader(GstBaseSrc*base){auto*s=reinterpret_cast<FlAudioReader*>(base);g_clear_object(&s->connection);return TRUE;}
static void finalizeReader(GObject*object){auto*s=reinterpret_cast<FlAudioReader*>(object);g_clear_object(&s->connection);g_clear_object(&s->session);g_object_unref(s->cancel);delete s->startup;G_OBJECT_CLASS(fl_audio_reader_parent_class)->finalize(object);}
static void fl_audio_reader_class_init(FlAudioReaderClass*c){auto*b=GST_BASE_SRC_CLASS(c);b->start=startReader;b->stop=stopReader;b->unlock=unlockReader;b->unlock_stop=unlockStopReader;GST_PUSH_SRC_CLASS(c)->create=readChunk;G_OBJECT_CLASS(c)->finalize=finalizeReader;
 gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(c),"FrameLink incremental audio reader","Source","Private GDP transport","FrameLink");gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(c),&wireTemplate);}
static void fl_audio_reader_init(FlAudioReader*s){s->cancel=g_cancellable_new();s->startup=new std::shared_ptr<FlAudioStartup>();gst_base_src_set_live(GST_BASE_SRC(s),TRUE);gst_base_src_set_format(GST_BASE_SRC(s),GST_FORMAT_BYTES);gst_base_src_set_do_timestamp(GST_BASE_SRC(s),FALSE);}

typedef struct {
 GstBin parent; FlAudioReader* reader; GstElement* depay; FlGstSession* session;
 gchar *path,*format,*tracks;guint rate,channels,queueMs,connectMs,idleMs;
 gboolean local,failed,segment; guint64 blocks,bytes;
} GstFrameLinkAudioSrc;
typedef struct {GstBinClass parent;} GstFrameLinkAudioSrcClass;
G_DEFINE_TYPE(GstFrameLinkAudioSrc,gst_framelink_audio_src,GST_TYPE_BIN)
static GstStaticPadTemplate pcmTemplate=GST_STATIC_PAD_TEMPLATE("src",GST_PAD_SRC,GST_PAD_ALWAYS,GST_STATIC_CAPS("audio/x-raw,format=(string){S16LE,F32LE},layout=(string)interleaved,rate=(int)[8000,192000],channels=(int){1,2}"));
static GstPadProbeReturn badInput(GstFrameLinkAudioSrc*s,const char*why){if(!s->failed){s->failed=TRUE;GST_ELEMENT_ERROR(s,STREAM,FORMAT,("%s",why),("FrameLink consumer rejected received PCM"));fl_gst_session_cancel(s->session);}return GST_PAD_PROBE_DROP;}
static GstPadProbeReturn inspectPcm(GstPad*,GstPadProbeInfo*info,gpointer data){auto*s=static_cast<GstFrameLinkAudioSrc*>(data);if(s->failed)return GST_PAD_PROBE_DROP;
 flSessionConfig c{};if(fl_gst_session_get_config(s->session,&c)!=FL_GST_SESSION_READY)return badInput(s,"audio session cancelled");
 if(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM){auto*e=GST_PAD_PROBE_INFO_EVENT(info);
  if(gst_event_has_name(e,"framelink-audio-eos")){
   auto*st=gst_event_get_structure(e);guint version=0;guint64 generation=0,blocks=0,bytes=0;
   if(guint(GST_EVENT_TYPE(e))!=(guint(GST_EVENT_CUSTOM_DOWNSTREAM)&0xffffu)||!gst_structure_get_uint(st,"version",&version)||version!=1||!gst_structure_get_uint64(st,"generation",&generation)||generation!=c.generation||!gst_structure_get_uint64(st,"blocks",&blocks)||blocks!=s->blocks||!gst_structure_get_uint64(st,"pcm-bytes",&bytes)||bytes!=s->bytes)return badInput(s,"audio terminal record mismatch");
   g_atomic_int_set(&s->reader->eos,1);auto*replacement=gst_event_new_eos();gst_event_set_seqnum(replacement,gst_event_get_seqnum(e));gst_event_unref(e);GST_PAD_PROBE_INFO_DATA(info)=replacement;
  }else if(GST_EVENT_TYPE(e)==GST_EVENT_EOS&&!g_atomic_int_get(&s->reader->eos)){return badInput(s,"missing terminal accounting");
  }else if(GST_EVENT_TYPE(e)==GST_EVENT_CAPS){GstCaps*caps=nullptr;gst_event_parse_caps(e,&caps);if(!caps||gst_caps_get_size(caps)!=1)return badInput(s,"invalid PCM caps");auto*st=gst_caps_get_structure(caps,0);gint rate=0,channels=0;auto*format=gst_structure_get_string(st,"format");auto*layout=gst_structure_get_string(st,"layout");
   if(!gst_structure_has_name(st,"audio/x-raw")||!format||strcmp(format,c.audio.pcm==FL_PCM_S16?"S16LE":"F32LE")||!layout||strcmp(layout,"interleaved")||!gst_structure_get_int(st,"rate",&rate)||!gst_structure_get_int(st,"channels",&channels)||guint(rate)!=c.audio.rate||guint(channels)!=c.audio.layout)return badInput(s,"PCM caps disagree with authenticated session");
  }else if(GST_EVENT_TYPE(e)==GST_EVENT_SEGMENT){const GstSegment*seg=nullptr;gst_event_parse_segment(e,&seg);if(seg->format!=GST_FORMAT_TIME||seg->rate!=1||seg->applied_rate!=1)return badInput(s,"unsupported received segment");s->segment=TRUE;
  }
 }else {auto*b=GST_PAD_PROBE_INFO_BUFFER(info);if(!b)return GST_PAD_PROBE_OK;const guint bpf=(c.audio.pcm==FL_PCM_S16?2u:4u)*c.audio.layout;auto size=gst_buffer_get_size(b);
  if(!s->segment||!size||size%bpf||size>((guint64(c.audio.rate)*c.audioQueueMs+999)/1000)*bpf||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(b))||!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(b)))return badInput(s,"invalid received PCM size or timing");++s->blocks;s->bytes+=size;
 }
 return GST_PAD_PROBE_OK;
}
static void contextSrc(GstElement*e,GstContext*c){auto*s=reinterpret_cast<GstFrameLinkAudioSrc*>(e);if(gst_context_has_context_type(c,FL_GST_SESSION_CONTEXT)){auto*session=fl_gst_session_from_context(c);if(session){if(GST_STATE(e)!=GST_STATE_NULL){g_object_unref(session);GST_ELEMENT_ERROR(s,CORE,STATE_CHANGE,("session context immutable outside NULL"),(nullptr));return;}g_clear_object(&s->session);s->session=session;}}GST_ELEMENT_CLASS(gst_framelink_audio_src_parent_class)->set_context(e,c);}
static GstStateChangeReturn stateSrc(GstElement*e,GstStateChange t){auto*s=reinterpret_cast<GstFrameLinkAudioSrc*>(e);
 if(t==GST_STATE_CHANGE_NULL_TO_READY){
  if(!s->reader||!s->depay||!s->tracks||(strcmp(s->tracks,"audio")&&strcmp(s->tracks,"av"))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("missing GDP or invalid tracks"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
  if(!s->session){if(!s->path||!*s->path||strcmp(s->tracks,"audio")||!s->format||(strcmp(s->format,"S16LE")&&strcmp(s->format,"F32LE"))){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("audio session path or authenticated context required"),(nullptr));return GST_STATE_CHANGE_FAILURE;}
   s->session=fl_gst_session_new(FL_GST_TRACK_AUDIO);s->local=TRUE;flAudioFormat format{guint(!strcmp(s->format,"F32LE")?FL_PCM_F32:FL_PCM_S16),s->rate,s->channels};
   *s->reader->startup=std::make_shared<FlAudioStartup>(s->session,e,s->path,format,s->queueMs,s->connectMs,s->idleMs,false);
  }else {flSessionConfig c{};if(fl_gst_session_get_config(s->session,&c)!=FL_GST_SESSION_READY||!c.audioTrackId||bool(c.videoTrackId)!=(!strcmp(s->tracks,"av"))||fl_gst_session_register_track(s->session,FL_GST_TRACK_AUDIO)!=FL_GST_SESSION_READY||fl_gst_session_track_ready(s->session,FL_GST_TRACK_AUDIO,c.generation)!=FL_GST_SESSION_READY){GST_ELEMENT_ERROR(s,RESOURCE,SETTINGS,("invalid or duplicate consumer audio context"),(nullptr));return GST_STATE_CHANGE_FAILURE;}}
  s->reader->session=static_cast<FlGstSession*>(g_object_ref(s->session));s->failed=s->segment=FALSE;s->blocks=s->bytes=0;
 }
 if(t==GST_STATE_CHANGE_PAUSED_TO_READY){if(s->session)fl_gst_session_cancel(s->session);if(*s->reader->startup)(*s->reader->startup)->cancel();}
 auto result=GST_ELEMENT_CLASS(gst_framelink_audio_src_parent_class)->change_state(e,t);
 if(t==GST_STATE_CHANGE_READY_TO_NULL||(t==GST_STATE_CHANGE_NULL_TO_READY&&result==GST_STATE_CHANGE_FAILURE)){
  if(s->reader){if(*s->reader->startup){(*s->reader->startup)->cancel();s->reader->startup->reset();}g_clear_object(&s->reader->session);}
  if(s->local){g_clear_object(&s->session);s->local=FALSE;}
 }
 return result;
}
enum {P_NONE,P_PATH,P_FORMAT,P_RATE,P_CHANNELS,P_QUEUE,P_CONNECT,P_IDLE,P_TRACKS,P_ID,P_GENERATION,P_AUDIO,P_VIDEO};
static void setSrc(GObject*o,guint id,const GValue*v,GParamSpec*p){auto*s=reinterpret_cast<GstFrameLinkAudioSrc*>(o);if(GST_STATE(s)!=GST_STATE_NULL){GST_WARNING_OBJECT(s,"configuration immutable outside NULL");return;}switch(id){case P_PATH:g_free(s->path);s->path=g_value_dup_string(v);break;case P_FORMAT:g_free(s->format);s->format=g_value_dup_string(v);break;case P_TRACKS:g_free(s->tracks);s->tracks=g_value_dup_string(v);break;case P_RATE:s->rate=g_value_get_uint(v);break;case P_CHANNELS:s->channels=g_value_get_uint(v);break;case P_QUEUE:s->queueMs=g_value_get_uint(v);break;case P_CONNECT:s->connectMs=g_value_get_uint(v);break;case P_IDLE:s->idleMs=g_value_get_uint(v);break;default:G_OBJECT_WARN_INVALID_PROPERTY_ID(o,id,p);}}
static void getSrc(GObject*o,guint id,GValue*v,GParamSpec*p){auto*s=reinterpret_cast<GstFrameLinkAudioSrc*>(o);flSessionConfig c{};if(s->session)fl_gst_session_get_config(s->session,&c);switch(id){case P_PATH:g_value_set_string(v,s->path);break;case P_FORMAT:g_value_set_string(v,s->format);break;case P_TRACKS:g_value_set_string(v,s->tracks);break;case P_RATE:g_value_set_uint(v,s->rate);break;case P_CHANNELS:g_value_set_uint(v,s->channels);break;case P_QUEUE:g_value_set_uint(v,s->queueMs);break;case P_CONNECT:g_value_set_uint(v,s->connectMs);break;case P_IDLE:g_value_set_uint(v,s->idleMs);break;case P_GENERATION:g_value_set_uint64(v,c.generation);break;case P_AUDIO:g_value_set_uint(v,c.audioTrackId);break;case P_VIDEO:g_value_set_uint(v,c.videoTrackId);break;case P_ID:{gchar value[33]{};if(c.generation)for(guint i=0;i<16;i++)g_snprintf(value+2*i,3,"%02x",c.sessionId.bytes[i]);g_value_set_string(v,value);break;}default:G_OBJECT_WARN_INVALID_PROPERTY_ID(o,id,p);}}
static void finalizeSrc(GObject*o){auto*s=reinterpret_cast<GstFrameLinkAudioSrc*>(o);g_clear_object(&s->session);g_free(s->path);g_free(s->format);g_free(s->tracks);G_OBJECT_CLASS(gst_framelink_audio_src_parent_class)->finalize(o);}
static gboolean querySrc(GstElement*e,GstQuery*q){
 if(GST_QUERY_TYPE(q)==GST_QUERY_LATENCY){gst_query_set_latency(q,TRUE,0,GST_CLOCK_TIME_NONE);return TRUE;}
 if(GST_QUERY_TYPE(q)==GST_QUERY_SEEKING){GstFormat format;gst_query_parse_seeking(q,&format,nullptr,nullptr,nullptr);gst_query_set_seeking(q,format,FALSE,0,-1);return TRUE;}
 return GST_ELEMENT_CLASS(gst_framelink_audio_src_parent_class)->query(e,q);
}
static void gst_framelink_audio_src_class_init(GstFrameLinkAudioSrcClass*c){auto*o=G_OBJECT_CLASS(c);o->set_property=setSrc;o->get_property=getSrc;o->finalize=finalizeSrc;auto*e=GST_ELEMENT_CLASS(c);e->set_context=contextSrc;e->change_state=stateSrc;e->query=querySrc;gst_element_class_set_static_metadata(e,"FrameLink audio consumer","Source/Audio","Authenticated incremental serialized PCM (not shared memory)","FrameLink");gst_element_class_add_static_pad_template(e,&pcmTemplate);
 auto rw=GParamFlags(G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS);auto ro=GParamFlags(G_PARAM_READABLE|G_PARAM_STATIC_STRINGS);
 g_object_class_install_property(o,P_PATH,g_param_spec_string("session","Session","Protected local rendezvous path",nullptr,rw));g_object_class_install_property(o,P_FORMAT,g_param_spec_string("format","Format","Exact S16LE or F32LE","S16LE",rw));g_object_class_install_property(o,P_TRACKS,g_param_spec_string("tracks","Tracks","audio standalone or av with context","audio",rw));
 g_object_class_install_property(o,P_RATE,g_param_spec_uint("rate","Rate","Exact sample rate",8000,192000,48000,rw));g_object_class_install_property(o,P_CHANNELS,g_param_spec_uint("channels","Channels","Exact channel count",1,2,2,rw));g_object_class_install_property(o,P_QUEUE,g_param_spec_uint("audio-queue-ms","Audio bound","Maximum negotiated PCM block/queue budget",1,2000,80,rw));g_object_class_install_property(o,P_CONNECT,g_param_spec_uint("connect-timeout-ms","Connect deadline","Whole startup deadline",1,60000,15000,rw));g_object_class_install_property(o,P_IDLE,g_param_spec_uint("idle-timeout-ms","Idle deadline","Socket timeout rounded up to seconds",1,60000,15000,rw));
 g_object_class_install_property(o,P_ID,g_param_spec_string("session-id","Session identity","Not a secret",nullptr,ro));g_object_class_install_property(o,P_GENERATION,g_param_spec_uint64("generation","Generation","Negotiated generation",0,G_MAXUINT64,0,ro));g_object_class_install_property(o,P_AUDIO,g_param_spec_uint("audio-track-id","Audio track","Audio identity",0,G_MAXUINT,0,ro));g_object_class_install_property(o,P_VIDEO,g_param_spec_uint("video-track-id","Video track","Video identity",0,G_MAXUINT,0,ro));
}
static void gst_framelink_audio_src_init(GstFrameLinkAudioSrc*s){s->format=g_strdup("S16LE");s->tracks=g_strdup("audio");s->rate=48000;s->channels=2;s->queueMs=80;s->connectMs=s->idleMs=15000;
 s->reader=static_cast<FlAudioReader*>(g_object_new(fl_audio_reader_get_type(),nullptr));s->depay=gst_element_factory_make("gdpdepay",nullptr);
 if(!s->depay){gst_object_unref(s->reader);s->reader=nullptr;return;}gst_bin_add_many(GST_BIN(s),GST_ELEMENT(s->reader),s->depay,nullptr);gst_element_link(GST_ELEMENT(s->reader),s->depay);
 auto*pad=gst_element_get_static_pad(s->depay,"src");gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),inspectPcm,s,nullptr);auto*ghost=gst_ghost_pad_new_from_template("src",pad,gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(s),"src"));gst_object_unref(pad);
 gst_pad_set_query_function(ghost,[](GstPad*pad,GstObject*parent,GstQuery*q)->gboolean{
  if(GST_QUERY_TYPE(q)==GST_QUERY_LATENCY){gst_query_set_latency(q,TRUE,0,GST_CLOCK_TIME_NONE);return TRUE;}
  if(GST_QUERY_TYPE(q)==GST_QUERY_SEEKING){GstFormat f;gst_query_parse_seeking(q,&f,nullptr,nullptr,nullptr);gst_query_set_seeking(q,f,FALSE,0,-1);return TRUE;}
  return gst_pad_query_default(pad,parent,q);
 });gst_element_add_pad(GST_ELEMENT(s),ghost);
}
gboolean fl_register_audio_src(GstPlugin*p){return gst_element_register(p,"framelinkaudiosrc",GST_RANK_NONE,gst_framelink_audio_src_get_type());}

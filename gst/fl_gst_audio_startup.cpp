#include "fl_gst_audio_startup.hpp"
typedef struct {GOutputStream parent;std::shared_ptr<FlAudioStartup>* startup;} FlDeferredAudioOutput;
typedef struct {GOutputStreamClass parent;} FlDeferredAudioOutputClass;
G_DEFINE_TYPE(FlDeferredAudioOutput,fl_deferred_audio_output,G_TYPE_OUTPUT_STREAM)
static gssize writeBytes(GOutputStream*stream,const void*bytes,gsize size,GCancellable*cancel,GError**error){
 auto*self=reinterpret_cast<FlDeferredAudioOutput*>(stream);auto*out=(*self->startup)->output(cancel,error);if(!out)return -1;
 auto n=g_output_stream_write(out,bytes,size,cancel,error);g_object_unref(out);return n;
}
static gboolean closeStream(GOutputStream*stream,GCancellable*,GError**){auto*self=reinterpret_cast<FlDeferredAudioOutput*>(stream);if(*self->startup)(*self->startup)->cancel();return TRUE;}
static void finalize(GObject*object){auto*self=reinterpret_cast<FlDeferredAudioOutput*>(object);delete self->startup;G_OBJECT_CLASS(fl_deferred_audio_output_parent_class)->finalize(object);}
static void fl_deferred_audio_output_class_init(FlDeferredAudioOutputClass*c){G_OUTPUT_STREAM_CLASS(c)->write_fn=writeBytes;G_OUTPUT_STREAM_CLASS(c)->close_fn=closeStream;G_OBJECT_CLASS(c)->finalize=finalize;}
static void fl_deferred_audio_output_init(FlDeferredAudioOutput*self){self->startup=new std::shared_ptr<FlAudioStartup>();}
GOutputStream* fl_deferred_audio_output(const std::shared_ptr<FlAudioStartup>&startup){auto*self=static_cast<FlDeferredAudioOutput*>(g_object_new(fl_deferred_audio_output_get_type(),nullptr));*self->startup=startup;return G_OUTPUT_STREAM(self);}

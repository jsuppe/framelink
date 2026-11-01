// Explicit GPU color-transform example: decoder NV12 -> shared FrameLink BGRA.
// No claim that the stock decoder's private NV12 arrays are shareable.
#include "framelink_d3d11.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/d3d11/gstd3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
static void complete(ID3D11Device*device){
 ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);
 D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query>query;
 require(SUCCEEDED(device->CreateQuery(&desc,&query)),"create completion query");context->End(query.Get());context->Flush();
 auto deadline=g_get_monotonic_time()+2000000;HRESULT hr;
 do{hr=context->GetData(query.Get(),nullptr,0,0);if(hr==S_FALSE)g_usleep(100);}while(hr==S_FALSE&&g_get_monotonic_time()<deadline);
 require(hr==S_OK,"GPU completion deadline/device failure");
}
int main(int argc,char**argv){
 gst_init(&argc,&argv);
 if(argc<3||argc>4){fprintf(stderr,"usage: fl_d3d_decode_producer PLUGIN CHANNEL [gpu-color|cpu-readable]\nconsumer: 320x240 BGRA, 60 frames, GPU-render-capable ring\n");return 2;}
 const bool cpu=argc==4&&std::string(argv[3])=="cpu-readable";
 if(argc==4&&!cpu&&std::string(argv[3])!="gpu-color"){fprintf(stderr,"unknown mode; no implicit fallback\n");return 2;}
 try{
 GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);require(plugin&&!error,"load FrameLink plugin");gst_object_unref(plugin);
 flChannel*producer=nullptr;auto deadline=g_get_monotonic_time()+10000000;
 do{if(flOpenProducer(argv[2],&producer)==FL_OK)break;g_usleep(10000);}while(g_get_monotonic_time()<deadline);
 require(producer,"open consumer ring");flChannelInfo channel{};require(flQuery(producer,&channel)==FL_OK,"query");require(channel.width==320&&channel.height==240&&channel.format==FL_FORMAT_BGRA8,"expected 320x240 BGRA ring");
 ComPtr<ID3D11Device>device=flChannelD3D11Device(producer);
 GstD3D11Device*gstDevice=nullptr;
 const std::string outputPipeline=std::string("appsrc name=input format=time is-live=true caps=video/x-raw")+(cpu?"":"(memory:D3D11Memory)")+",format=BGRA,width=320,height=240,framerate=30/1 ! framelinksink name=output native-memory="+(cpu?"false":"true")+" preserve-frame-metadata=true sync=false async=false enable-last-sample=false";
 auto*tx=gst_parse_launch(outputPipeline.c_str(),&error);require(tx&&!error,"sink pipeline");
 auto*sink=gst_bin_get_by_name(GST_BIN(tx),"output");auto*input=gst_bin_get_by_name(GST_BIN(tx),"input");gboolean adopted=FALSE;g_signal_emit_by_name(sink,"adopt-channel",producer,&adopted);require(adopted,"adopt producer");
 require(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE,"start native sink");
 // GStreamer requires the exact GstD3D11Device wrapper, not merely matching
 // underlying ID3D11Device pointers. Discover it from this sink's native pool.
 if(cpu)gstDevice=gst_d3d11_device_new_wrapped(device.Get());
 else {GstBuffer*probe=nullptr;g_signal_emit_by_name(sink,"acquire-native-buffer",1000u,&probe);require(probe,"inspect native pool device");
 gstDevice=(GstD3D11Device*)gst_object_ref(GST_D3D11_MEMORY_CAST(gst_buffer_peek_memory(probe,0))->device);gst_buffer_unref(probe);}
 const std::string decoderPipeline=std::string("videotestsrc num-buffers=60 ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! x264enc tune=zerolatency bframes=0 key-int-max=30 ! h264parse ! d3d11h264dec name=decoder ! video/x-raw(memory:D3D11Memory),format=NV12")+(cpu?" ! d3d11download ! videoconvert ! video/x-raw,format=BGRA":"")+" ! appsink name=decoded sync=false enable-last-sample=false max-buffers=1";
 auto*decode=gst_parse_launch(decoderPipeline.c_str(),&error);require(decode&&!error,"decoder pipeline");
 auto*context=gst_d3d11_context_new(gstDevice);gst_element_set_context(decode,context);gst_context_unref(context);
 auto*decoded=gst_bin_get_by_name(GST_BIN(decode),"decoded");require(gst_element_set_state(decode,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE,"start hardware decoder");
 GstD3D11Converter*converter=nullptr;unsigned count=0;auto started=g_get_monotonic_time();
 for(;;){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(decoded),5*GST_SECOND);if(!sample){require(gst_app_sink_is_eos(GST_APP_SINK(decoded)),"decoder timeout");break;}
  auto*src=gst_sample_get_buffer(sample);GstBuffer*dst=nullptr;
  if(cpu){require(!gst_is_d3d11_memory(gst_buffer_peek_memory(src,0)),"explicit CPU alternative did not download");dst=gst_buffer_ref(src);}
  else {auto*mem=gst_buffer_peek_memory(src,0);require(gst_is_d3d11_memory(mem),"decoder unexpectedly downloaded");
  auto*resource=gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem));ComPtr<ID3D11Device>actual;resource->GetDevice(&actual);require(actual.Get()==device.Get(),"decoder ignored shared device context");
  if(!converter){GstVideoInfo in{},out{};require(gst_video_info_from_caps(&in,gst_sample_get_caps(sample)),"input caps");gst_video_info_set_format(&out,GST_VIDEO_FORMAT_BGRA,320,240);out.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;converter=gst_d3d11_converter_new(gstDevice,&in,&out,nullptr);require(converter,"create NV12 to BGRA GPU converter");}
  g_signal_emit_by_name(sink,"acquire-native-buffer",1000u,&dst);require(dst,"reserve native output");
  require(gst_d3d11_converter_convert_buffer(converter,src,dst),"GPU color conversion");
  // Decoder storage must not be reused while the conversion still reads it.
  // This finite diagnostic uses an explicit GPU completion wait, no pixel map.
  gst_d3d11_device_lock(gstDevice);complete(device.Get());gst_d3d11_device_unlock(gstDevice);
  }
  if(!cpu){GST_BUFFER_PTS(dst)=GST_BUFFER_PTS(src);GST_BUFFER_DTS(dst)=GST_BUFFER_DTS(src);GST_BUFFER_DURATION(dst)=GST_BUFFER_DURATION(src);}
  require(gst_app_src_push_buffer(GST_APP_SRC(input),dst)==GST_FLOW_OK,"submit to native sink");gst_sample_unref(sample);++count;
  const auto due=started+gint64(count)*1000000/30;const auto now=g_get_monotonic_time();if(due>now)g_usleep(due-now);
 }
 require(count==60,"decoder frame count");gst_app_src_end_of_stream(GST_APP_SRC(input));auto*bus=gst_element_get_bus(tx);auto*end=gst_bus_timed_pop_filtered(bus,5*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));require(end&&GST_MESSAGE_TYPE(end)==GST_MESSAGE_EOS,"native sink completion");gst_message_unref(end);gst_object_unref(bus);
 if(cpu)printf("decoded=%u declared_readbacks=%u declared_cpu_color_transforms=%u declared_uploads=%u; explicit CPU-readable alternative, not zero-copy\n",count,count,count,count);
 else printf("decoded=%u gpu_color_transforms=%u application_payload_cpu_maps=0 explicit_gpu_completion_waits=%u; native sink path, NOT raw NV12 decoder zero-copy\n",count,count,count);
 gst_element_set_state(decode,GST_STATE_NULL);gst_element_set_state(tx,GST_STATE_NULL);if(converter)gst_object_unref(converter);gst_object_unref(decoded);gst_object_unref(decode);gst_object_unref(sink);gst_object_unref(input);gst_object_unref(tx);gst_object_unref(gstDevice);return 0;
 }catch(const std::exception&e){fprintf(stderr,"decode example failed: %s\n",e.what());return 1;}
}

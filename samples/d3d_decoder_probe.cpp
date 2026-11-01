// Hardware qualification probe, not a zero-copy claim or a CI-only substitute.
#include <gst/app/gstappsink.h>
#include <gst/d3d11/gstd3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <set>
#include <utility>
int main(int argc,char**argv){
 gst_init(&argc,&argv);
 const char* spec=argc>1?argv[1]:"videotestsrc num-buffers=30 ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! x264enc tune=zerolatency bframes=0 key-int-max=30 ! h264parse ! d3d11h264dec name=decoder ! video/x-raw(memory:D3D11Memory),format=NV12 ! appsink name=output sync=false enable-last-sample=false max-buffers=1";
 GError*error=nullptr;auto*pipeline=gst_parse_launch(spec,&error);
 if(!pipeline||error){fprintf(stderr,"pipeline: %s\n",error?error->message:"null");return 1;}
 auto*out=gst_bin_get_by_name(GST_BIN(pipeline),"output");if(!out||!GST_IS_APP_SINK(out))return 2;
 if(gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)return 3;
 unsigned frames=0,shareable=0;bool valid=true;std::set<std::pair<void*,guint>> resources;
 for(;;){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),5*GST_SECOND);
  if(!sample){if(!gst_app_sink_is_eos(GST_APP_SINK(out)))valid=false;break;}
  auto*buffer=gst_sample_get_buffer(sample);auto*mem=gst_buffer_peek_memory(buffer,0);
  if(gst_buffer_n_memory(buffer)!=1||!gst_is_d3d11_memory(mem)){valid=false;gst_sample_unref(sample);break;}
  auto*dmem=GST_D3D11_MEMORY_CAST(mem);auto*resource=gst_d3d11_memory_get_resource_handle(dmem);ID3D11Texture2D*texture=nullptr;
  if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))){valid=false;gst_sample_unref(sample);break;}
  D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);auto sub=gst_d3d11_memory_get_subresource_index(dmem);
  IDXGIResource1*dxgi=nullptr;HANDLE handle=nullptr;HRESULT shared=E_NOINTERFACE;
  if(SUCCEEDED(texture->QueryInterface(IID_PPV_ARGS(&dxgi)))){shared=dxgi->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle);dxgi->Release();}
  if(SUCCEEDED(shared)&&handle){++shareable;CloseHandle(handle);}
  resources.insert({resource,sub});
  if(frames==0)printf("decoded texture: %ux%u format=%u array=%u mip=%u bind=0x%x misc=0x%x subresource=%u export_hr=0x%08lx\n",desc.Width,desc.Height,desc.Format,desc.ArraySize,desc.MipLevels,desc.BindFlags,desc.MiscFlags,sub,(unsigned long)shared);
  texture->Release();++frames;gst_sample_unref(sample);
 }
 auto*bus=gst_element_get_bus(pipeline);auto*message=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR);
 if(message){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(message,&e,&d);fprintf(stderr,"runtime: %s (%s)\n",e?e->message:"error",d?d:"");g_clear_error(&e);g_free(d);gst_message_unref(message);valid=false;}
 printf("decoded_frames=%u observed_resource_subresources=%zu nt_exportable_frames=%u; diagnostic only, no FrameLink transfer\n",frames,resources.size(),shareable);
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(bus);gst_object_unref(out);gst_object_unref(pipeline);return valid&&frames?0:4;
}

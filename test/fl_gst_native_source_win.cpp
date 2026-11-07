#include "framelink_d3d11.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/d3d11/gstd3d11.h>
#include <gst/video/video.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atomic>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void fill(flBuffer&b,bool nv12,unsigned value=71){flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);for(unsigned i=0;i<m.planes;++i)for(unsigned y=0;y<m.plane[i].rows;++y)memset((char*)m.plane[i].pixels+y*m.plane[i].stride,value+i,nv12?64:256);flImageUnmap(b.image);}
static bool copyNegative=false;
static void run(const char*format,bool adopt=false){
 bool nv12=!strcmp(format,"NV12");GError*e=nullptr;std::string name="gst-native-win-"+std::to_string(g_get_monotonic_time());
 auto*p=gst_parse_launch(("framelinksrc name=source channel="+name+" width=64 height=32 format="+format+" pool-size=2 native-memory=true preserve-timestamps=true ! appsink name=out sync=false async=false enable-last-sample=false").c_str(),&e);CHECK(p&&!e);
 if(adopt){
     auto*src=gst_bin_get_by_name(GST_BIN(p),"source");
     guint w=0,h=0;g_object_get(src,"width",&w,"height",&h,nullptr);
     flFormat fmt=!strcmp(format,"NV12")?FL_FORMAT_NV12:!strcmp(format,"RGB10A2_LE")?FL_FORMAT_RGB10A2:FL_FORMAT_RGBA8;
     flChannel*c=nullptr;gboolean accepted=TRUE;
     CHECK(flCreateChannelEx(name.c_str(),w,h,fmt,2,FL_MAP_CPU,&c)==FL_OK);
     g_signal_emit_by_name(src,"adopt-channel",c,&accepted);CHECK(!accepted);flChannelInfo info{};CHECK(flQuery(c,&info)==FL_OK);flClose(c);
     CHECK(flCreateChannelEx(name.c_str(),w,h,fmt,2,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
     g_signal_emit_by_name(src,"adopt-channel",c,&accepted);CHECK(accepted);gst_object_unref(src);
 }
 auto*out=gst_bin_get_by_name(GST_BIN(p),"out");CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);flBuffer b{},other{},reserved{},blocked{};
 CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK&&flAcquireBuffer(producer,&other,1000)==FL_OK);fill(b,nv12);CHECK(flSubmit(producer,&b,1234567)==FL_OK);
 auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample);auto*buf=gst_sample_get_buffer(sample);
 CHECK(GST_BUFFER_PTS(buf)==1234567&&gst_buffer_n_memory(buf)==1);
 CHECK(gst_caps_features_contains(gst_caps_get_features(gst_sample_get_caps(sample),0),"memory:D3D11Memory"));
 auto*mem=gst_buffer_peek_memory(buf,0);CHECK(gst_is_d3d11_memory(mem));
 if(copyNegative){
     auto*copy=gst_memory_copy(mem,0,-1);CHECK(copy);
     GstMapInfo snapshot{};CHECK(gst_memory_map(copy,&snapshot,GST_MAP_READ));CHECK(snapshot.data[0]==71);gst_memory_unmap(copy,&snapshot);
     mem=copy; // deliberate copied candidate; alias probe below MUST reject it
 }
 // CreateSharedHandle can produce distinct NT wrapper objects for one GPU
 // allocation. Use an isolated alias probe rather than NT object equality.
 // TEST ONLY: bypass producer reservation after publish, with no GPU reader
 // running, and wait for producer completion before verifying the consumer.
 fill(b,nv12,81);
 auto*device=flChannelD3D11Device(producer);ID3D11DeviceContext*ctx=nullptr;device->GetImmediateContext(&ctx);
 D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};ID3D11Query*q=nullptr;CHECK(SUCCEEDED(device->CreateQuery(&qd,&q)));ctx->End(q);ctx->Flush();
 auto deadline=g_get_monotonic_time()+2000000;HRESULT ready=S_FALSE;
 while((ready=ctx->GetData(q,nullptr,0,0))==S_FALSE&&g_get_monotonic_time()<deadline)Sleep(1);
 CHECK(ready==S_OK);q->Release();ctx->Release();
 auto*identityMeta=gst_buffer_get_video_meta(buf);CHECK(identityMeta);
 GstMapInfo identityMap{};CHECK(gst_memory_map(mem,&identityMap,GST_MAP_READ));
 bool aliases=true;
 for(unsigned i=0;i<(nv12?2u:1u);++i)for(unsigned y=0;y<(i?16u:32u);++y)for(unsigned x=0;x<(nv12?64u:256u);++x)
     if(identityMap.data[identityMeta->offset[i]+y*identityMeta->stride[i]+x]!=81+i)aliases=false;
 gst_memory_unmap(mem,&identityMap);
 if(copyNegative){CHECK(!aliases);puts("deliberate GPU copy rejected by allocation-alias oracle after successful setup");std::exit(0);}
 CHECK(aliases);
 auto*retained=gst_memory_ref(mem);gst_sample_unref(sample);CHECK(flAcquireBuffer(producer,&blocked,0)==FL_TIMEOUT);
 auto*preroll=gst_app_sink_try_pull_preroll(GST_APP_SINK(out),0);if(preroll)gst_sample_unref(preroll);gst_memory_unref(retained);
 fill(other,nv12);CHECK(flSubmit(producer,&other,2234567)==FL_OK);
 sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample);buf=gst_sample_get_buffer(sample);CHECK(GST_BUFFER_PTS(buf)==2234567);
 auto*meta=gst_buffer_get_video_meta(buf);CHECK(meta&&meta->n_planes==(nv12?2u:1u));gsize offsets[4]{};gint strides[4]{};
 for(unsigned i=0;i<meta->n_planes;++i){offsets[i]=meta->offset[i];strides[i]=meta->stride[i];}
 retained=gst_memory_ref(gst_buffer_peek_memory(buf,0));gst_sample_unref(sample);
 CHECK(flAcquireBuffer(producer,&reserved,1000)==FL_OK&&reserved.slot==b.slot);CHECK(flAcquireBuffer(producer,&blocked,0)==FL_TIMEOUT);
 // Reuse the first slot: its native wrapper now uses the cached measured
 // layout. Explicit CPU map must still download fresh pixels with exact pitch.
 fill(reserved,nv12,91);CHECK(flSubmit(producer,&reserved,3234567)==FL_OK);
 auto*reused=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(reused);
 auto*reuseBuffer=gst_sample_get_buffer(reused);CHECK(GST_BUFFER_PTS(reuseBuffer)==3234567);
 auto*reuseMeta=gst_buffer_get_video_meta(reuseBuffer);CHECK(reuseMeta&&reuseMeta->n_planes==(nv12?2u:1u));
 auto*reuseMemory=gst_buffer_peek_memory(reuseBuffer,0);GstMapInfo reuseMap{};CHECK(gst_memory_map(reuseMemory,&reuseMap,GST_MAP_READ));
 for(unsigned i=0;i<(nv12?2u:1u);++i){CHECK(reuseMeta->offset[i]==offsets[i]&&reuseMeta->stride[i]==strides[i]);
  for(unsigned y=0;y<(i?16u:32u);++y)for(unsigned x=0;x<(nv12?64u:256u);++x)CHECK(reuseMap.data[reuseMeta->offset[i]+y*reuseMeta->stride[i]+x]==91+i);}
 gst_memory_unmap(reuseMemory,&reuseMap);gst_sample_unref(reused);
 CHECK(gst_element_set_state(p,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(out);gst_object_unref(p);
 // Explicit test-only readback: source itself wraps the original native texture.
 GstMapInfo map{};CHECK(gst_memory_map(retained,&map,GST_MAP_READ));
 for(unsigned i=0;i<(nv12?2u:1u);++i)for(unsigned y=0;y<(i?16u:32u);++y)for(unsigned x=0;x<(nv12?64u:256u);++x)CHECK(map.data[offsets[i]+y*strides[i]+x]==71+i);
 gst_memory_unmap(retained,&map);gst_memory_unref(retained);flClose(producer);
 printf("%s native D3D11: same shared allocation, pixels/PTS, bounded reuse and retained memory after stop pass\n",format);
}
static void layoutGeneration(){
 auto name="native-layout-generation-"+std::to_string(g_get_monotonic_time());GError*e=nullptr;
 // Fakesink drops its buffer after handoff. Appsink retains its reusable
 // GstSample until another pull, so it correctly prevents a native resize.
 auto*p=gst_parse_launch(("framelinksrc channel="+name+" width=64 height=32 format=NV12 pool-size=2 native-memory=true preserve-timestamps=true ! fakesink name=out sync=false async=false enable-last-sample=false signal-handoffs=true").c_str(),&e);CHECK(p&&!e);
 struct Check {std::atomic<unsigned> width{64},height{32};std::atomic<bool> done{false};}check;
 auto*out=gst_bin_get_by_name(GST_BIN(p),"out");g_signal_connect(out,"handoff",G_CALLBACK(+[](GstElement*,GstBuffer*buffer,GstPad*,gpointer data){
  auto*c=static_cast<Check*>(data);const unsigned w=c->width.load(),h=c->height.load();auto*meta=gst_buffer_get_video_meta(buffer);CHECK(meta&&meta->width==w&&meta->height==h&&meta->n_planes==2);
  auto*mem=gst_buffer_peek_memory(buffer,0);GstMapInfo map{};CHECK(gst_memory_map(mem,&map,GST_MAP_READ));
  for(unsigned plane=0;plane<2;++plane){unsigned rows=plane?h/2:h;CHECK(meta->stride[plane]>=gint(w)&&map.size>=meta->offset[plane]+gsize(rows-1)*meta->stride[plane]+w);
   for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<w;++x)CHECK(map.data[meta->offset[plane]+y*meta->stride[plane]+x]==51+plane);}
  gst_memory_unmap(mem,&map);c->done=true;
 }),&check);
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 for(unsigned step=0;step<2;++step){unsigned w=step?640:64,h=step?64:32;
  if(step){bool resized=false;const auto deadline=g_get_monotonic_time()+2000000;
   do{CHECK(flRequestGeometry(producer,w,h,FL_FORMAT_NV12)==FL_OK);flChannelInfo info{};CHECK(flQuery(producer,&info)==FL_OK);resized=info.width==w&&info.height==h;if(!resized)g_usleep(1000);}while(!resized&&g_get_monotonic_time()<deadline);
   CHECK(resized);}
  check.width=w;check.height=h;check.done=false;
  flBuffer b{};CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK);flMappedImage image{};CHECK(flImageMapPlanes(b.image,&image)==FL_OK);
  for(unsigned plane=0;plane<2;++plane)for(unsigned y=0;y<image.plane[plane].rows;++y)memset((char*)image.plane[plane].pixels+y*image.plane[plane].stride,51+plane,w);
  flImageUnmap(b.image);CHECK(flSubmit(producer,&b,1000+step)==FL_OK);
  const auto deadline=g_get_monotonic_time()+2000000;while(!check.done&&g_get_monotonic_time()<deadline)g_usleep(1000);CHECK(check.done);
 }
 CHECK(gst_element_set_state(p,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_object_unref(out);gst_object_unref(p);flClose(producer);
 puts("native layout generation: resized NV12 pixels, planes and memory extent pass");
}
int main(int argc,char**argv){gst_init(&argc,&argv);CHECK(argc==2||argc==3);copyNegative=argc==3;GError*e=nullptr;auto*p=gst_plugin_load_file(argv[1],&e);CHECK(p&&!e);gst_object_unref(p);run("RGBA");run("NV12");run("RGBA",true);layoutGeneration();}

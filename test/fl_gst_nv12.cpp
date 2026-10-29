#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static guint8 value(int n,unsigned p,unsigned y,unsigned x){return (n*17+p*79+y*3+x*7)&255;}
static GstBuffer* pattern(int n){
 const unsigned w=130,h=66;gsize offsets[4]={64,64+167*h+37,0,0};gint strides[4]={167,181,0,0};
 GstBuffer*b=gst_buffer_new_allocate(nullptr,offsets[1]+181*(h/2)+64,nullptr);
 GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE));memset(m.data,0xcd,m.size);
 for(unsigned p=0;p<2;p++)for(unsigned y=0;y<(p?h/2:h);y++)for(unsigned x=0;x<w;x++)m.data[offsets[p]+y*strides[p]+x]=value(n,p,y,x);
 gst_buffer_unmap(b,&m);CHECK(gst_buffer_add_video_meta_full(b,GST_VIDEO_FRAME_FLAG_NONE,GST_VIDEO_FORMAT_NV12,w,h,2,offsets,strides));return b;
}
static void verify(GstSample*s,int n){
 GstVideoInfo info;CHECK(gst_video_info_from_caps(&info,gst_sample_get_caps(s)));CHECK(info.finfo->format==GST_VIDEO_FORMAT_NV12);
 CHECK(info.width==130&&info.height==66);GstVideoFrame frame{};CHECK(gst_video_frame_map(&frame,&info,gst_sample_get_buffer(s),GST_MAP_READ));
 for(unsigned p=0;p<2;p++)for(unsigned y=0;y<(p?33:66);y++)for(unsigned x=0;x<130;x++)CHECK(((guint8*)GST_VIDEO_FRAME_PLANE_DATA(&frame,p))[y*GST_VIDEO_FRAME_PLANE_STRIDE(&frame,p)+x]==value(n,p,y,x));
 gst_video_frame_unmap(&frame);
}
int main(int argc,char**argv){
 alarm(25);gst_init(&argc,&argv);CHECK(argc==2);GError*error=nullptr;GstPlugin*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin);gst_object_unref(plugin);
 char receive[512],send[512];snprintf(receive,sizeof(receive),"framelinksrc channel=gst-nv12-%d format=NV12 width=130 height=66 fps=60 ! appsink name=out sync=false",getpid());
 snprintf(send,sizeof(send),"appsrc name=in is-live=true format=time caps=video/x-raw,format=NV12,width=130,height=66,framerate=60/1 ! framelinksink channel=gst-nv12-%d sync=false",getpid());
 GstElement*r=gst_parse_launch(receive,&error);CHECK(r&&!error);GstElement*t=gst_parse_launch(send,&error);CHECK(t&&!error);
 GstElement*out=gst_bin_get_by_name(GST_BIN(r),"out"),*in=gst_bin_get_by_name(GST_BIN(t),"in");
 CHECK(gst_element_set_state(r,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);usleep(100000);
 CHECK(gst_element_set_state(t,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 int checked=0;
 for(int n=0;n<30 && checked<12;n++) {
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),pattern(n))==GST_FLOW_OK);
  GstSample*s=gst_app_sink_try_pull_sample(GST_APP_SINK(out),500*GST_MSECOND);
  if(s){verify(s,n);gst_sample_unref(s);checked++;}
 }
 CHECK(checked==12);
 gst_element_set_state(t,GST_STATE_NULL);gst_element_set_state(r,GST_STATE_NULL);
 gst_object_unref(in);gst_object_unref(out);gst_object_unref(t);gst_object_unref(r);
 // A consumer with an RGB ring must reject NV12, never reinterpret its bytes.
 snprintf(receive,sizeof(receive),"framelinksrc channel=gst-mismatch-%d width=130 height=66 ! appsink name=out sync=false",getpid());
 snprintf(send,sizeof(send),"appsrc name=in is-live=true format=time caps=video/x-raw,format=NV12,width=130,height=66,framerate=60/1 ! framelinksink channel=gst-mismatch-%d sync=false",getpid());
 r=gst_parse_launch(receive,&error);CHECK(r&&!error);t=gst_parse_launch(send,&error);CHECK(t&&!error);
 in=gst_bin_get_by_name(GST_BIN(t),"in");out=gst_bin_get_by_name(GST_BIN(r),"out");
 CHECK(gst_element_set_state(r,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);usleep(100000);
 CHECK(gst_element_set_state(t,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),pattern(0))==GST_FLOW_OK);
 GstBus*bus=gst_element_get_bus(t);
 GstMessage*message=gst_bus_timed_pop_filtered(bus,2*GST_SECOND,GST_MESSAGE_ERROR);CHECK(message);
 GError*failure=nullptr;gchar*details=nullptr;gst_message_parse_error(message,&failure,&details);
 CHECK(failure->domain==GST_CORE_ERROR && failure->code==GST_CORE_ERROR_NEGOTIATION);
 g_error_free(failure);g_free(details);gst_message_unref(message);gst_object_unref(bus);
 CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(out),50*GST_MSECOND));
 gst_element_set_state(t,GST_STATE_NULL);gst_element_set_state(r,GST_STATE_NULL);
 gst_object_unref(in);gst_object_unref(out);gst_object_unref(t);gst_object_unref(r);
 puts("GStreamer NV12 sink -> actual DMA-BUF -> source, padded/offset GstVideoMeta and distinct Y/UV bytes passed");
}

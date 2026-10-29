#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define getpid _getpid
static void usleep(unsigned us){Sleep((us+999)/1000);}
static void alarm(unsigned){} // CTest enforces the timeout on Windows.
#else
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static GstVideoFormat testedFormat=GST_VIDEO_FORMAT_NV12;
static unsigned planes(){return testedFormat==GST_VIDEO_FORMAT_NV12?2:1;}
static unsigned bytes(){return testedFormat==GST_VIDEO_FORMAT_NV12?130:520;}
static guint8 value(int n,unsigned p,unsigned y,unsigned x){return (n*17+p*79+y*3+x*7)&255;}
static GstBuffer* pattern(int n){
 const unsigned w=130,h=66;gsize offsets[4]={64,64+167*h+37,0,0};gint strides[4]={testedFormat==GST_VIDEO_FORMAT_NV12?167:544,181,0,0};
 GstBuffer*b=gst_buffer_new_allocate(nullptr,planes()==2?offsets[1]+181*(h/2)+64:64+544*h+64,nullptr);
 GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE));memset(m.data,0xcd,m.size);
 for(unsigned p=0;p<planes();p++)for(unsigned y=0;y<(p?h/2:h);y++)for(unsigned x=0;x<bytes();x++)m.data[offsets[p]+y*strides[p]+x]=value(n,p,y,x);
 gst_buffer_unmap(b,&m);CHECK(gst_buffer_add_video_meta_full(b,GST_VIDEO_FRAME_FLAG_NONE,testedFormat,w,h,planes(),offsets,strides));return b;
}
static void verify(GstSample*s,int n){
 GstVideoInfo info;CHECK(gst_video_info_from_caps(&info,gst_sample_get_caps(s)));CHECK(info.finfo->format==testedFormat);
 CHECK(info.width==130&&info.height==66);GstVideoFrame frame{};CHECK(gst_video_frame_map(&frame,&info,gst_sample_get_buffer(s),GST_MAP_READ));
 for(unsigned p=0;p<planes();p++)for(unsigned y=0;y<(p?33:66);y++)for(unsigned x=0;x<bytes();x++)CHECK(((guint8*)GST_VIDEO_FRAME_PLANE_DATA(&frame,p))[y*GST_VIDEO_FRAME_PLANE_STRIDE(&frame,p)+x]==value(n,p,y,x));
 gst_video_frame_unmap(&frame);
}
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);
GST_PLUGIN_STATIC_DECLARE(app);
}
#endif
int main(int argc,char**argv){
 if(argc>1 && !strcmp(argv[argc-1],"rgb10a2")){testedFormat=GST_VIDEO_FORMAT_RGB10A2_LE;--argc;}
 alarm(25);gst_init(&argc,&argv);GError*error=nullptr;
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(framelink);
 GstPlugin*plugin=gst_registry_find_plugin(gst_registry_get(),"framelink");
#else
 CHECK(argc==2);GstPlugin*plugin=gst_plugin_load_file(argv[1],&error);
#endif
if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin);gst_object_unref(plugin);
 const char*fmt=gst_video_format_to_string(testedFormat);
 char receive[512],send[512];snprintf(receive,sizeof(receive),"framelinksrc channel=gst-nv12-%d format=%s width=130 height=66 fps=60 ! appsink name=out sync=false",getpid(),fmt);
 snprintf(send,sizeof(send),"appsrc name=in is-live=true format=time caps=video/x-raw,format=%s,width=130,height=66,framerate=60/1 ! framelinksink channel=gst-nv12-%d sync=false",fmt,getpid());
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
 snprintf(send,sizeof(send),"appsrc name=in is-live=true format=time caps=video/x-raw,format=%s,width=130,height=66,framerate=60/1 ! framelinksink channel=gst-mismatch-%d sync=false",fmt,getpid());
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
 printf("GStreamer %s padded/offset byte roundtrip and mismatch rejection passed\n",fmt);
}

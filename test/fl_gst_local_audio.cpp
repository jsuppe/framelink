#include "framelink_session_transport.h"
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char**argv){gst_init(&argc,&argv);
 for(int first=0;first<2;first++){
  gchar*root=g_dir_make_tmp("framelink-local-XXXXXX",nullptr);CHECK(root);std::string path=std::string(root)+"/session";g_free(root);
  auto*p=fl_gst_session_new(2);auto*c=fl_gst_session_new(2);flAudioFormat fmt{FL_PCM_S16,48000,FL_LAYOUT_STEREO};gboolean pr=FALSE,cr=FALSE;GError*pe=nullptr,*ce=nullptr;
  auto producer=[&]{pr=fl_gst_session_open_local_audio(p,TRUE,path.c_str(),&fmt,80,2000,1000,nullptr,&pe);};
  auto consumer=[&]{cr=fl_gst_session_open_local_audio(c,FALSE,path.c_str(),&fmt,80,2000,1000,nullptr,&ce);};
  std::thread a([&]{if(first)consumer();else producer();});g_usleep(20000);std::thread b([&]{if(first)producer();else consumer();});a.join();b.join();
  if(pe)fprintf(stderr,"producer %s\n",pe->message);if(ce)fprintf(stderr,"consumer %s\n",ce->message);
  CHECK(pr&&cr&&!pe&&!ce);auto*ps=fl_gst_session_claim_audio_stream(p,TRUE,1);auto*cs=fl_gst_session_claim_audio_stream(c,FALSE,1);CHECK(ps&&cs);
  char send='x',got=0;gsize n=0;CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(ps)),&send,1,&n,nullptr,nullptr));
  CHECK(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(cs)),&got,1,&n,nullptr,nullptr)&&got==send);
  g_object_unref(ps);g_object_unref(cs);fl_gst_session_cancel(p);fl_gst_session_cancel(c);g_object_unref(p);g_object_unref(c);
 }
 auto*s=fl_gst_session_new(2);flAudioFormat fmt{FL_PCM_S16,48000,1};GError*error=nullptr;
 gchar*root=g_dir_make_tmp("framelink-missing-XXXXXX",nullptr);CHECK(root);std::string path=std::string(root)+"/missing";g_free(root);
 auto start=g_get_monotonic_time();CHECK(!fl_gst_session_open_local_audio(s,FALSE,path.c_str(),&fmt,80,50,1000,nullptr,&error));CHECK(error&&g_get_monotonic_time()-start<1000000);g_clear_error(&error);g_object_unref(s);
 puts("local audio setup: both startup orders, authenticated stream lifetime and bounded missing-peer passed");
}

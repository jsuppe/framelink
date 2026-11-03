#include "framelink_session_transport.h"
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <sys/stat.h>
#endif
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
 bool permissions=false;
#ifndef _WIN32
 // A session directory reachable by group/other must be refused by both roles;
 // never publish or read a rendezvous another local user could replace.
 // Windows DACL rejection is covered by av_rendezvous on the same code path.
 for(mode_t mode:{mode_t(0750),mode_t(0705)}){
  gchar*base=g_dir_make_tmp("framelink-perm-XXXXXX",nullptr);CHECK(base);std::string dir=std::string(base)+"/session";g_free(base);
  CHECK(mkdir(dir.c_str(),0700)==0&&chmod(dir.c_str(),mode)==0);
  // Both roles together: without the permission check they would connect.
  auto*pq=fl_gst_session_new(2);auto*cq=fl_gst_session_new(2);GError*pe=nullptr,*ce=nullptr;gboolean pr=TRUE,cr=TRUE;auto begin=g_get_monotonic_time();
  std::thread prod([&]{pr=fl_gst_session_open_local_audio(pq,TRUE,dir.c_str(),&fmt,80,500,1000,nullptr,&pe);});
  std::thread cons([&]{cr=fl_gst_session_open_local_audio(cq,FALSE,dir.c_str(),&fmt,80,500,1000,nullptr,&ce);});prod.join();cons.join();
  fprintf(stderr,"mode %o producer: %s; consumer: %s\n",unsigned(mode),pe?pe->message:"connected",ce?ce->message:"connected");
  CHECK(!pr&&!cr&&pe&&ce&&g_get_monotonic_time()-begin<2000000);CHECK(strstr(pe->message,"owner-only"));
  flSessionConfig out{};CHECK(fl_gst_session_get_config(pq,&out)!=FL_GST_SESSION_READY&&fl_gst_session_get_config(cq,&out)!=FL_GST_SESSION_READY);
  g_clear_error(&pe);g_clear_error(&ce);g_object_unref(pq);g_object_unref(cq);
 }
 permissions=true;
#endif
 printf("local audio setup: both startup orders, authenticated stream lifetime, bounded missing-peer%s passed\n",permissions?" and permissive-directory rejection":"");
}

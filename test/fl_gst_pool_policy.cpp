#include "framelink.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "../samples/deadline.h"
#ifdef __ANDROID__
extern "C" {
GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);
}
#endif
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static GstElement* parse(const std::string& s){GError* e=nullptr;auto*p=gst_parse_launch(s.c_str(),&e);if(e)fprintf(stderr,"%s\n",e->message);CHECK(p&&!e);return p;}
static void push(GstElement* in,int64_t pts) {auto*b=gst_buffer_new_allocate(nullptr,16*16*4,nullptr);gst_buffer_memset(b,0,127,16*16*4);GST_BUFFER_PTS(b)=pts;CHECK(gst_app_src_push_buffer(GST_APP_SRC(in),b)==GST_FLOW_OK);}
static void policy(unsigned pool,bool fail,bool recover) {
 const auto name="pool-policy-"+std::to_string(g_get_monotonic_time());flChannel*c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),16,16,FL_FORMAT_BGRA8,pool,FL_MAP_CPU|FL_DEFER_RELEASE,&c)==FL_OK);
 auto*p=parse("appsrc name=in is-live=true format=time caps=video/x-raw,format=BGRA,width=16,height=16,framerate=30/1 ! framelinksink channel="+name+" preserve-timestamps=true sync=false buffer-wait-ms="+std::to_string(recover?500:fail?50:0)+" error-on-ring-full="+(fail?"true":"false"));
 auto*in=gst_bin_get_by_name(GST_BIN(p),"in");auto*bus=gst_element_get_bus(p);
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 std::vector<uint64_t> leases;flFrame f{};
 for(unsigned i=0;i<pool;++i){push(in,i+1);CHECK(flAcquireFrame(c,&f,2000)==FL_OK);uint64_t t=0;CHECK(flLeaseFrame(c,&f,&t)==FL_OK);leases.push_back(t);}
 const gint64 before=g_get_monotonic_time();push(in,999);
 if(recover){
  g_usleep(20000);CHECK(flCompleteReadLease(c,leases[0])==FL_OK);leases.erase(leases.begin());CHECK(flCollectReadLeases(c)==FL_OK);
  CHECK(flAcquireFrame(c,&f,2000)==FL_OK && f.ptsNs==999);uint64_t t=0;CHECK(flLeaseFrame(c,&f,&t)==FL_OK);leases.push_back(t);
 }
 CHECK(gst_app_src_end_of_stream(GST_APP_SRC(in))==GST_FLOW_OK);
 bool stats=false;for(;;){auto*m=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,(GstMessageType)(GST_MESSAGE_EOS|GST_MESSAGE_ERROR|GST_MESSAGE_ELEMENT));CHECK(m);
  if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){CHECK(fail&&!recover);GError* e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);CHECK(e->domain==GST_RESOURCE_ERROR&&e->code==GST_RESOURCE_ERROR_BUSY);g_error_free(e);g_free(d);gst_message_unref(m);CHECK(g_get_monotonic_time()-before>=30000);break;}
  if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ELEMENT){auto*s=gst_message_get_structure(m);if(gst_structure_has_name(s,"framelink-sink-stats")){guint64 received=0,submitted=0,dropped=0,full=0;CHECK(gst_structure_get_uint64(s,"received",&received)&&gst_structure_get_uint64(s,"submitted",&submitted)&&gst_structure_get_uint64(s,"dropped",&dropped)&&gst_structure_get_uint64(s,"ring-full-drops",&full));CHECK(received==pool+1&&submitted==pool+(recover?1:0)&&dropped==(recover?0:1)&&full==(recover?0:1));stats=true;}}
  bool eos=GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS;gst_message_unref(m);if(eos){CHECK(!fail||recover);CHECK(stats);break;}
 }
 CHECK(g_get_monotonic_time()-before<2000000);
 for(auto t:leases)CHECK(flCompleteReadLease(c,t)==FL_OK);CHECK(flCollectReadLeases(c)==FL_OK);
 gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(in);gst_object_unref(bus);gst_object_unref(p);flClose(c);
}
static void source(unsigned pool) {
 const auto name="source-pool-"+std::to_string(g_get_monotonic_time());
 auto*p=parse("framelinksrc name=source channel="+name+" width=16 height=16 pool-size="+std::to_string(pool)+" ! fakesink sync=false");
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);flChannelInfo info{};CHECK(flQuery(producer,&info)==FL_OK&&info.poolSize==pool);
 auto*src=gst_bin_get_by_name(GST_BIN(p),"source");guint actual=0;g_object_get(src,"pool-size",&actual,nullptr);CHECK(actual==pool);
 g_object_set(src,"pool-size",pool+1,nullptr);g_object_get(src,"pool-size",&actual,nullptr);CHECK(actual==pool); // immutable while allocated
 std::vector<flBuffer> buffers(pool);for(auto&b:buffers)CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK);flBuffer excess{};CHECK(flAcquireBuffer(producer,&excess,0)==FL_TIMEOUT);for(auto&b:buffers)CHECK(flCancelBuffer(producer,&b)==FL_OK);
 flClose(producer);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(src);gst_object_unref(p);
}
static void agePolicy() {
 const auto name="age-policy-"+std::to_string(g_get_monotonic_time());
 auto*p=parse("framelinksrc name=source channel="+name+" width=16 height=16 pool-size=2 max-queue-age-ms=50 preserve-timestamps=true ! appsink name=out sync=false async=false max-buffers=2");
 auto*src=gst_bin_get_by_name(GST_BIN(p),"source");auto*out=gst_bin_get_by_name(GST_BIN(p),"out");auto*pad=gst_element_get_static_pad(src,"src");
 std::atomic<bool> blocked{false};
 auto probe=gst_pad_add_probe(pad,(GstPadProbeType)(GST_PAD_PROBE_TYPE_BLOCK|GST_PAD_PROBE_TYPE_BUFFER),
  [](GstPad*,GstPadProbeInfo*,gpointer data){((std::atomic<bool>*)data)->store(true);return GST_PAD_PROBE_OK;},&blocked,nullptr);
 CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 flChannel*producer=nullptr;CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);flBuffer b{};
 auto send=[&](int64_t pts){CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK && flSubmit(producer,&b,pts)==FL_OK);};
 const int64_t future=120*GST_SECOND;
 send(future);
 for(int i=0;i<1000&&!blocked.load();++i)g_usleep(1000);CHECK(blocked.load());
 send(future+1);g_usleep(150000); // second announcement waits while downstream is blocked
 gst_pad_remove_probe(pad,probe);
 auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample&&GST_BUFFER_PTS(gst_sample_get_buffer(sample))==future);gst_sample_unref(sample);
 guint64 expired=0;for(int i=0;i<1000&&!expired;++i){g_object_get(src,"expired-frames",&expired,nullptr);g_usleep(1000);}CHECK(expired==1);
 send(0); // backwards media time is fresh arrival, not an age-policy violation
 sample=gst_app_sink_try_pull_sample(GST_APP_SINK(out),2*GST_SECOND);CHECK(sample&&GST_BUFFER_PTS(gst_sample_get_buffer(sample))==0);gst_sample_unref(sample);
 g_object_get(src,"expired-frames",&expired,nullptr);CHECK(expired==1);
 flClose(producer);gst_element_set_state(p,GST_STATE_NULL);gst_object_unref(pad);gst_object_unref(src);gst_object_unref(out);gst_object_unref(p);
}
int main(int argc,char**argv){gst_init(&argc,&argv);SampleDeadline deadline;CHECK(argc==2);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
#else
 GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 for(unsigned pool:{2u,3u}){source(pool);policy(pool,false,false);policy(pool,true,false);policy(pool,true,true);}
 agePolicy();
 puts("2/3 image capacities: bounded drop, bounded error, delayed-reader recovery and exact loss accounting pass");
}

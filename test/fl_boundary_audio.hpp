#pragma once
// Test-only loopback GDP PCM alongside native video. Not an authenticated app API.
#ifdef FL_BENCH_GST
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <atomic>
#ifdef __ANDROID__
extern "C" {GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements);GST_PLUGIN_STATIC_DECLARE(gdp);GST_PLUGIN_STATIC_DECLARE(tcp);}
#endif
class BoundaryAudio {
 struct Stamp{uint64_t identity,pts,source,magic;};
 struct Row{uint64_t identity,pts,source,arrival,duration,samples;};
 GstElement*p=nullptr,*app=nullptr;std::thread worker;std::vector<Row> records;std::atomic<unsigned> count{0};bool active=false;std::string role;
 uint64_t offset=37000000;
 static uint64_t stamp(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
 static GstPadProbeReturn probe(GstPad*pad,GstPadProbeInfo*info,gpointer opaque){auto*self=(BoundaryAudio*)opaque;if(auto*b=GST_PAD_PROBE_INFO_BUFFER(info)){auto*event=gst_pad_get_sticky_event(pad,GST_EVENT_SEGMENT,0);CHECK(event);const GstSegment*segment=nullptr;gst_event_parse_segment(event,&segment);CHECK(segment&&gst_segment_to_running_time(segment,GST_FORMAT_TIME,GST_BUFFER_PTS(b))==GST_BUFFER_PTS(b));gst_event_unref(event);self->observe(b);}return GST_PAD_PROBE_OK;}
 void observe(GstBuffer*b){const auto arrived=stamp();unsigned n=count.load();CHECK(n<frames);GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_READ));CHECK(m.size==size_t(48000/fps)*4);Stamp h{};memcpy(&h,m.data,sizeof(h));CHECK(h.identity==n&&h.pts==offset+uint64_t(n)*1000000000/fps&&h.magic==0x46524c50434d3031ULL&&h.source<=arrived);CHECK(GST_BUFFER_PTS(b)==h.pts&&GST_BUFFER_DURATION(b)==uint64_t(n+1)*1000000000/fps-uint64_t(n)*1000000000/fps);for(size_t i=sizeof(h);i<m.size;i++)CHECK(m.data[i]==(unsigned char)n);gst_buffer_unmap(b,&m);records.push_back({n,h.pts,h.source,arrived,GST_BUFFER_DURATION(b),48000/fps});count.store(n+1,std::memory_order_release);}
 void pipeline(const std::string&text){GError*error=nullptr;p=gst_parse_launch(text.c_str(),&error);if(error)fprintf(stderr,"audio pipeline: %s\n",error->message);CHECK(p&&!error);CHECK(gst_element_set_state(p,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);}
 unsigned listenPort(){auto*server=gst_bin_get_by_name(GST_BIN(p),"server");CHECK(server);gint port=0;for(unsigned i=0;i<500&&!port;i++){g_object_get(server,"current-port",&port,nullptr);if(!port)std::this_thread::sleep_for(std::chrono::milliseconds(2));}gst_object_unref(server);CHECK(port>0);return unsigned(port);}
 static unsigned upstream(){auto*s=getenv("FL_BENCH_AUDIO_UP_PORT");CHECK(s&&*s);auto n=strtoul(s,nullptr,10);CHECK(n>0&&n<65536);return unsigned(n);}
 void write(){std::ofstream out(prefix+".audio-"+role+".csv");out<<"identity,pts_ns,source_marker_ns,arrival_ns,duration_ns,samples\n";for(auto&r:records)out<<r.identity<<','<<r.pts<<','<<r.source<<','<<r.arrival<<','<<r.duration<<','<<r.samples<<'\n';out.close();CHECK(out.good());}
public:
 BoundaryAudio(){const char*s=getenv("FL_BENCH_AUDIO");active=s&&!strcmp(s,"1");if(!active)return;CHECK(48000%fps==0&&48000/fps*4>=sizeof(Stamp));gst_init(nullptr,nullptr);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(tcp);
#endif
 records.reserve(frames);}
 static void setPort(unsigned port){auto v=std::to_string(port);
#ifdef _WIN32
 CHECK(!_putenv_s("FL_BENCH_AUDIO_UP_PORT",v.c_str()));
#else
 CHECK(!setenv("FL_BENCH_AUDIO_UP_PORT",v.c_str(),1));
#endif
 }
 bool enabled()const{return active;}
 void receiver(){if(!active)return;role="consumer";pipeline("tcpserversrc name=server host=127.0.0.1 port=0 ! gdpdepay ! appsink name=audio sync=false async=false max-buffers=2 drop=false");setPort(listenPort());app=gst_bin_get_by_name(GST_BIN(p),"audio");CHECK(app);worker=std::thread([this]{for(unsigned n=0;n<frames;n++){auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(app),(n?3:10)*GST_SECOND);CHECK(sample);auto*caps=gst_sample_get_caps(sample);auto*st=gst_caps_get_structure(caps,0);gint rate=0,channels=0;CHECK(gst_structure_get_int(st,"rate",&rate)&&rate==48000&&gst_structure_get_int(st,"channels",&channels)&&channels==2);CHECK(!strcmp(gst_structure_get_string(st,"format"),"S16LE"));auto*b=gst_sample_get_buffer(sample);CHECK(gst_segment_to_running_time(gst_sample_get_segment(sample),GST_FORMAT_TIME,GST_BUFFER_PTS(b))==GST_BUFFER_PTS(b));observe(b);gst_sample_unref(sample);}});}
 void relay(){if(!active)return;role="relay";auto port=upstream();pipeline("tcpserversrc name=server host=127.0.0.1 port=0 ! gdpdepay ! identity name=observe ! gdppay ! tcpclientsink host=127.0.0.1 port="+std::to_string(port)+" sync=false async=false");auto*observe=gst_bin_get_by_name(GST_BIN(p),"observe");CHECK(observe);auto*pad=gst_element_get_static_pad(observe,"src");CHECK(pad);gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,probe,this,nullptr);gst_object_unref(pad);gst_object_unref(observe);setPort(listenPort());std::ofstream out(prefix+".audio-port");out<<upstream();out.close();CHECK(out.good());}
 void producer(){if(!active)return;role="producer";pipeline("appsrc name=audio is-live=true format=time block=true max-buffers=2 max-bytes=0 caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! gdppay ! tcpclientsink host=127.0.0.1 port="+std::to_string(upstream())+" sync=false async=false");app=gst_bin_get_by_name(GST_BIN(p),"audio");CHECK(app);}
 void start(uint64_t epoch){if(!active)return;CHECK(role=="producer");worker=std::thread([this,epoch]{for(unsigned n=0;n<frames;n++){std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(epoch+uint64_t(n)*1000000000/fps)));auto*b=gst_buffer_new_allocate(nullptr,48000/fps*4,nullptr);CHECK(b);GstMapInfo map{};CHECK(gst_buffer_map(b,&map,GST_MAP_WRITE));memset(map.data,(unsigned char)n,map.size);Stamp h{n,offset+uint64_t(n)*1000000000/fps,stamp(),0x46524c50434d3031ULL};memcpy(map.data,&h,sizeof(h));gst_buffer_unmap(b,&map);GST_BUFFER_PTS(b)=h.pts;GST_BUFFER_DURATION(b)=uint64_t(n+1)*1000000000/fps-uint64_t(n)*1000000000/fps;CHECK(gst_app_src_push_buffer(GST_APP_SRC(app),b)==GST_FLOW_OK);records.push_back({n,h.pts,h.source,stamp(),uint64_t(n+1)*1000000000/fps-uint64_t(n)*1000000000/fps,48000/fps});count.store(n+1,std::memory_order_release);}CHECK(gst_app_src_end_of_stream(GST_APP_SRC(app))==GST_FLOW_OK);auto*bus=gst_element_get_bus(p);auto*message=gst_bus_timed_pop_filtered(bus,5*GST_SECOND,(GstMessageType)(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(message&&GST_MESSAGE_TYPE(message)==GST_MESSAGE_EOS);gst_message_unref(message);gst_object_unref(bus);});}
 void finish(){if(!active)return;if(worker.joinable())worker.join();for(unsigned i=0;i<2500&&count.load(std::memory_order_acquire)<frames;i++)std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(count.load()==frames);write();}
 ~BoundaryAudio(){if(!active)return;CHECK(!worker.joinable());if(p)gst_element_set_state(p,GST_STATE_NULL);if(app)gst_object_unref(app);if(p)gst_object_unref(p);}
};
#else
class BoundaryAudio {public:BoundaryAudio(){CHECK(!getenv("FL_BENCH_AUDIO")||!strcmp(getenv("FL_BENCH_AUDIO"),"0"));}bool enabled()const{return false;}static void setPort(unsigned){}void receiver(){}void relay(){}void producer(){}void start(uint64_t){}void finish(){}};
#endif

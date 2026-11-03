// M5b task11: bin parity. Every scenario runs with the producer and consumer
// each as standalone elements (task7 application path) or as the bins
// (framelinkavsink/framelinkavsrc), in all four combinations, and the
// observable outcomes must match: counts and render lag, negotiated memory,
// early EOS, consumer pause, producer flush, peer death both ways, missing
// tracks, queue bounds, reconnect and resource cleanup.
#include "fl_gst_av_harness.h"

struct Outcome {std::string producer,consumer;std::string summary()const{return "producer="+producer+" consumer="+consumer;}};
static const Mode kModes[2]={Mode::Element,Mode::Bin};

static void log(const char*scenario,Mode pm,Mode cm,const Producer&p,const Consumer&c,const Outcome&o){
 fprintf(stderr,"  %-16s producer=%-7s consumer=%-7s | %s | frames %u/%u samples %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT " lag v %.1f..%.1f a %.1f..%.1fms %s\n",scenario,name(pm),name(cm),o.summary().c_str(),
  c.frames.load(),p.pushedFrames.load(),c.samples.load(),p.pushedSamples.load(),c.trackBest[0]/1e6,c.trackWorst[0]/1e6,c.trackBest[1]/1e6,c.trackWorst[1]/1e6,c.features.c_str());
}
// Startup in both orders and full streams: exact counts, lag and memory path.
static Outcome normal(Mode pm,Mode cm,const std::string&tracks,bool native,bool consumerFirst){
 auto path=sessionPath();Producer p(pm,path,tracks,FRAMES);Consumer c(cm,path,tracks,native);
 if(consumerFirst){c.start();g_usleep(200000);p.start();}else{p.start();g_usleep(200000);c.start();}
 c.finish();p.finish();Outcome o{p.result,c.result};
 const bool v=tracks!="audio",a=tracks!="video";
 log(native?"native":consumerFirst?"consumer-first":"producer-first",pm,cm,p,c,o);
 CHECK(o.producer=="EOS"&&o.consumer=="EOS");
 CHECK(c.frames==(v?FRAMES:0u)&&c.samples==(a?guint64(BLOCKS)*BLOCK:0u)&&c.contiguous&&c.lagWithin());
 if(!native)CHECK(c.uniform);
 o.consumer+=" "+c.features;return o;
}
// A clock-paced file-like producer that starts 300ms behind its timeline
// bursts frames faster than real time. The ring keeps only the newest unread
// frame, so coordinated video fails at once, naming the cause, in both modes.
// Element: the application starts PLAYING 300ms after applying the clock.
// Bin: PLAYING 300ms before the consumer exists, so media is held meanwhile.
static Outcome burst(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2,80,true);p.lateMs=300;Consumer c(cm,path,"av");
 if(pm==Mode::Bin){p.start();g_usleep(300000);c.start();}else{c.start();p.start();}
 c.finish();p.finish();Outcome o{p.result,c.result};log("burst",pm,cm,p,c,o);
 CHECK(o.consumer.find("coordinated video frame replaced before it was read")!=std::string::npos&&o.producer.rfind("error: ",0)==0&&c.frames<FRAMES*2);
 return {"error","error: coordinated video frame replaced before it was read: the producer delivered faster than real time"};
}
// The producer application ends the stream early: everything pushed arrives.
static Outcome earlyEos(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",0);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=10;}));gst_element_send_event(p.p,gst_event_new_eos());
 c.finish();p.finish();Outcome o{p.result,c.result};log("early-eos",pm,cm,p,c,o);
 CHECK(o.producer=="EOS"&&o.consumer=="EOS"&&c.frames==p.pushedFrames&&c.samples==p.pushedSamples&&c.contiguous&&c.lagWithin());return o;
}
// Consumer application pauses for 300ms mid-stream and resumes.
static Outcome consumerPause(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8;}));gst_element_set_state(c.p,GST_STATE_PAUSED);g_usleep(300000);gst_element_set_state(c.p,GST_STATE_PLAYING);
 c.finish();p.finish();Outcome o{p.result,c.result};log("consumer-pause",pm,cm,p,c,o);return o;
}
// Producer application attempts a flushing seek mid-stream. The contract
// rejects seeks; the coordinated sinks refuse it before it reaches the
// sources, so the stream stays intact.
static Outcome producerFlush(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8;}));const bool seeked=gst_element_seek_simple(p.p,GST_FORMAT_TIME,GstSeekFlags(GST_SEEK_FLAG_FLUSH),GST_SECOND/2);
 c.finish();p.finish();Outcome o{p.result,c.result};log("producer-seek",pm,cm,p,c,o);
 fprintf(stderr,"    flushing seek %s; delivered %u/%u frames, %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT " samples\n",seeked?"accepted":"refused",c.frames.load(),p.pushedFrames.load(),c.samples.load(),p.pushedSamples.load());
 o.producer+=seeked?" seek=accepted":" seek=refused";
 if(!seeked)CHECK(o.producer=="EOS seek=refused"&&o.consumer=="EOS"&&c.frames==FRAMES*2&&c.samples==guint64(BLOCKS)*BLOCK*2&&c.contiguous);
 return o;
}
// Consumer application attempts a flushing seek on its live pipeline.
static Outcome consumerSeek(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES*2);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8;}));const bool seeked=gst_element_seek_simple(c.p,GST_FORMAT_TIME,GstSeekFlags(GST_SEEK_FLAG_FLUSH),0);
 c.finish();p.finish();Outcome o{p.result,c.result};log("consumer-seek",pm,cm,p,c,o);
 fprintf(stderr,"    flushing seek %s\n",seeked?"accepted":"refused");o.consumer+=seeked?" seek=accepted":" seek=refused";return o;
}
static Outcome death(Mode pm,Mode cm,bool killProducer){
 auto path=sessionPath();Producer p(pm,path,"av",0);Consumer c(cm,path,"av");c.start();p.start();
 CHECK(waitFor([&]{return c.frames.load()>=8&&p.playing&&c.playing;}));
 Side&dead=killProducer?static_cast<Side&>(p):static_cast<Side&>(c);Side&peer=killProducer?static_cast<Side&>(c):static_cast<Side&>(p);
 const gint64 begin=g_get_monotonic_time();dead.kill();peer.finish();const gint64 took=peer.endedAt-begin;
 Outcome o{killProducer?"killed":p.result,killProducer?c.result:"killed"};log(killProducer?"producer-death":"consumer-death",pm,cm,p,c,o);
 fprintf(stderr,"    peer reported %.0fms after death\n",took/1e3);
 CHECK(peer.result.rfind("error: ",0)==0&&took<6000000);
 // Peer-death errors are classed, not compared verbatim: which stream notices
 // first (audio socket, video ring, control connection) is a race.
 return {killProducer?"killed":"error",killProducer?"error":"killed"};
}
static Outcome mismatch(Mode pm,Mode cm,const std::string&pt,const std::string&ct){
 auto path=sessionPath();Producer p(pm,path,pt,FRAMES);Consumer c(cm,path,ct);const gint64 begin=g_get_monotonic_time();c.start();p.start();c.finish();p.finish();
 const gint64 took=std::max(p.endedAt,c.endedAt)-begin;Outcome o{p.result,c.result};log(("tracks "+pt+"/"+ct).c_str(),pm,cm,p,c,o);
 CHECK(o.producer.rfind("error: ",0)==0&&o.consumer.rfind("error: ",0)==0&&took<8000000);return o;
}
static Outcome queueBound(Mode pm,Mode cm){
 auto path=sessionPath();Producer p(pm,path,"av",FRAMES,5);Consumer c(cm,path,"av");c.start();p.start();c.finish();p.finish();
 Outcome o{p.result,c.result};log("audio-queue-5ms",pm,cm,p,c,o);return o;
}
// Two sessions on the same path, each with fresh applications/bins.
static Outcome reconnect(Mode pm,Mode cm){
 auto path=sessionPath();Outcome o;
 for(int round=0;round<2;round++){Producer p(pm,path,"av",FRAMES);Consumer c(cm,path,"av");c.start();p.start();c.finish();p.finish();
  o={p.result,c.result};log(round?"reconnect-2":"reconnect-1",pm,cm,p,c,o);CHECK(o.producer=="EOS"&&o.consumer=="EOS"&&c.frames==FRAMES&&c.samples==guint64(BLOCKS)*BLOCK);}
 return o;
}
// Bin churn: handles return to the warm-up level; a deliberate leak is caught.
static void cleanup(){
 fprintf(stderr,"CASE cleanup (bin/bin churn)\n");
 auto rounds=[](int n,bool leak){std::vector<guint> counts;std::vector<GSocket*> leaked;
  for(int i=0;i<n;i++){auto path=sessionPath();{Producer p(Mode::Bin,path,"av",10);Consumer c(Mode::Bin,path,"av");c.start();p.start();c.finish();p.finish();CHECK(p.result=="EOS"&&c.result=="EOS");}
   if(leak)for(int k=0;k<4;k++)leaked.push_back(g_socket_new(G_SOCKET_FAMILY_IPV4,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr));
   counts.push_back(handles());}
  for(auto*s:leaked)g_object_unref(s);
  const guint warm=*std::max_element(counts.begin(),counts.begin()+3);bool ok=counts.back()<=warm+4;
  std::string t;for(auto c:counts)t+=std::to_string(c)+" ";fprintf(stderr,"  %s handles: %s(warm-up max %u) -> %s\n",leak?"leak control":"churn",t.c_str(),warm,ok?"bounded":"growing");return ok;};
 CHECK(rounds(8,false));CHECK(!rounds(8,true));
}

int main(int argc,char**argv){gst_init(&argc,&argv);
#ifdef __ANDROID__
 GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);GST_PLUGIN_STATIC_REGISTER(gdp);GST_PLUGIN_STATIC_REGISTER(gio);
 GST_PLUGIN_STATIC_REGISTER(videotestsrc);GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
#else
 CHECK(argc>1);GError*error=nullptr;auto*plugin=gst_plugin_load_file(argv[1],&error);if(error)fprintf(stderr,"%s\n",error->message);CHECK(plugin&&!error);gst_object_unref(plugin);
#endif
 const char*only=getenv("FL_PARITY_ONLY");
 // Every scenario in the four producer/consumer combinations; outcomes must agree.
 auto parity=[&](const char*scenario,const std::function<Outcome(Mode,Mode)>&run){
  if(only&&!strstr(scenario,only))return;fprintf(stderr,"CASE %s\n",scenario);std::string first;bool same=true;
  for(Mode pm:kModes)for(Mode cm:kModes){auto o=run(pm,cm).summary();if(first.empty())first=o;else if(o!=first)same=false;}
  fprintf(stderr,"  => %s: %s\n",same?"parity":"DIFFERENT",first.c_str());CHECK(same);};
 parity("consumer-first av",[](Mode pm,Mode cm){return normal(pm,cm,"av",false,true);});
 parity("producer-first av",[](Mode pm,Mode cm){return normal(pm,cm,"av",false,false);});
 parity("burst",burst);
 parity("video-only",[](Mode pm,Mode cm){return normal(pm,cm,"video",false,true);});
 parity("audio-only",[](Mode pm,Mode cm){return normal(pm,cm,"audio",false,false);});
 // Native memory: the same negotiated platform memory on both consumer paths.
 if(const char*f=nativeFeature()){static std::string feature=f;
  parity("native-memory",[](Mode pm,Mode cm){auto o=normal(pm,cm,"av",true,true);CHECK(o.consumer=="EOS "+feature);return o;});}
 else fprintf(stderr,"CASE native-memory: not offered by this build\n");
 parity("early-eos",earlyEos);
 parity("consumer-pause",consumerPause);
 parity("producer-seek",producerFlush);
 parity("consumer-seek",consumerSeek);
 parity("producer-death",[](Mode pm,Mode cm){return death(pm,cm,true);});
 parity("consumer-death",[](Mode pm,Mode cm){return death(pm,cm,false);});
 parity("missing-audio",[](Mode pm,Mode cm){return mismatch(pm,cm,"video","av");});
 parity("missing-video",[](Mode pm,Mode cm){return mismatch(pm,cm,"av","audio");});
 parity("audio-queue-bound",queueBound);
 parity("reconnect",reconnect);
 if(!only||strstr("cleanup",only))cleanup();
 puts("bin parity: identical outcomes for element/bin producers x element/bin consumers");
 return 0;
}

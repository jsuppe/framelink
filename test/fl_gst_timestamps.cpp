#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "../samples/deadline.h"
#include <string>
#include "../gst/framelink_meta.h"
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
extern "C" { GST_PLUGIN_STATIC_DECLARE(framelink);GST_PLUGIN_STATIC_DECLARE(app);GST_PLUGIN_STATIC_DECLARE(coreelements); }
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
struct Pair {
    GstElement *rx, *tx, *in, *out;
    explicit Pair(bool preserve, bool metadata=false) {
        static unsigned count = 0;
        const auto channel = "pts-" + std::to_string(g_get_monotonic_time()) + "-" + std::to_string(count++);
        const std::string mode = std::string(preserve ? "true" : "false") + (metadata?" preserve-frame-metadata=true":"");
        GError* error = nullptr;
        rx = gst_parse_launch(("framelinksrc width=16 height=16 channel=" + channel + " preserve-timestamps=" + mode + " ! appsink name=out sync=false").c_str(), &error);
        CHECK(rx && !error);
        tx = gst_parse_launch(("appsrc name=in is-live=true format=time handle-segment-change=true caps=video/x-raw,format=BGRA,width=16,height=16,framerate=30/1 ! framelinksink sync=false channel=" + channel + " preserve-timestamps=" + mode).c_str(), &error);
        CHECK(tx && !error);
        in = gst_bin_get_by_name(GST_BIN(tx), "in");
        out = gst_bin_get_by_name(GST_BIN(rx), "out");
        CHECK(gst_element_set_state(rx, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        CHECK(gst_element_set_state(tx, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    }
    void push(guint64 pts, const GstSegment* segment = nullptr, guint64 dts=GST_CLOCK_TIME_NONE, guint64 duration=GST_CLOCK_TIME_NONE) {
        auto* b = gst_buffer_new_allocate(nullptr, 16*16*4, nullptr);
        CHECK(b);
        gst_buffer_memset(b, 0, 0x7f, 16*16*4);
        GST_BUFFER_PTS(b) = pts;
        GST_BUFFER_DTS(b)=dts; GST_BUFFER_DURATION(b)=duration;
        // No duration: permit testing INT64_MAX without end-time overflow.
        if (segment) {
            auto* caps = gst_app_src_get_caps(GST_APP_SRC(in));
            auto* sample = gst_sample_new(b, caps, segment, nullptr);
            CHECK(gst_app_src_push_sample(GST_APP_SRC(in), sample) == GST_FLOW_OK);
            gst_caps_unref(caps); gst_sample_unref(sample); gst_buffer_unref(b);
        } else CHECK(gst_app_src_push_buffer(GST_APP_SRC(in), b) == GST_FLOW_OK);
    }
    GstSample* pull() {
        auto* s = gst_app_sink_try_pull_sample(GST_APP_SINK(out), 2*GST_SECOND);
        CHECK(s); return s;
    }
    void error() {
        auto* bus = gst_element_get_bus(tx);
        auto* msg = gst_bus_timed_pop_filtered(bus, 2*GST_SECOND, GST_MESSAGE_ERROR);
        CHECK(msg);
        GError* failure = nullptr; gchar* debug = nullptr;
        gst_message_parse_error(msg, &failure, &debug);
        CHECK(failure->domain == GST_STREAM_ERROR && failure->code == GST_STREAM_ERROR_FORMAT);
        g_error_free(failure); g_free(debug); gst_message_unref(msg); gst_object_unref(bus);
        CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(out), 50*GST_MSECOND));
    }
    ~Pair() {
        gst_element_set_state(tx, GST_STATE_NULL); gst_element_set_state(rx, GST_STATE_NULL);
        for (auto* p : {in, out, tx, rx}) gst_object_unref(p);
    }
};
int main(int argc, char** argv) {
    gst_init(&argc, &argv); SampleDeadline deadline; CHECK(argc == 2);
#ifdef __ANDROID__
    GST_PLUGIN_STATIC_REGISTER(framelink);GST_PLUGIN_STATIC_REGISTER(app);GST_PLUGIN_STATIC_REGISTER(coreelements);
#else
    GError* error = nullptr;
    auto* plugin = gst_plugin_load_file(argv[1], &error); CHECK(plugin && !error); gst_object_unref(plugin);
#endif
    {
        Pair p(true);
        const guint64 values[] = {0, GST_SECOND, GST_SECOND, GST_SECOND/2, G_MAXINT64};
        const bool discontinuities[] = {true, false, true, true, false};
        for (unsigned i=0; i<5; ++i) {
            p.push(values[i]); auto* s=p.pull(); auto* b=gst_sample_get_buffer(s);
            CHECK(GST_BUFFER_PTS(b)==values[i]);
            CHECK(bool(GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT))==discontinuities[i]);
            CHECK(gst_segment_to_running_time(gst_sample_get_segment(s),GST_FORMAT_TIME,GST_BUFFER_PTS(b))==values[i]);
            gst_sample_unref(s);
        }
    }
    {
        Pair p(true); GstSegment segment; gst_segment_init(&segment,GST_FORMAT_TIME);
        segment.start = 5*GST_SECOND; segment.time = 2*GST_SECOND; segment.base = 3*GST_SECOND;
        p.push(6*GST_SECOND,&segment); auto* s=p.pull();
        CHECK(GST_BUFFER_PTS(gst_sample_get_buffer(s))==4*GST_SECOND); gst_sample_unref(s);
    }
    { Pair p(true); p.push(guint64(G_MAXINT64)+1); p.error(); }
    { Pair p(true); p.push(GST_SECOND); gst_sample_unref(p.pull());
      p.push(GST_CLOCK_TIME_NONE); p.error(); }
    {
        Pair p(false,true);
        for(unsigned i=0;i<3;++i){
            guint64 pts=(i+1)*GST_SECOND,dts=pts-100,duration=1234567+i*111;
            p.push(pts,nullptr,dts,duration);auto* sample=p.pull();auto* b=gst_sample_get_buffer(sample);
            CHECK(GST_BUFFER_PTS(b)==pts && GST_BUFFER_DTS(b)==dts && GST_BUFFER_DURATION(b)==duration);
            auto* meta=gst_buffer_get_framelink_frame_meta(b);CHECK(meta && meta->frame.streamGeneration==1);
            CHECK(bool(meta->frame.flags&FL_FRAME_DISCONT)==(i==0));
            int n=9,d=9;CHECK(gst_structure_get_fraction(gst_caps_get_structure(gst_sample_get_caps(sample),0),"framerate",&n,&d));CHECK(n==0&&d==1);
            auto* copy=gst_buffer_copy_deep(b);auto* cm=gst_buffer_get_framelink_frame_meta(copy);CHECK(cm&&cm->frame.streamGeneration==1);gst_buffer_unref(copy);
            gst_sample_unref(sample);
        }
        p.push(4*GST_SECOND);auto* sample=p.pull();auto* b=gst_sample_get_buffer(sample);
        CHECK(GST_BUFFER_DTS(b)==GST_CLOCK_TIME_NONE&&GST_BUFFER_DURATION(b)==GST_CLOCK_TIME_NONE);gst_sample_unref(sample);
    }
    {
        Pair p(false,true);GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);
        segment.start=5*GST_SECOND;segment.base=3*GST_SECOND;segment.rate=2.0;
        p.push(6*GST_SECOND,&segment,5*GST_SECOND,20*GST_MSECOND);auto* sample=p.pull();auto* b=gst_sample_get_buffer(sample);
        CHECK(GST_BUFFER_PTS(b)==3500*GST_MSECOND&&GST_BUFFER_DTS(b)==3*GST_SECOND&&GST_BUFFER_DURATION(b)==10*GST_MSECOND);gst_sample_unref(sample);
    }
    { Pair p(false,true);p.push(GST_SECOND,nullptr,guint64(G_MAXINT64)+1);p.error(); }
    {
        Pair first(false,true), second(false,true);
        first.push(7*GST_SECOND,nullptr,6*GST_SECOND,17*GST_MSECOND);
        auto* a=first.pull();auto* copy=gst_buffer_copy_deep(gst_sample_get_buffer(a));
        auto* meta=gst_buffer_get_framelink_frame_meta(copy);CHECK(meta);meta->frame.streamGeneration=77;
        // A transforming upstream element's current buffer fields, not stale meta, win.
        GST_BUFFER_DURATION(copy)=19*GST_MSECOND;
        CHECK(gst_app_src_push_buffer(GST_APP_SRC(second.in),copy)==GST_FLOW_OK);
        auto* b=second.pull();auto* out=gst_sample_get_buffer(b);
        CHECK(GST_BUFFER_PTS(out)==7*GST_SECOND&&GST_BUFFER_DTS(out)==6*GST_SECOND&&GST_BUFFER_DURATION(out)==19*GST_MSECOND);
        CHECK(gst_buffer_get_framelink_frame_meta(out)->frame.streamGeneration==77);
        gst_sample_unref(a);gst_sample_unref(b);
    }
    // Default behavior is still arrival time, not the producer's media PTS.
    { Pair p(false); p.push(123*GST_SECOND); auto* s=p.pull();
      CHECK(GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(gst_sample_get_buffer(s))));
      CHECK(GST_BUFFER_PTS(gst_sample_get_buffer(s)) != 123*GST_SECOND); gst_sample_unref(s); }
    std::puts("Timestamp contract: zero, repeated/backward, signed limit, overflow/missing rejection, segment mapping, variable metadata, generation relay, default mode passed");
}

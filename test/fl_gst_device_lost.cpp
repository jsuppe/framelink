// M6 decision 13: framelinksrc reports GPU device loss with one stable error.
// FRAMELINK_TEST_FAULTS=ready-error=3 (set by CTest) makes the third frame's
// ready fence complete with an error, as a GPU reset leaves it. The source
// must deliver frames 1 and 2, then post "FrameLink GPU device lost"
// (GST_RESOURCE_ERROR_FAILED, details framelink-device-lost and
// framelink-local-failure) and deliver nothing more.
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "../samples/deadline.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)

int main(int argc, char** argv) {
    gst_init(&argc, &argv); SampleDeadline deadline; CHECK(argc == 2);
    GError* error = nullptr;
    auto* plugin = gst_plugin_load_file(argv[1], &error); CHECK(plugin && !error); gst_object_unref(plugin);
    const std::string channel = "device-lost-" + std::to_string(g_get_monotonic_time());
    auto* rx = gst_parse_launch(("framelinksrc width=16 height=16 channel=" + channel + " ! appsink name=out sync=false").c_str(), &error);
    CHECK(rx && !error);
    auto* tx = gst_parse_launch(("appsrc name=in is-live=true format=time caps=video/x-raw,format=BGRA,width=16,height=16,framerate=30/1 ! framelinksink sync=false channel=" + channel).c_str(), &error);
    CHECK(tx && !error);
    auto* in = gst_bin_get_by_name(GST_BIN(tx), "in");
    auto* out = gst_bin_get_by_name(GST_BIN(rx), "out");
    CHECK(gst_element_set_state(rx, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    CHECK(gst_element_set_state(tx, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    auto push = [&](int n) {
        auto* b = gst_buffer_new_allocate(nullptr, 16*16*4, nullptr); CHECK(b);
        gst_buffer_memset(b, 0, 0x40 + n, 16*16*4);
        GST_BUFFER_PTS(b) = n * GST_SECOND / 30;
        CHECK(gst_app_src_push_buffer(GST_APP_SRC(in), b) == GST_FLOW_OK);
    };
    // One frame at a time: the default one-frame mailbox replaces an unread frame.
    for (int n = 1; n <= 2; ++n) {
        push(n);
        auto* s = gst_app_sink_try_pull_sample(GST_APP_SINK(out), 2*GST_SECOND); CHECK(s);
        GstMapInfo map; CHECK(gst_buffer_map(gst_sample_get_buffer(s), &map, GST_MAP_READ));
        CHECK(map.data[0] == 0x40 + n); gst_buffer_unmap(gst_sample_get_buffer(s), &map); gst_sample_unref(s);
    }
    push(3);
    auto* bus = gst_element_get_bus(rx);
    auto* msg = gst_bus_timed_pop_filtered(bus, 3*GST_SECOND, GST_MESSAGE_ERROR); CHECK(msg);
    GError* failure = nullptr; gchar* debug = nullptr;
    gst_message_parse_error(msg, &failure, &debug);
    std::fprintf(stderr, "frame 3 (ready fence completed with an error): %s (%s)\n", failure->message, debug ? debug : "");
    CHECK(failure->domain == GST_RESOURCE_ERROR && failure->code == GST_RESOURCE_ERROR_FAILED);
    CHECK(!std::strcmp(failure->message, "FrameLink GPU device lost"));
    const GstStructure* details = nullptr; gst_message_parse_error_details(msg, &details);
    gboolean lost = FALSE, local = FALSE;
    CHECK(details && gst_structure_get_boolean(details, "framelink-device-lost", &lost) && lost);
    CHECK(gst_structure_get_boolean(details, "framelink-local-failure", &local) && local);
    g_error_free(failure); g_free(debug); gst_message_unref(msg); gst_object_unref(bus);
    CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(out), 100*GST_MSECOND)); // frame 3 never delivered
    gst_element_set_state(tx, GST_STATE_NULL); gst_element_set_state(rx, GST_STATE_NULL);
    for (auto* p : {in, out, tx, rx}) gst_object_unref(p);
    std::puts("framelinksrc: ready fence error posts 'FrameLink GPU device lost', frame not delivered");
    return 0;
}

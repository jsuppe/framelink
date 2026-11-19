/* Compile the public session header as C11 and call the public epoch and
 * cancellable entry points without the internal header. Invalid requests must
 * fail before any I/O and never call the epoch callback. */
#include "framelink_session_transport.h"
static unsigned calls;
static gint64 epoch(gpointer data,GstClockTime now) {
    (void)data; (void)now; ++calls; return 0;
}
static int invalid(GError** error) {
    const int ok=*error&&(*error)->domain==G_IO_ERROR&&
        ((*error)->code==G_IO_ERROR_INVALID_ARGUMENT||(*error)->code==G_IO_ERROR_NOT_FOUND);
    g_clear_error(error);
    return ok;
}
int main(int argc,char**argv) {
    FlGstEpochFunc callback=epoch;
    FlGstNativeEpochFunc same=callback; /* one type under both names */
    flSessionCapabilities none={0};
    flChannel* channel=NULL;
    GError* error=NULL;
    GCancellable* cancel;
    FlGstSession* session;
    gst_init(&argc,&argv);
    session=fl_gst_session_new(FL_GST_TRACK_VIDEO);
    cancel=g_cancellable_new();
    if(fl_gst_session_open_local_av_epoch(session,TRUE,"",&none,1,2,0,100,100,0,&channel,same,NULL,cancel,&error)||
       channel||!error) return 1;
    g_clear_error(&error);
#if defined(__linux__) || defined(_WIN32)
    if(fl_gst_session_open_local_cpu_epoch(session,TRUE,"",&none,1,2,0,100,100,callback,NULL,cancel,&error)||!error) return 2;
    g_clear_error(&error);
#endif
    /* No setup ran, so there is no control connection to wait on. */
    if(fl_gst_session_local_ready_cancellable(session,TRUE,100,cancel,&error)||!invalid(&error)) return 3;
    if(fl_gst_session_finish_local_cancellable(session,FALSE,100,cancel,&error)||!invalid(&error)) return 4;
    if(fl_gst_session_local_ready_cancellable(session,TRUE,0,cancel,&error)||!invalid(&error)) return 5;
    if(fl_gst_session_finish_local_cancellable(session,TRUE,60001,cancel,&error)||!invalid(&error)) return 6;
    if(calls) return 7;
    g_object_unref(cancel);
    g_object_unref(session);
    return 0;
}

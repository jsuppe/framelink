/* Compile the public header as C11 and call the exported DLL, without an
 * internal header supplying the callback typedef or C++ name linkage. */
#include "framelink_session_transport.h"
#include <string.h>
static unsigned calls;
static gint64 epoch(gpointer data,GstClockTime now) {
    (void)data; (void)now; ++calls; return -1;
}
int main(int argc,char**argv) {
    GstFrameLinkMemoryPolicy candidate={0},bound={0};
    flSessionCapabilities caps={0};
    flChannel* channel=NULL;
    GError* error=NULL;
    FlGstNativeEpochFunc callback=epoch;
    FlGstSession* session;
    gboolean ok;
    gst_init(&argc,&argv);
    session=fl_gst_session_new(FL_GST_TRACK_VIDEO);
    bound.version=99;
    ok=fl_gst_session_open_local_native_epoch(session,TRUE,"unused",&caps,
        1,2,80,100,100,&candidate,&bound,&channel,callback,NULL,NULL,&error);
    if(ok||!error||channel||bound.version||calls) return 1;
    if(error->domain!=G_IO_ERROR||error->code!=G_IO_ERROR_INVALID_ARGUMENT) return 2;
    g_clear_error(&error);
    g_object_unref(session);
    return 0;
}

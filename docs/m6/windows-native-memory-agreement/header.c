#include "framelink_session_transport.h"
static gboolean (*agreement)(FlGstSession*,GSocketConnection*,gboolean,GBytes*,const GstFrameLinkMemoryPolicy*,guint,GCancellable*,GError**) = fl_gst_session_authenticate_native_memory;
int main(void) { return agreement == 0; }

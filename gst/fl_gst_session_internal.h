#pragma once
#include "framelink_session_transport.h"
// Internal publication seam. Only the successful authenticated transport path
// calls it; application-configured records alone cannot supply media streams.
FL_GST_SESSION_API gboolean fl_gst_session_publish_audio(FlGstSession*,
 const flSessionConfig*,GSocketConnection*,gboolean producer);
FL_GST_SESSION_API gboolean fl_gst_session_publish_events(FlGstSession*,
 const flSessionConfig*,GSocketConnection*,gboolean producer);

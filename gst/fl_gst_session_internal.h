#pragma once
#include "framelink_session_transport.h"
// Internal publication seam. Only the successful authenticated transport path
// calls it; application-configured records alone cannot supply media streams.
FL_GST_SESSION_API gboolean fl_gst_session_publish_audio(FlGstSession*,
 const flSessionConfig*,GSocketConnection*,gboolean producer);
FL_GST_SESSION_API gboolean fl_gst_session_publish_events(FlGstSession*,
 const flSessionConfig*,GSocketConnection*,gboolean producer);
// Queued END record without consuming it; TIMEOUT when none has arrived yet.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_peek_video_end(FlGstSession*,
 guint64 generation,FlGstVideoEvent*);

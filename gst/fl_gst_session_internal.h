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
// Producer epoch provider for convenience bins: called once with the current
// system-clock time when the producer proposes the session. The default
// (NULL) proposes now+200ms for applications that start at the epoch.
typedef gint64 (*FlGstEpochFunc)(gpointer data,GstClockTime now);
FL_GST_SESSION_API gboolean fl_gst_session_open_local_av_epoch(FlGstSession*,gboolean producer,
 const gchar* session_path,const flSessionCapabilities* local,guint video_queue_frames,guint video_pool,
 guint audio_queue_ms,guint connect_timeout_ms,guint idle_timeout_ms,guint channel_flags,flChannel** channel,
 FlGstEpochFunc,gpointer,GCancellable*,GError**);
// Ready/drain handshakes that a state change can cancel.
FL_GST_SESSION_API gboolean fl_gst_session_local_ready_cancellable(FlGstSession*,gboolean producer,guint timeout_ms,GCancellable*,GError**);
FL_GST_SESSION_API gboolean fl_gst_session_finish_local_cancellable(FlGstSession*,gboolean producer,guint timeout_ms,GCancellable*,GError**);

#ifdef __linux__
FL_GST_SESSION_API gboolean fl_gst_session_open_local_cpu_epoch(FlGstSession*,gboolean,
 const gchar*,const flSessionCapabilities*,guint,guint,guint,guint,guint,
 FlGstEpochFunc,gpointer,GCancellable*,GError**);
#endif

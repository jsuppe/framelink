#pragma once
#include "framelink_session_transport.h"
// Interface between the session library and the GStreamer plugin. Not public
// API and not installed; plain C names so the two libraries do not depend on
// one compiler's name mangling.
G_BEGIN_DECLS
// Internal publication seam. Only the successful authenticated transport path
// calls it; application-configured records alone cannot supply media streams.
G_GNUC_INTERNAL gboolean fl_gst_session_publish_audio(FlGstSession*,
 const flSessionConfig*,GSocketConnection*,gboolean producer);
G_GNUC_INTERNAL gboolean fl_gst_session_publish_events(FlGstSession*,
 const flSessionConfig*,GSocketConnection*,gboolean producer);
// Queued END record without consuming it; TIMEOUT when none has arrived yet.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_peek_video_end(FlGstSession*,
 guint64 generation,FlGstVideoEvent*);
G_END_DECLS

// Private to the session library, not an exported application API.
G_GNUC_INTERNAL void fl_gst_session_complete_local(FlGstSession*);

#ifdef _WIN32
// Retained result; transport keeps it through setup rollback/publication.
G_GNUC_INTERNAL flD3D11DeviceOwner* fl_gst_session_ref_d3d11_device_owner(FlGstSession*);
#endif

#pragma once
#include <gst/gst.h>
#include "framelink_session.h"
#if defined(_WIN32) && !defined(FL_GST_SESSION_STATIC)
# ifdef FL_GST_SESSION_BUILD
#  define FL_GST_SESSION_API __declspec(dllexport)
# else
#  define FL_GST_SESSION_API __declspec(dllimport)
# endif
#else
# define FL_GST_SESSION_API __attribute__((visibility("default")))
#endif
G_BEGIN_DECLS
#define FL_GST_SESSION_CONTEXT "framelink.session.v1"
#define FL_GST_TRACK_VIDEO 1u
#define FL_GST_TRACK_AUDIO 2u
typedef struct _FlGstSession FlGstSession;
typedef enum { FL_GST_SESSION_READY=0, FL_GST_SESSION_TIMEOUT,
 FL_GST_SESSION_CANCELLED, FL_GST_SESSION_INVALID, FL_GST_SESSION_STALE } FlGstSessionResult;
FL_GST_SESSION_API GType fl_gst_session_get_type(void);
// Full reference. Required tracks: VIDEO, AUDIO or both. No background I/O.
FL_GST_SESSION_API FlGstSession* fl_gst_session_new(guint required_tracks);
// Context stores a strong typed GObject reference, never a borrowed pointer.
FL_GST_SESSION_API GstContext* fl_gst_session_create_context(FlGstSession*);
// Returns full reference or NULL for wrong context/type.
FL_GST_SESSION_API FlGstSession* fl_gst_session_from_context(const GstContext*);
// Called by the authenticated transport controller, NOT an authentication API.
// Exact offers/config are checked before publication. Replacement requires a
// newer generation; old waiters are cancelled. Track registration is retained.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_configure(FlGstSession*,
 const flSessionConfig*, const flSessionCapabilities*, const flSessionCapabilities*);
// Snapshot after negotiation, before media readiness, for caps/device setup.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_get_config(FlGstSession*,flSessionConfig*);
// Register once per local track; duplicate/missing/unsupported tracks reject.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_register_track(FlGstSession*,guint track);
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_track_ready(FlGstSession*,guint track,guint64 generation);
// Finite, cancellable readiness wait; never call from synchronous set_state.
// Output unchanged on error. A new generation never satisfies an old waiter.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_wait(FlGstSession*,guint64 generation,
 guint timeout_ms,flSessionConfig*);
// One mapping for every track in a generation. Identical repeat is allowed;
// conflicting clock/base changes require a new generation. No clock measurement,
// drift correction or per-track rebasing is inferred. Caller holds a session ref.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_set_mapping(FlGstSession*,guint64 generation,
 const flClockMapping*,guint64 max_uncertainty_ns);
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_map_pts(FlGstSession*,guint64 generation,
 gint64 producer_pts_ns,gint64* receiver_pts_ns);
FL_GST_SESSION_API void fl_gst_session_cancel(FlGstSession*);
G_END_DECLS

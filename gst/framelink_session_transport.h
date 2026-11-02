#pragma once
#include "framelink_session_context.h"
#include <gio/gio.h>
G_BEGIN_DECLS
// Reusable synchronous control negotiation on a provider-authorized connected
// STREAM socket. Caller owns stream/session/secret throughout this call. Invoke
// from a worker, never a synchronous element state change. Secret is32bytes and
// must come from protected discovery/broker, not public session IDs. No logging.
// Producer proposes fresh session/clock identities and monotonically increasing
// generation; exact media formats are selected from both offers by this function.
// V2 is explicit. Authentication + offers + config + confirmation are bound to
// fresh directional HMAC transcripts. A finite whole-operation deadline and
// caller cancellation apply; failure closes the supplied connection, never falls
// back. Success publishes validated config to coordinator; no media is sent here.
FL_GST_SESSION_API gboolean fl_gst_session_negotiate_stream(FlGstSession*,
 GSocketConnection*,gboolean producer,GBytes* secret,
 const flSessionCapabilities* local,const flSessionConfig* proposal,
 guint timeout_ms,GCancellable*,GError**);
// Bind a distinct PCM stream to the negotiated configuration/generation, using
// a derived credential and the AUDIO challenge role. No PCM is read/written.
FL_GST_SESSION_API gboolean fl_gst_session_authenticate_audio_stream(FlGstSession*,
 GSocketConnection*,gboolean producer,GBytes* root_secret,guint timeout_ms,
 GCancellable*,GError**);
G_END_DECLS

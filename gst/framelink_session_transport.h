#pragma once
#include "framelink_session_context.h"
#include <gio/gio.h>
#include "framelink.h"
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
// Authenticate a distinct short-lived CPU bootstrap STREAM, using a separate
// credential domain bound to the entire configured session and fresh challenges.
// Requires a video track. Caller retains the stream on success; failure cancels
// the session and closes the stream. This does NOT authorize an unrelated FD,
// publish a CPU channel, or perform discovery: an authenticated descriptor
// transfer over this same connection is still required before CPU binding.
FL_GST_SESSION_API gboolean fl_gst_session_authenticate_cpu_bootstrap(FlGstSession*,
 GSocketConnection*,gboolean producer,GBytes* root_secret,guint timeout_ms,
 GCancellable*,GError**);
#ifndef _WIN32
// Dedicated Unix STREAM bootstrap: authenticates, transfers a fresh connected
// SEQPACKET endpoint via SCM_RIGHTS, validates it and acknowledges receipt.
// Closes bootstrap on success/failure; caller still owns its GObject reference.
// Success returns an owned CLOEXEC fd for bind_shared_cpu_fd; failure sets -1
// and cancels session. No discovery/pool binding; timeout covers entire call.
FL_GST_SESSION_API gboolean fl_gst_session_create_cpu_endpoint(FlGstSession*,
 GSocketConnection*,gboolean producer,GBytes*,guint timeout_ms,GCancellable*,
 int* endpoint_fd,GError**);
#endif
// Full reference, single consumer/producer element claim per generation. Returns
// NULL before successful audio authentication, on wrong role or duplicate claim.
FL_GST_SESSION_API GSocketConnection* fl_gst_session_claim_audio_stream(FlGstSession*,
 gboolean producer,guint64 generation);
G_END_DECLS

G_BEGIN_DECLS
// Standalone audio-only current-user rendezvous. A producer publishes a private
// loopback listener; a consumer may start first and wait up to the same finite
// deadline. This is worker-thread setup, not a blocking element state callback.
// Android cross-UID callers use their authorized broker/connected-stream APIs.
FL_GST_SESSION_API gboolean fl_gst_session_open_local_audio(FlGstSession*,gboolean producer,
 const gchar* session_path,const flAudioFormat*,guint audio_queue_ms,
 guint connect_timeout_ms,guint idle_timeout_ms,GCancellable*,GError**);
G_END_DECLS

G_BEGIN_DECLS
// Optional distinct authenticated event stream for ordered video GAP/flush
// controls. No PCM/video payload. Bind before elements start; same config/root
// credential as negotiation. Success transfers an owning reference to session.
FL_GST_SESSION_API gboolean fl_gst_session_authenticate_event_stream(FlGstSession*,
 GSocketConnection*,gboolean producer,GBytes* root_secret,guint timeout_ms,
 GCancellable*,GError**);
#define FL_GST_VIDEO_GAP 1u
#define FL_GST_VIDEO_BARRIER 2u
#define FL_GST_VIDEO_FLUSH 3u
#define FL_GST_VIDEO_END 4u
typedef struct {guint kind;guint64 afterFrames,ptsNs,durationNs;} FlGstVideoEvent;
FL_GST_SESSION_API gboolean fl_gst_session_has_event_stream(FlGstSession*,guint64 generation);
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_send_video_event(FlGstSession*,guint64 generation,const FlGstVideoEvent*);
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_receive_video_event(FlGstSession*,guint64 generation,guint timeout_ms,FlGstVideoEvent*);
G_END_DECLS

G_BEGIN_DECLS
// Standalone two-process A/V (or single-track) setup over the same protected
// current-user rendezvous as audio-only. Worker-thread call, finite deadline.
// Tracks are those with a nonzero count in `local`; both peers must agree.
// Performs authenticated negotiation, an authenticated clock-offset measurement
// (consumer installs the mapping; uncertainty above 50ms rejects), audio and
// event stream authentication, and the video channel lease. The consumer
// creates the ring (channel_flags 0 = FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA;
// video_pool images, 0 = 4, 2..16. Queue depth 1 works with pools >=2;
// greater depths require queue + 2 <= pool. Unsupported negotiated depth
// rejects before channel creation; it is never silently clamped.)
// under a secret-derived name; the
// producer attaches only after the consumer reports it, and the consumer
// returns only after the producer confirms the attach. *channel receives the
// owned lease for the video element's "adopt-channel" signal, or NULL when no
// video track exists. Failure cancels the session and returns no channel.
FL_GST_SESSION_API gboolean fl_gst_session_open_local_av(FlGstSession*,gboolean producer,
 const gchar* session_path,const flSessionCapabilities* local,guint video_queue_frames,
 guint video_pool,guint audio_queue_ms,guint connect_timeout_ms,guint idle_timeout_ms,guint channel_flags,
 flChannel** channel,GCancellable*,GError**);
// Bounded startup gate over the retained control connection. Consumer: call
// after its pipeline was set to PLAYING (sinks/devices created) to report
// ready. Producer: call before setting PLAYING; waits up to timeout_ms so the
// first frames are not overwritten while the consumer is still initializing.
FL_GST_SESSION_API gboolean fl_gst_session_local_ready(FlGstSession*,gboolean producer,
 guint timeout_ms,GError**);
// Bounded end-of-session handshake over the setup control connection retained
// by fl_gst_session_open_local_av. Call after the local pipeline reached EOS and
// before setting it to NULL. Consumer: report drained and close. Producer: keep
// the video lease and streams open until the consumer reports drained, closes,
// or timeout_ms elapses (FALSE). Idempotent per session.
FL_GST_SESSION_API gboolean fl_gst_session_finish_local(FlGstSession*,gboolean producer,
 guint timeout_ms,GError**);
// Install the negotiated timeline on a pipeline in NULL/READY: monotonic system
// clock, no start-time reset, producer base time = session epoch, consumer base
// time 0 (mapped PTS are receiver system-clock times) and the given pipeline
// latency (consumer; must cover transport and sink buffering). No PTS rewriting.
// The producer call waits until the epoch (at most 1s) so live sources start
// at a nonnegative running time.
FL_GST_SESSION_API gboolean fl_gst_session_apply_pipeline_clock(FlGstSession*,GstPipeline*,
 gboolean producer,GstClockTime latency,GError**);
G_END_DECLS

#if !defined(_WIN32)
#include "framelink_shared_cpu_channel.h"
G_BEGIN_DECLS
// Explicit CPU-only broker seam, NOT authentication or local discovery. Caller
// must first configure the session from authenticated offers and supply a
// broker-authorized connected SEQPACKET FD. Consumes fd on every path. Caller
// holds a session reference throughout this worker-thread call. No hidden native
// fallback; metadata and ordered EOS are mandatory on both endpoints.
// Consumer supplies 2..16 matching descriptors; producer supplies NULL/0.
// Bounded per-I/O by timeout_ms. Session cancel/reconfigure shuts down this
// socket, including while setup is blocked. Does not mark a track ready.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_bind_shared_cpu_fd(
 FlGstSession*,gboolean producer,guint64 generation,int fd,guint timeout_ms,
 const flMemoryRequirements*,guint operations,const flSharedCpuDescriptor*,gsize count);
// Transfer-full, once per generation, exact role. Caller externally serializes
// channel/lease calls and closes the channel. Session retains only a socket
// shutdown handle: cancel/reconfigure interrupts I/O but never frees claimed
// channels/leases; held mappings survive. Unclaimed channels are session-owned.
FL_GST_SESSION_API flSharedCpuChannel* fl_gst_session_claim_shared_cpu(
 FlGstSession*,gboolean producer,guint64 generation);
G_END_DECLS
#endif

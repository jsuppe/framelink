#pragma once
#include "framelink_session_context.h"
#include <gio/gio.h>
#include "framelink.h"
#include "framelink_memory_policy.h"
G_BEGIN_DECLS
// Explicit native-memory agreement on a dedicated connected STREAM. Both peers
// supply the SAME exact candidate, in producer/consumer order (not element-local
// order). Authenticates the full configured session, both requirements and paths,
// and their generation using a separate credential domain and fresh challenges.
// Requires a video session and compatible, non-CPU native paths. Does not choose
// a candidate, transfer handles, probe devices, apply policy or mark tracks ready.
// Broker/adapter must still verify exact allocation identity/layout/device and
// completion contracts, then apply this policy (swap endpoints on the consumer).
// Worker-thread only; caller retains all arguments. Success leaves stream open;
// failure closes it and cancels session. Finite whole-call deadline/cancellation.
// Legacy negotiation/wire/defaults unchanged; no implicit fallback/downgrade.
FL_GST_SESSION_API gboolean fl_gst_session_authenticate_native_memory(
 FlGstSession*,GSocketConnection*,gboolean producer,GBytes* root_secret,
 const GstFrameLinkMemoryPolicy* producer_order,guint timeout_ms,GCancellable*,GError**);
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
#ifndef _WIN32
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_bind_shared_cpu_fd(
 FlGstSession*,gboolean producer,guint64 generation,int fd,guint timeout_ms,
 const flMemoryRequirements*,guint operations,const flSharedCpuDescriptor*,gsize count);
#else
// Windows broker seam: caller authenticates/delegates this exact connected local
// OVERLAPPED message pipe and pinned peer process. Consumes pipe on every path;
// borrows peerProcess for the call. Same mandatory metadata/EOS and role/generation
// contract as the FD seam. Caller holds a session reference throughout this
// worker-thread call. Per-I/O timeout, no native fallback. Session owns a
// manual-reset cancellation event shared with setup and claimed channels.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_bind_shared_cpu_pipe(
 FlGstSession*,gboolean producer,guint64 generation,void* pipe,void* peerProcess,
 guint timeout_ms,const flMemoryRequirements*,guint operations,
 const flSharedCpuDescriptor*,gsize count);
// Explicit authenticated Windows profile. Consumes pipe on every path; borrows
// pinned peer process and 32-byte nonzero root credential. Authenticates on THIS
// pipe before pool negotiation: full config, fresh nonces, directional roles and
// both process IDs are HMAC-bound. No STREAM-auth substitution or fallback.
// timeout_ms bounds the whole authentication, then each pool I/O. Session
// cancel/reconfigure interrupts both phases; failure publishes no CPU channel.
// Discovery/connection and protected credential distribution remain caller-owned.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_bind_authenticated_cpu_pipe(
 FlGstSession*,gboolean producer,guint64 generation,void* pipe,void* peerProcess,
 GBytes* root_secret,guint timeout_ms,const flMemoryRequirements*,guint operations,
 const flSharedCpuDescriptor*,gsize count);
// Configured-session named endpoint profile (Windows only, worker thread).
// Consumer creates a first-instance current-user-only local message pipe;
// producer may start first. UTF-8 name must be \\.\pipe\framelink-cpu- followed
// by ASCII letters/digits/hyphens (max240 total characters). Names are not secrets.
// Obtains/pins the kernel peer and authenticates THIS pipe before pool exchange.
// Caller still supplies protected root credential and authenticated config:
// no discovery record, offer negotiation, PCM/events or track readiness implied.
// Connection plus authentication share one timeout_ms deadline; subsequent pool
// I/O uses timeout_ms per operation, matching bind_authenticated_cpu_pipe.
// Session cancel/reconfigure interrupts all phases. No fallback or handle input.
FL_GST_SESSION_API FlGstSessionResult fl_gst_session_open_named_cpu_pipe(
 FlGstSession*,gboolean producer,guint64 generation,const gchar* pipe_name,
 GBytes* root_secret,guint timeout_ms,const flMemoryRequirements*,guint operations,
 const flSharedCpuDescriptor*,gsize count);

#endif
// Transfer-full, once per generation, exact role. Caller externally serializes
// channel/lease calls and closes the channel. Session retains only a socket
// shutdown handle (POSIX) or shared cancellation event (Windows). Cancel and
// reconfigure interrupt I/O but never free claimed channels/leases; held mappings survive. Unclaimed channels are session-owned.
FL_GST_SESSION_API flSharedCpuChannel* fl_gst_session_claim_shared_cpu(
 FlGstSession*,gboolean producer,guint64 generation);
G_END_DECLS

#if defined(__linux__) || defined(_WIN32)
G_BEGIN_DECLS
// Explicit current-user CPU setup: Linux/Android authenticated Unix discovery;
// Windows typed FAR3 loopback control and authenticated named message pipe.
// Negotiates common clock, PCM/events, endpoint and CPU pool setup.
// Video is required. Both callers explicitly authorize shared-CPU COPY by using
// this API; no native fallback or legacy discovery downgrade. Same queue/pool limits and whole setup
// deadline as open_local_av. On success elements claim the coordinator's CPU
// channel; use local_ready/finish_local for startup/drain. Worker thread only.
FL_GST_SESSION_API gboolean fl_gst_session_open_local_cpu(FlGstSession*,gboolean,
 const gchar*,const flSessionCapabilities*,guint video_queue_frames,guint video_pool,
 guint audio_queue_ms,guint connect_timeout_ms,guint idle_timeout_ms,GCancellable*,GError**);
G_END_DECLS
#endif

#ifdef _WIN32
G_BEGIN_DECLS
// Explicit current-user native profile, worker-thread only. FAR4 discovery is
// separate from legacy/CPU profiles. Negotiates session, authenticates the exact
// producer-ordered candidate BEFORE allocation, then clock/PCM/events and channel.
// v1 supports D3D11 GPU_SAMPLE producer (operations=0), with an explicit receiver:
// CPU_READ (operations=COPY|READBACK), or GPU_SAMPLE (operations=0, deferred release).
// BOTH requirements must authorize the path. Other paths reject; no fallback.
// Native receivers must negotiate retain-memory-until-gpu-complete-v1 downstream
// and retain each memory reference until actual GPU use completes. A policy is
// not proof of arbitrary decoder compatibility or absence of layout-probe costs.
// candidate.version=1, candidate.sessionGeneration=0 (template). Both peers must
// supply the same candidate. Success returns producer-ordered bound policy with
// negotiated generation and owned channel; configure/adopt/apply through bins
// before starting, swapping policy endpoints for receiver. Actual device/layout
// and upstream completion checks still run at channel/allocation integration.
// Hold a session reference and do not reconfigure it or mutate arguments during
// this call. Failure clears outputs and cancels session. One deadline covers
// cancellable setup I/O; native allocation calls are synchronous platform calls.
// Caller GCancellable interrupts I/O. Existing defaults/protocols unchanged.
// A/V bins select this profile only via set-local-native-candidate; see
// framelink_memory_policy.h. Setting native-memory alone does not select it.
FL_GST_SESSION_API gboolean fl_gst_session_open_local_native(FlGstSession*,gboolean producer,
 const gchar* session_path,const flSessionCapabilities*,guint video_queue_frames,guint video_pool,
 guint audio_queue_ms,guint connect_timeout_ms,guint idle_timeout_ms,
 const GstFrameLinkMemoryPolicy* candidate,GstFrameLinkMemoryPolicy* bound,flChannel** channel,
 GCancellable*,GError**);
// Same explicit profile, with producer-selected pipeline epoch. Called once on
// the setup worker after control connection, before session negotiation. Return
// a nonnegative monotonic system-clock timestamp; negative rejects setup. The
// callback must not block and its data must outlive the call. Consumer ignores
// the callback: its epoch always comes from the authenticated producer. Null
// callback preserves open_local_native's now+200ms epoch. The explicit local
// native bin profile passes its already-selected pipeline epoch here.
typedef gint64 (*FlGstNativeEpochFunc)(gpointer data,GstClockTime now);
FL_GST_SESSION_API gboolean fl_gst_session_open_local_native_epoch(FlGstSession*,gboolean producer,
 const gchar* session_path,const flSessionCapabilities*,guint video_queue_frames,guint video_pool,
 guint audio_queue_ms,guint connect_timeout_ms,guint idle_timeout_ms,
 const GstFrameLinkMemoryPolicy* candidate,GstFrameLinkMemoryPolicy* bound,flChannel** channel,
 FlGstNativeEpochFunc,gpointer,GCancellable*,GError**);
G_END_DECLS
#endif

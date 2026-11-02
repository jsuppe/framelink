# M5b public GStreamer A/V contract

Status: tasks1–9 locally validated on the M5b development branch. Public audio
sink/source, coordinated timing, lifecycle/security, two-process examples, the
automated validation matrix and the producer bin are implemented; the consumer
bin and bin parity remain unfinished. See [producer bin](m5b/producer-bin/README.md),
[validation](m5b/validation/README.md), [examples](m5b/examples/README.md) and [lifecycle closure](m5b/lifecycle-security/README.md). Existing `framelinksink`/`framelinksrc` remain video
only. No compatibility claim follows from this design document alone.

## Public surface

- `framelinkaudiosink`: always-present `sink` pad, producer role.
- `framelinkaudiosrc`: always-present `src` pad, consumer role, live source.
- `framelinkavsink`: producer convenience bin; `video_sink` and `audio_sink`
  ghost pads wrapping the individual elements, no output device. Implemented in
  task 9. With `session=`, it performs local discovery off the state-change
  path, holds media at its pads until the session is ready, and applies the
  producer pipeline-clock semantics: start time NONE, base time = session
  epoch, monotonic system clock required. Without `session=`, it is a thin
  composition over an installed context, and the lease is adopted with its
  `adopt-channel` action (decision Jon, 2026-11-02: self-contained bins).
- `framelinkavsrc`: consumer convenience bin; `video_src` and `audio_src`
  ghost pads, no display/audio sink hardwired.
- Bins expose only enabled tracks, chosen while NULL with `tracks=audio|video|av`;
  default `av`. Individual audio elements default `audio`; coordinated use is
  explicit `av`. Video elements retain their legacy defaults unless attached
  to the shared session context. No dummy video allocation for audio-only use.

Audio caps: `audio/x-raw,format={S16LE,F32LE},layout=interleaved,rate=[8000,192000],
channels={1,2}`. Mono/stereo canonical layout only; reject incompatible channel
masks, planar data, compressed formats, malformed sizes and non-integral sample
frames. Negotiation selects an exact shared format; no implicit resampling,
channel conversion or byte-order conversion. Users may insert audioconvert and
audioresample upstream. Buffer duration derives from cumulative sample offsets,
not repeated rounded block durations. F32 transport preserves bytes (no mixing).

## Properties and session context

Shared configuration is immutable outside NULL. Public elements/bins expose:

- `session`: protected local discovery name/path, required unless a provider
  context is installed. Not an authentication secret or remote network URL.
- `tracks`: explicit required track set; all peers must agree, no silent downgrade.
- `connect-timeout-ms` (default15000, range1..60000),
  `idle-timeout-ms` (default15000, range1..60000).
- `audio-queue-ms` (default80, range1..2000): hard bounded buffering budget,
  not preroll/latency target. Oversized blocks fail rather than bypass the cap.
- `video-queue-frames` (default2, range1..16), independent of required native
  allocation count and existing video overflow policy.
- Read-only negotiated `session-id`, `generation`, `audio-track-id`,
  `video-track-id`; absent track ID is zero. Nonzero IDs must differ.

The reusable support API owns one ref-counted coordinator. Persistent GstContext
`framelink.session.v1` carries a typed coordinator object, not raw borrowed
pointers or plaintext credentials. Producer and consumer each have their own
local coordinator; same-pipeline A/V elements share it through context propagation.
Element identity alone must not create two competing sessions. Incompatible
contexts/configurations fail before any media. Applications can install an
OS-authorized provider for Android signed cross-UID connections; no dependency
on `samples/av_apps.cpp` is permitted in the plugin/public API.

Desktop discovery retains current-user endpoint ACLs, random credentials and
challenge authentication. Android uses authorized broker leases, connected stream
FD ownership and revocation. Separate control/audio connections are bound to the
same authenticated session/generation and direction. Native video continues to
use M5a allocation agreement and completion rules. Never log authentication keys.

## Versioning and missing tracks

The existing v1 160-byte core session record requires both tracks; keep its
validation/encoding and ABI unchanged. Introduce explicit v2 selection semantics
using the same fixed field layout and a distinct version/magic. In v2 an absent
track has a zero ID, all-zero format and zero queue budget; at least one track
must exist. Offers may omit a track only when both peers omit it. Mismatched track
sets reject. Both implementations must explicitly support v2; unknown version or
reserved bytes reject. Do not infer audio-only from failed video initialization.

Existing v1 helpers/callers keep v1 behavior. V2 selection, authentication and
replacement bind track presence as well as IDs, formats, clock identity and
generation. A replacement cannot silently change the session's track set.
An old peer must fail clearly, never receive a fake video track or an unauthenticated
fallback. Cross-version negotiation is explicit, not retry-on-parser-error.

## Common timeline and startup

Negotiate once, then wait for required tracks to become ready under one bounded
startup deadline. Do not block an individual element's synchronous state-change
callback waiting for its sibling to start: asynchronous coordinator readiness
allows both tracks to initialize. No media may leak before authentication and
readiness complete. Missing required track reaches a bounded error, not a hang.

Map producer TIME segments to running time and carry that common media timeline
through both transports. Preserve intentional offsets (including audio leading
video). Never subtract independent first-buffer PTS values. Consumer applies one
shared receiver mapping, never arrival-time retimestamping. Standard downstream
sinks schedule against the common pipeline clock/base time. The transport elements
must not also pace each track independently or add per-hop presentation reservoirs.

Missing PTS/overflow/negative mapping reject; no guessed wall-clock timestamps.
Initial support is forward rate1 TIME segments. Reject unsupported seek/trick-rate
changes. Flush/cancellation wakes bounded operations, retires ownership safely and
requires a coordinated new epoch/generation before resuming. DISCONT/GAP/EOS are
explicit control/timing events; no synthetic silence or video fabrication.
Track EOS is distinct from peer death. Normal session EOS follows all required
track drains; fatal authentication/transport errors fail the coordinated session.

## Lifecycle, latency and observability

All joins/readiness/writes/reads/drain/teardown have finite limits and cancellation.
No streaming callback holds the coordinator lock during blocking socket/GPU work.
No thread may access a destroyed element/context; release joins workers and closes
owned handles once. Reconnect uses fresh authentication and generation and discards
stale payload/control messages. Pause/resume is coordinated; no independent rebasing.

Audio uses existing serialized GDP/GIO PCM, not shared audio memory. Report logical
PCM bytes, serialized bytes where observable, queue high-water, gaps/underruns,
dropped/rejected blocks, generation, timing offset and transport errors. Copies
inside OS/driver internals remain unknown, not zero. Trace opt-in, aggregate EOS
stats cheap; keys/PCM payload are not traced. Latency queries report live status
and actual bounded buffering policy without claiming physical-output alignment.

## Acceptance and implementation order

1. Task2: reusable coordinator, context and v2 compatibility/track presence;
   meaningful negative tests before public element networking.
2. Tasks3–6: audio sink/source, shared TIME semantics, bounded security/lifecycle.
3. Tasks7–8: independent two-process examples and automated content/timing/fault
   evidence; Windows first, then Linux/Android support.
4. Tasks9–11: bins are thin compositions of the validated elements, with equivalent
   PCM/timing/memory/error behavior and explicit optional tracks.
5. Tasks12–13: platform runtime/build qualification, references/migration docs,
   exact-head CI, review and merge. Public factory names do not mean availability
   until implementation and `gst-inspect` checks exist.

Tests must distinguish exact producer/consumer media offset, arrival jitter,
GPU completion, scheduled playback and physical speaker/display synchronization.
No new shared-PCM transport or universal decoder support is included.

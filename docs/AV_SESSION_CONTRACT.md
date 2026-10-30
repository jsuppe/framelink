# Local A/V session contract v1

`framelink_session.h` supplies allocation-free, portable control-plane helpers.
It does **not** add audio to `flFrame`, perform discovery/IPC, authenticate a peer,
measure clocks, or play media. GStreamer remains the audio transport/playback
framework. The first implementation negotiates exactly one video and one PCM
audio track. Video-only applications keep the existing FrameLink API unchanged.

## Identity and lifetime

The session owner generates a fresh nonzero 128-bit session ID (use OS secure
randomness). A session ID is opaque, **not a bearer credential**. Track IDs are
nonzero, distinct and stable within a session. Configuration generation starts
at one and strictly increases for format, epoch or endpoint replacement.
Generation must never wrap: start a new session identity instead. Process
restart creates a new identity unless a separate surviving owner explicitly
retains the session. A consumer must not silently adopt an unrelated identity.

Only the consumer owns the FrameLink video ring, as in the existing API. Session
ownership is a control-plane role, not a change in buffer ownership. The consumer
allocates the negotiated ring after agreement and returns its protected endpoint
to the producer. Audio transport endpoints are exchanged separately and have the
same session/generation binding. Session generation and FrameLink ring generation
are distinct; applications validate both where appropriate.

## Capability negotiation

Both peers provide `flSessionCapabilities`, with at most eight concrete formats
per track. Every populated entry is validated, even when it is not selected.
Video intersection requires the same native format, dimensions and rational frame
rate. Producer order selects video. Audio is interleaved S16 or F32, mono (one
front-center channel) or stereo (left, right), 8,000–192,000 samples/s. Exact
format/rate/layout intersection is required; among matches, 48 kHz is preferred,
then producer order. No conversion is implied. PCM byte order must be explicitly
set in the chosen GStreamer caps (the portable session v1 canonical payload uses
S16LE/F32LE); native-endian formats must not be guessed.

The capability struct is a **local API**, not a wire struct. Transport adapters
must serialize bounded offers explicitly, reject duplicate/unknown fields and
oversized counts before writing into this struct, and cap message lengths before
allocation. The fixed codec below is for the resulting configuration, not offers.
Reject empty intersections; do not silently downgrade to video-only. Hardware
allocation support must be checked before acknowledging ready: advertised formats
are capabilities of this particular endpoint, not merely enum values.

## Control state machine

1. **Disconnected -> negotiating:** authenticate the local peer, exchange version
   and bounded offers, agree track formats and session/clock identities.
2. **Negotiating -> configured:** owner sends the encoded configuration; peer
   checks it against the actual offers and acknowledges the *identical* config.
   The codec validates syntax, not whether a selection appeared in either offer.
3. **Configured -> ready:** allocate/open video and audio endpoints and exchange
   readiness for the same session ID and generation. Do not produce until both
   tracks and the clock mapping are ready.
4. **Ready -> running:** explicit start acknowledgement enables media flow.
5. **Running -> draining/configured:** on discontinuity/renegotiation, stop old
   submission, retire outstanding video buffers/fences, flush old PCM, then
   exchange a strictly newer generation and repeat readiness. The first buffer
   of each new generation carries the discontinuity marker in its transport.
6. **Any state -> disconnected:** timeout, permission failure, peer death or fatal
   track failure closes only locally owned endpoints, retires GPU work safely,
   and exposes the failure. No unlimited implicit retry loop.

An application transport must tag every control message and media stream with
session identity/generation (or bind the endpoint immutably to them). Reject
stale replies and data. Retransmission of an identical configuration may be
acknowledged idempotently by the adapter; `flSessionValidateReplacement` rejects
an equal generation because it validates a *replacement*, not retransmission.

## Wire configuration

Exactly 160 bytes, all integers little-endian; never memcpy a C struct. Decode
requires the exact length, known magic/version, and all reserved bytes zero.
Decode failure leaves its output unchanged. No allocations occur.

- 0–3: ASCII `FVS1`; 4–7: version (1).
- 8–23: session ID; 24–39: source clock ID (opaque bytes).
- 40–47: generation; 48–51: video track ID; 52–55: audio track ID.
- 56–75: video format, width, height, FPS numerator, FPS denominator (u32 each).
- 76–87: PCM format, sample rate, layout (u32 each).
- 88–95: nonnegative epoch nanoseconds, restricted to signed-64 range.
- 96–111: video queue frames, audio queue ms, connection timeout ms, idle timeout ms.
- 112–159: reserved zero.

V1 deliberately rejects unknown versions rather than guessing compatibility.
A future offer/control envelope must have its own bounded versioned codec; this
configuration is not an extensible arbitrary-length message format.

## Clock and sample timing

The clock ID identifies a running clock instance, **not** a label such as
“monotonic” or “Windows QPC”. PTS is nonnegative nanoseconds relative to the
negotiated source epoch. Audio PTS identifies the first sample in its block;
video PTS identifies the content time. Unknown PTS (`-1` in the legacy video API)
is not allowed for synchronized session mode.

Both peers must explicitly agree a mapping before ready. `flClockMapping`
contains paired source/target anchors and measured uncertainty. `flSessionMapPts`
checks source identity, uncertainty budget, and all signed arithmetic. Equal clock
IDs permit only identity mapping (equal anchors, zero uncertainty). Differing
clock IDs require explicit paired anchors; these may be measured by an adapter
or supplied by a trusted shared-clock authority. The helper does not certify the
measurement. If no mapping can meet the budget, reject synchronized startup.
Do not assume clocks match simply because both processes run on one machine.

Mapping is an offset, not a drift estimator. Playback clock drift and resampling
belong to GStreamer/M4. Session generation changes invalidate prior mappings when
epoch/clock changes. Keep mapping identity with the accepted configuration.

Compute audio timestamps from cumulative sample index:
`offset = floor(sampleIndex * 1e9 / rate)`. Block duration is
`offset(first + count) - offset(first)`, with checked index addition. Do not sum
rounded block durations: 44.1 kHz would drift. The helper avoids intermediate
multiply overflow and rejects out-of-range results.

## Bounded queues, timeouts and errors

V1 allows 1–16 queued video frames and 1–2000 ms audio queue capacity. These are
hard maxima, not target latency. Connect and idle deadlines are finite 1–60000 ms.
Measure deadlines on the local monotonic clock, independent of media PTS. A peer
which cannot honor these bounds must reject configuration.

Video defaults to discard oldest unpresented frame when full; never overwrite an
acquired/in-flight GPU buffer. Audio cannot silently discard arbitrary samples:
backpressure is bounded by the negotiated deadline, after which report underrun/
overrun and restart at an explicit discontinuity or fail the session. One stalled
track cannot grow the other's queues unboundedly. Paused sessions require explicit
control keepalive or disconnection; media inactivity is not indefinite liveness.

## Local access control and cleanup

Control endpoints must be scoped to the authenticated user/application before any
endpoint handles are shared: private owner-only filesystem sockets on POSIX with
peer credential checks; explicit current-user named-pipe DACL and client identity
checks on Windows; signature-permission Binder/UID enforcement for cross-app
Android deployment. Abstract socket names, predictable ports, session IDs and
configuration validation are **not** authentication. A loopback TCP transport
needs an authenticated protected rendezvous/token mechanism; being local alone
is insufficient. Reject handle transfer to an unverified peer.

Do not remove another process's socket/files on startup. Use fresh per-session
names and clean only resources this process owns. Consumer death must retire its
ring safely; producer death must not strand channel ownership. Authenticated
control and audio endpoint adapters are M3/M5 deliverables, not provided by this
pure contract library.

## Qualification

`session_contract` checks negotiation, equivalent rational frame rates, malformed
counts/formats, wire lengths/version/reserved fields, signed timestamp boundaries,
stale generations, clock mismatches/uncertainty and cumulative audio rounding.
This does **not** itself prove separate processes agree; the two-process M3
integration tests are the completion gate for that behavior.

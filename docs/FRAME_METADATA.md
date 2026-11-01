# M5a.7 frame metadata extension — opt-in transport and GStreamer integration

The legacy FrameReady message carries PTS only. Native Gst output must not invent
variable duration or discard DTS/discontinuity when opted into full metadata.

Implementation contract:

- Keep legacy FL_VERSION6 messages and public flFrame layout unchanged.
- Advertise optional feature support in the formerly-zero Hello feature word;
  low16 bits are support, high16 are required features. Unknown required features
  reject before ring import. Old peers remain valid for legacy mode only.
- An opt-in consumer flag requires metadata support. A new FrameReadyMetadata
  message contains the legacy frame identity/PTS plus versioned metadata.
- Metadata: explicit DTS/duration validity, discontinuity, and nonzero stream
  generation. Missing fields are canonical zero, not inferred from nominal FPS.
  Timestamps are agreed nonnegative running-time nanoseconds; unrepresentable
  timestamp mappings reject rather than silently retimestamp.
- flSetFrameMetadata attaches metadata to an acquired producer reservation before
  BeginSubmit. All submit paths (including native sync-file) publish that same
  metadata. Reacquiring/canceling a slot must not reuse stale metadata.
- flGetFrameMetadata reads the exact held consumer frame before ownership moves
  into a reader lease. Legacy frames report absence, not fabricated values.
- Required mode rejects old peers and metadata-less publication; invalid data
  leaves reservation/ownership intact for cancellation or correction.
- GStreamer opt-in mode converts PTS/DTS through the TIME segment, preserves
  duration/discontinuity, and carries stream generation. No audio timeline change.

Acceptance before completion: exact variable duration/DTS/flags/generation on
Linux, Windows and Android; malformed/unsupported/legacy controls; slot reuse
without stale metadata; existing readiness-fence, native source and legacy A/V
regressions. Protocol support is not authenticated memory-policy negotiation;
that remains separate task7 work.

## GStreamer opt-in

Set `preserve-frame-metadata=true` on both `framelinksink` and `framelinksrc`.
This implies PTS preservation independently of the older `preserve-timestamps`
property. The source requires metadata-capable peers, advertises variable-rate
caps (`framerate=0/1`), and never synthesizes missing duration or DTS.

The sink converts PTS and DTS to nonnegative signed-nanosecond running time.
Duration is the difference between mapped frame end and start, so a 2x segment
maps a 20ms media duration to 10ms running-time duration. Reverse/nonfinite rates,
out-of-segment endpoints, overflow and invalid timestamps fail before pixel copy.
Missing DTS/duration stay absent. Existing legacy behavior remains the default.

`GstFrameLinkFrameMeta` (public `framelink_meta.h`) carries stream generation
through full-buffer copies and FrameLink relays. Byte-region copies do not retain
it. Relays use the current GstBuffer timestamp/duration/flags, not stale values in
that meta. The source adds DISCONT when transport identities skip or generation
changes, in addition to preserving producer discontinuities. Generation labels
are media context, not globally unique allocation IDs or authentication tokens.

An adopted source channel must have FL_REQUIRE_FRAME_METADATA set before adoption.
All existing native-memory ownership rules still apply. Metadata preservation
does not itself provide clock synchronization, audio-master scheduling, or proof
of physical presentation; applications must preserve the agreed session timeline.

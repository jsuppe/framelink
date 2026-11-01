# M5a.7 frame metadata extension — core transport validated; GStreamer integration pending

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

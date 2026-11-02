# M5b.3 public audio sink: standalone startup and bounded failure

Task3 locally validated; milestone remains unmerged. The existing authenticated
context path is retained. Standalone `session` uses protected current-user
rendezvous, v2 audio-only negotiation and generation-bound audio authentication.
Startup runs in a cancellable worker, not a blocking state callback. All public
configuration is immutable outside NULL. Android cross-UID uses the existing
broker/context seam; these shell checks do not qualify public cross-app audio.

Final affected Linux5/5 and Windows5/5: gst_audio_sink, gst_local_audio,
gst_session_transport, gst_session_context, existing video gst_sink_stats.
Android4/4 executables passed on A06B4AABFB07983 with writable TMPDIR; arm64 and
x86_64 native plus existing JNI builds passed. Initial MSVC narrowing-enum build
failure is retained; explicit guint conversion repaired it before final checks.

Content checks cover S16/F32 mono/stereo exact bytes,37ms PTS origin and10ms
sample durations. Both startup orders pass. Missing peer without media reports
a bounded bus error; cancellation tears down promptly; unsupported caps,
misaligned blocks, missing PTS and oversized blocks reject. An authenticated
non-reading peer produces bounded write failure and releases the producer;
Windows accepted49 blocks before failure with198us teardown, Android2039 blocks
with2488us teardown. Socket/kernel buffering differs by platform: these counts
are NOT the element queue capacity. The element queue is bounded by negotiated
PCM byte/time budget plus an in-flight block, not a promised latency reservoir.

Audio remains GDP/GIO serialized/copied. Sink EOS counters measure PCM accepted
at the sink boundary, not remote delivery. Socket idle deadlines round up to
seconds through GSocket; startup deadlines use milliseconds. Full coordinated
flush/seek/discontinuity/lifecycle qualification remains tasks5/6. Public source
and bins remain subsequent tasks. No physical output or performance claim.

# Explicit shared-CPU GStreamer source

`framelinksrc shared-cpu=true` now claims the consumer CPU channel bound to its
session coordinator. Default/native/GPU-readback paths remain unchanged. This
initial source is explicitly **video-only**: audio tracks and separate event
streams reject at start until their accounting/flush/GAP integration is added.
It requires a broker-configured session, fresh unclaimed CPU channel, shared clock
mapping, strict metadata plus ordered EOS and negotiated actual consumer COPY.
Native memory, GPU readback, native adopted channels, native memory-policy objects
and queue-age dropping cannot be combined with this route. Windows rejects it.

The new `flSharedCpuQuery` reports immutable session/local role/operations/flags
and pool count, preserving outputs on failure. This checks actual declared COPY,
not merely permission to copy; both-endpoint permission was enforced at setup.
Caps geometry/format derive from the negotiated CPU pool/session. No native image
cast, GPU allocation/map, implicit conversion or fallback is used.

Each source frame copies logical rows from the cached read-only shared mapping
into independent standard GStreamer system memory, accounting for source stride,
plane offset and GStreamer destination padding. The transport read lease is
released before publishing the output buffer. Retaining a GstSample cannot stall
or expose later transport overwrites. One logical CPU copy is explicitly traced
(`gst.shared_cpu_copy_source`) including work later rejected by timing checks.
This is a cached copy-out path, not zero-copy output or a pooling/rate claim.

Strict sequential identities and session media generation are checked. PTS and
DTS use the existing shared receiver mapping; duration, discontinuity and metadata
are preserved, including overflow checks. Missing mapping fails, never rebases.
The existing pending-delivery mechanism protects already-copied source output.
Ordered EOS returns EOS after the exact frames and publishes terminal count/PTS;
peer loss is an error, not EOS. Empty streams are supported.

Unlock cancels the session and shuts down blocked CPU receive; teardown does not
wait out a60-second socket timeout. A new generation/channel is required after
cancellation. Same-generation pause/resume and flushing/seek controls are NOT
qualified here; do not infer them from old/native pipeline tests.

## Validation

Linux25/25 selected CPU/contract/coordinator and existing GStreamer queue,
NV12/RGB10, timeline/bin tests pass. Android13 selected runtime binaries pass;
both Android ABI builds pass. After the final trace-placement correction and
stronger cancellation fixture, the new source test was rebuilt/rerun on Linux
and Android and both ABI builds passed again. Other broad results precede only
that localized change, not a claim of full final-head milestone qualification.

The new pipeline test verifies eight BGRA and eight NV12 frames with padded
shared strides, a gap between NV12 planes, every logical pixel, mapped PTS/DTS,
duration, media generation and explicit discontinuity. It retains the first
output while the producer reuses its slot. Missing actual COPY permission,
missing mapping, stale media generation and peer death fail; empty EOS succeeds.
With a live source task and no frames, teardown interrupts a60-second receive
within the fixed1-second gate. Final Linux trace records17 explicit copy events,
13,824bytes (16 delivered frames plus the intentionally rejected unmapped frame),
all operation=COPY. Source hashes, exact commands and raw logs are in
`shared-cpu-gst-source/`.

Next: CPU sink adoption/upload with declared copies, then coordinated A/V-bin
selection, broker/local rendezvous, event/flush/pause handling and sustained rates.
M6.8b/c and M6.9 remain open; no Windows CPU allocator or end-to-end A/V claim.

Event-stream support has since been integrated and qualified in SHARED_CPU_GST_EVENTS.md; the initial event-stream rejection described above is superseded. Audio-track rejection remains until A/V qualification.

# Bounded producer/consumer startup coordination

## Root cause and boundary

The original control READY means video session setup exists, not that the audio
device has prerolled. The producer epoch was chosen 500 ms after the initial
clock exchange, before pipeline construction and device opening. While the
consumer waits for preroll it does not drain/render video. The producer keeps
sending, fills the eight-slot native ring and loses frames at the unchanged
5 ms acquisition timeout. A stale start epoch also permits a catch-up burst.

This is distinct from the previously fixed selected-device-clock startup phase
and renderer pop/refill ordering. A steady-state FPS number hides startup loss.

## Fix

ProducerMapped streaming requests control mode 2. Modes 0 (verification) and 1
(legacy audio-preferred playback) remain unchanged. Older producers reject 2
explicitly; **no silent fallback**. Both peers must be updated.

- Producer allows exactly one PCM block to prime the consumer; all video and
  subsequent PCM wait at bounded upstream pad gates.
- Consumer waits for actual audio sink preroll, then requests final epoch.
- Producer chooses a fresh common epoch, distributes it to every PLAYING child
  (updating only the bin does not propagate base-time in PLAYING), and sends it.
- Consumer maps it through the established clock mapping, installs audio and
  renderer clocks, then acknowledges GO. Producer releases both gates.
- Source PTS, durations, segment mappings and intentional A/V offsets are not
  rewritten. The new shared origin changes startup wall-clock latency only.
- Queues are unchanged. Existing timeout/cancellation bounds apply. Per-session
  first-clock callback carries final timing; onSession's config epoch is provisional.
- Arrival-drift slope starts after priming; intentional device setup time must
  not be reported as sustained producer clock drift. Continuity checks still
  include the initial PCM block.

This implementation targets the existing finite fixed-rate external-pipeline
contract; it does not add arbitrary live-source or seek semantics.

## Validation commands

```
python3 test/m5_startup.py --build BUILD --output EVIDENCE
FRAMELINK_TEST_PREROLL_DELAY_MS=700 python3 test/m4_app_offsets.py \
  BUILD/framelink_av_producer BUILD/framelink_av_consumer BUILD/libgstframelink.so \
  --clock-policy producer --output EVIDENCE_OFFSETS
FRAMELINK_TEST_PRODUCER_CLOCK=1 FRAMELINK_TEST_PREROLL_DELAY_MS=700 \
  python3 test/m4_recovery.py --build BUILD
```

`FRAMELINK_TEST_PREROLL_DELAY_MS` is an explicit test injection, bounded by the
session timeout. It holds startup after PCM admission and before preroll completion.

Linux RTX3090 decoded 160x90@50, induced 700 ms setup:

- Coordinated BGRA and NV12: 100/100 unique received frame identities,
  100/100 PCM blocks, zero ring-full drops; hashes/PTS/durations preserved.
- Legacy uncoordinated negative control: 65/100 submitted, 62/100 received,
  35 ring-full drops for each format. Startup-completeness assertion rejects it.
- Intentional 37 ms and 123456789 ns audio offsets preserved; reset rejected.
- Producer-clock recovery normal, bounded, pause/fresh session and reset/restart:
  4/4 pass with 700 ms induced preroll.

These are startup transport and timeline results, **not physical presentation or
lossless rendering qualification**. The standalone CPU consumer's appsink/drop
policy schedules only about 51/100 frames even when transport delivers all100;
this work does not relabel that separate limitation as fixed. Windows/WASAPI
validation and independent review are required before deployment.

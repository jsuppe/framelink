# Bounded finite A/V sample runs (mode 3)

This opt-in sample/playback API extension permits finite qualification beyond
60 seconds. It is **not** infinite/live support, a new FrameLink transport ABI,
or a change to media timing, pool sizes, application queues, or drop policy.

## Contract

- Producer/consumer CLI: `--media-run-ms N`; renderer: `--av-media-run-ms N`;
  shared `av::PlaybackOptions::mediaRunMs`. Zero/default in the API keeps the
  legacy deadline and modes. Explicit CLI values must be decimal digits in
  **1..300000 ms** (five minutes). Signs, whitespace, suffixes, zero and overflow
  are rejected. `--timeout-ms` / `--av-timeout-ms` remains **100..60000 ms**,
  default **15000 ms**; do not scale it with media length.
- Opt-in requires coordinated ProducerMapped streaming, one sample session,
  and no pacing injection. `frames` remains 1..100000, fps 13..240. The declared
  duration `ceil(frames * 1000 / fps)` must fit the agreed media budget. Timeline
  duration, not frame allocation count, determines this bound.
- The existing authenticated sample control mode word is **3**, followed by
  one little-endian uint64 budget from consumer and the identical producer
  echo. Both local budgets must match exactly. This occurs before audio setup,
  READY/PRIMED/GO. Producer mode <=2 behavior is unchanged when opt-in is absent.
  Old producers reject unsupported mode 3; new producers with a finite budget
  reject legacy consumers. There is no downgrade or silent deadline extension.
  `flSessionConfig` wire layout/version and image/fence ownership are unchanged.
- The sample process watchdog initially retains its setup timeout. Only after
  the coordinated GO does the finite active deadline become
  `mediaRunMs + timeoutMs`. Playback API and external-source EOS waits use the
  same bounded active phase. The timeout term is drain/grace, not added playback
  latency. Nonzero audio offset, common latency and the 50 ms epoch lead consume
  that grace; an insufficient explicitly selected grace may fail, not extend.
  One sample process is bounded by setup + media + grace (at most **420 s**;
  active phase at most **360 s**). Embedded recovery attempts remain separately
  bounded by the existing retry limit; an intentional pause retains existing
  semantics, not a new live-run permission.
- Socket authentication/steady operations retain their existing rounded **1–2 s**
  idle timeout. Mode 3 also watches producer video and PCM pad progress (until
  each EOS) and consumer decoded PCM progress with the same idle interval.
  Early PCM EOS/incomplete sample duration fails. Cancellation remains polled
  in the existing receiver loop and startup waits. Reaching EOS does not permit
  an infinite drain: active deadline and the completion control-read timeout
  still apply. These are application/operation deadlines, not a guarantee to
  detect a stopped receiver before OS socket buffers fill; this sample protocol
  has no new heartbeat. A stalled process itself cannot execute its watchdog.
- Bounds are validated before arithmetic: <=100000 frames makes `frames*1000`
  safe in uint64; <=300000+60000 ms is safe before conversion to signed 64-bit
  monotonic microseconds. Untrusted uint64 wire budgets are checked before use.

## Qualification harness and regression coverage

`test/m5_renderer_rate_probe.py --native --seconds 70 --clock-policy producer
--trace on|off` uses the same source and 15000 ms startup timeout on both peers.
It records exact commands, real process exits, status and process CPU samples;
its outer deadline is media duration +35 s. It refuses existing case output
folders. Each trace thread has a fixed 524288-event capacity; captures must
verify footer loss/rejection counters and cannot treat a truncated trace as a
pass. Trace-off has aggregate evidence only. Trace-on includes retirement/GPU
query diagnostics and ring tracing; overhead comparisons include those costs
and exclude end-of-process CSV flush from sampled CPU.

`m5_finite_budget.py` runs actual peers: 4 s playback with a 2 s startup bound,
missing peer, unequal budgets, legacy-mode mismatch, short EOS, suspended
producer/consumer, and API cancellation. Suspension affects only fixture
children. A stopped consumer can accept a whole short fixture into kernel
buffers; producer completion then fails on the unchanged 2 s control timeout,
not the 70 s media budget. Unit coverage checks max/overflow wire values and
strict numeric parsing. The sustained native runs separately prove >60 s real
playback; the short regression is not advertised as that proof.

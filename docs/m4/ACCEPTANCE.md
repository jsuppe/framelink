# M4 synchronization acceptance and evidence contract

Status: acceptance criteria, not test results. M4 only; broad device deployment
and release soak gates remain M5/M6. Each result must record revision, platform,
actual sink/clock, scenario, queue high-water marks, and observed limitation.

## Timing invariants

* Source media PTS is interpreted through its original TIME segment. Segment
  start/time/base/rate are not interchangeable. A normalized zero segment is
  legal only after converting buffer PTS and duration consistently.
* Audio PTS addresses the first sample. Never infer audio timing from video
  sequence or transport arrival. Sample accumulation must avoid rounding drift.
* Both sinks use one receiver epoch and clock mapping. No per-track first-arrival
  anchoring; no timestamp replacement by reception time.
* Audio-master means the active audio sink actually supplies the selected clock.
  A system-clock or silent-sink fallback must be named and reported as fallback.
* A presentation deadline includes the common pipeline latency exactly once.
  Device latency and renderer refresh/submit latency are distinct quantities.
* GPU-ready/retirement fences do not establish physical display presentation.
* Recovery either resumes the current valid generation or flushes both tracks
  and establishes a new shared epoch. Stale frames/audio cannot cross generations.
* Continuous playback cannot depend on a per-video/audio-pair verification ACK.
* Queue capacities include hidden queues (appsrc, appsink, GStreamer queue,
  socket buffering and retained native GPU frames); bounds must be documented.

## Required cases

1. Nonzero segment start=5s, base=3s, source PTS=6s maps to running time=4s.
   Different source segment encodings of the same instant map identically for
   video and audio. Nonzero receiver base remains common; arrival delay has no
   influence. Invalid/out-of-segment timestamps reject or report discontinuity.
2. Distinct audio and system test clocks: demonstrate selected master identity,
   not just matching numeric timestamps. Missing audio clock produces explicit
   fallback or failure. CLOCK_LOST must trigger joint re-anchoring.
3. Reported audio latency 0/20/100ms: both scheduling deadlines shift together;
   do not delay only video or double-apply sink latency. Headless evidence is
   scheduling evidence, never physical lip-sync qualification.
4. Early/on-time/late video: deterministic hold/present/drop; finite capacities;
   retirement callback occurs only once and only after GPU ownership ends.
5. Positive and negative sustained drift: distinguish producer pacing changes
   from deliberate timestamp corruption. Report actual correction mechanism and
   bounded A/V offset; audioresample presence alone is not correction evidence.
6. Audio stall while video continues; video stall while audio continues: no
   deadlock or unbounded lag. Explicit underrun/gap counters and recovery state.
7. Pause/resume: no stale backlog burst. Account for stopped playback-clock time
   and rebuild common epoch if needed; no independent per-track offsets.
8. Session replacement/timestamp reset: old generation purged, old native frames
   retired safely, new generation reaches playback with shared mapping.
9. Active-audio selection and mute: mute does not silently change master timeline;
   selection flush/re-anchor policy explicit. Source removal cannot leave stale
   clock pointers. Nonselected visible sessions remain valid.
10. Audio device change/loss: real-device test where available; deterministic
    simulated clock loss separately labeled. New device latency re-queried.
11. Original video-only low-latency mode remains independent of audio scheduling.
12. Pipeline EOS/peer death/slow consumer: bounded teardown and explicit outcome.

## Physical flash/click measurement

Use one acquisition timebase for screen light and speaker sound (photodiode plus
microphone sampled by the same multichannel ADC, or independently qualified
high-frame-rate A/V recording). Generate matching flash/click source PTS at
regular and randomized phases. Record renderer mode, refresh rate, audio device,
reported latencies, microphone distance/acoustic delay, acquisition calibration,
and sample/frame quantization uncertainty. Measure signed audio-minus-video
onset offset and distribution before/after stalls and over a sustained interval.
Do not compensate with independent recording clocks. Software timestamp logs,
screenshots, virtual displays, fakesinks and loopback PCM are not this test.
Numerical physical pass thresholds must be agreed before claiming a pass.

## Review/CI gates

Independent fixtures must fail if PTS is restamped, the two epochs diverge,
latency is applied once to only one track, stale generation is accepted, or queue
limits are removed. Unit tests can prove scheduler policy; real pipeline tests
must exercise that policy and output measured actual clock/sink selection.
Linux/Windows validation must identify any hardware-unavailable skip. No blanket
M4 completion claim while physical alignment or drift recovery remains untested;
report implementation completion and qualification gaps separately.

# M5 Linux transport evaluation (2026-10-30)

Baseline: FrameLink `ed81cbb`, Release build, Linux host melchior, GStreamer
1.24.2; PulseAudio compatibility server PipeWire 1.0.5. This is a bounded
characterization, not a latency SLA, independent-clock qualification, physical
speaker measurement, or proof of all-platform readiness.

## Decision
Retain coordinated GStreamer GDP/loopback audio transport. No measured requirement
violation currently justifies a second custom PCM protocol. A requested latency
budget, concurrency/load profile, and sustained loaded tests are still needed
before declaring performance qualification. GPU memory allocation negotiation is
M5a, not a reason to replace PCM transport.

## Measured evidence
- `linux-transport-20261030/summary.json`: 15 runs, 100 pairs each, baseline,
  injected producer jitter and audio-leading cases. All 1,500 pairs preserve the
  exact intentional source A/V PTS offset. Source-mark-to-audio-arrival median
  ranges 0.136–0.612 ms baseline, 0.408–0.615 ms jitter, 0.187–0.612 ms audio leads.
  Worst per-run p99: 0.672, 0.697, and 2.442 ms respectively. Full JSON preserves
  maxima and video/differential distributions; do not report p99 as a hard bound.
- CPU averages per 100-pair fixture: baseline 0.170 s, jitter 0.233 s, audio leads
  0.294 s. Includes setup, GPU/video handling, generation and verification, and
  logging. **Not isolated audio CPU cost or real-time utilization.**
- `linux-authenticated-20261030`: three real app cases pass: 37 ms and
  123.456789 ms offsets preserved; timestamp-reset negative rejected. This uses
  production authentication/GIO reader, unlike the lower-level timing fixture.
- `copy-cost.jsonl`: 100,000 hot reused-buffer `gst_buffer_fill` calls per size.
  3,840-byte copy averaged 71.23 ns on this host. This isolates the explicit
  receiver copy primitive; excludes allocation, kernel copies, GDP handling,
  thread dispatch and cache misses. It is **not total PCM delivery cost**.

### Measurement boundaries and uncertainty
The native fixture uses actual FrameLink video and GDP over loopback TCP with
unsynchronized appsink arrival probes. It is single-process and serially verifies
pairs, so it is not a production concurrency/throughput test. Production apps
use authenticated GIO sockets and a chunk reader, which the timestamp-offset
smoke covers but which has not been per-chunk latency-profiled here.

Source marks occur before buffer filling; arrival probes occur before appsink
synchronization. Thus generation/fill/queue time is included. Both use the host
monotonic clock with microsecond representation (1,000 ns quantization). There is
no cross-machine clock offset; scheduling/probe perturbation is not calibrated.
Presentation time and physical output are **not measured**. No equivalence
between arrival skew and output lip sync is asserted.

## Source-level buffering/copy audit
`samples/av_apps.cpp` producer: bounded appsrc (2 buffers), queue (4 buffers,
80 ms media-time ceiling), GDP serialization and GIO stream sink. TCP/socket
buffers add buffering beyond those media queues; queue limits are not a global
end-to-end latency bound. Video and audio PTS are not replaced by arrival times.

Receiver `AudioReader`: read up to 4,096 bytes into stack memory, allocate a
GstBuffer and `gst_buffer_fill` into it, then bounded appsrc (4 buffers), GDP
depayload and audio output. The stack-to-GstBuffer operation is a definite
user-space copy. Read/write kernel transfers, GDP adapter behavior and audio
conversion/device-ring copies are additional implementation-dependent costs;
this audit does not assert an exact total copy count. `audioresample` alone is
not clock discipline; concrete sink slaving and common epoch policy matter.

Deployment requires matched GStreamer core/app/GDP/GIO plugins, applicable audio
sink plugins, native FrameLink dependencies, and a private rendezvous directory.
The sample trust boundary is same-host authenticated peers, not a secure remote
network protocol. The current consumer CPU GStreamer video path must not be
advertised as native GPU zero-copy.

## Linux output capability and failure qualification
Only `auto_null` is present; no physical audio device is exposed. A temporary
named null sink was created, played through real `pulsesink`, and removed in a
finally block. Existing default sink was not changed. Five checks passed:
playback, observed missing-device fallback, missing-server failure, invalid-format
rejection, dedicated module cleanup (`linux-output-20261030/results.json`).

**Important:** on this PipeWire server `pulsesink device=nonexistent` succeeds
using server fallback. This is not evidence that the requested device exists.
Applications promising strict endpoint selection need explicit selection checks
and route-change handling. The test records this local behavior rather than
requiring it on all PulseAudio versions. Invalid format is rejected during
pipeline construction, not by physical hardware negotiation.

Physical Linux display scanout/speaker alignment and system suspend/resume remain
unqualified here. `/dev/dri/card1` and renderD128 exist, but device presence alone
is not display qualification. No shared host suspend or user display reconfiguration
was performed. Windows/Android qualification is owned by separate M5 workstreams.

## Reproduction
```
cmake -S . -B build-m5 -DCMAKE_BUILD_TYPE=Release
cmake --build build-m5 -j4
python3 test/m5_transport_benchmark.py build-m5 --output /tmp/m5-timing
python3 test/m4_app_offsets.py build-m5/framelink_av_producer build-m5/framelink_av_consumer build-m5/libgstframelink.so --output /tmp/m5-auth
python3 test/m5_linux_outputs.py --output /tmp/m5-output
c++ -O2 test/m5_audio_copy_cost.cpp -o /tmp/m5-copy $(pkg-config --cflags --libs gstreamer-1.0)
/tmp/m5-copy
```
Output test requires a running PulseAudio-compatible service and module-null-sink.
No CTest hardware gate is added: device behavior depends on the selected server.

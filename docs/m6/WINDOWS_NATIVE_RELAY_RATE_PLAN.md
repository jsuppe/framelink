# Authenticated Windows native relay rate qualification

Status: **synthetic harness validated at 11d1b65: focused build, tiny smoke,
and all four matched short target pairs passed; BGRA1080p60 1800s passed**.
See [smoke evidence](windows-native-relay-rate-smoke-11d1b65/README.md) and
[target pairs](windows-native-relay-targets-11d1b65/README.md). The separate
[paced real-decoder extension](windows-decoder-rate-harness/README.md) has
passed focused Windows checks and the1080p60 short pair at77afa1a. The first
explicit4K30 replay pair atb247e5d passed trace-off but failed two trace-on
deadlines;4K30 remains unqualified. The newer helper462fa5f is included in
[the successful a50841a integrated batch](windows-integrated-a50841a/README.md):
178/178 tests, API checks and3/3 unchanged clients. This is not duration acceptance.
The [final-candidate native NV12 1080p60 short pair](windows-native-final-nv12-1080-a50841a/README.md)
passed both arms. Its1800s trace-off run is active under root exec12979 / PID34508
as recorded in the ledger; no duration result is claimed.
Owner: Evelyn. Windows execution remains exclusive; consult the
[current execution ledger](LOW_LATENCY_PLAN.md#authoritative-execution-state--2026-11-12)
for the live owner before starting any runtime work.

## Why a new harness

The existing authenticated FAR4→FAR5 three-process fixture proves functional
forwarding, PCM, terminal handling and retained ownership for eight64×32 frames.
Its per-frame parent events intentionally serialize execution and cannot prove
1080p60 or4K30 behavior. The older native boundary benchmark has pacing and GPU
completion but inherits textures and uses a different audio/control setup.
Neither is silently relabeled as this qualification.

Add a separate **test-only** paced executable using the existing local setup,
consumer snapshot, `SessionNativeRelay`, `SessionPcmRelay` and supervisor. Keep
all existing regression modes and public contracts unchanged.

## Frozen observable requirements

- Source, relay and final reader are separate processes. Texture handles are
  authenticated/pulled by FAR4/FAR5, not inherited from the harness launcher.
  Two slots, one unread frame, zero configured transport hold; no hidden fallback.
- Autonomous source pacing at30/60fps, independent100Hz PCM (480 stereo48kHz
  sample frames per block). Keep video37ms ahead of audio on the source timeline.
  Parent control events are setup/final-ownership controls, not per-frame pacing.
- Expected identities/counts derive from explicit configuration, never from the
  received set. Source payload carries a non-wrapping identity and timestamp;
  receiver verifies the payload independently of relay logs. Preserve exact
  generation, PTS, DTS, duration and PCM continuity.
- All roles record same-domain monotonic times, with overflow-safe conversion.
  Record source schedule/acquisition/upload/submit, relay acquisition/forward,
  final acquisition and actual GPU completion. Report each distribution separately.
- Validate original allocation identity at relay setup. Reader holds the genuine
  FrameLink lease through GPU completion. A held-last-frame control verifies
  upstream remains pending until actual completion; no timeout or control event
  itself may authorize early reuse.
- Real authenticated END/EOS and exact video/PCM terminal counts are mandatory.
  Publication completion and native drain are separate observations.
- Source upload and final private verification-copy/readback are explicit work,
  not zero-copy claims. The native-video relay must perform no payload map/copy/upload/readback.
  PCM transport/verification is recorded separately and is not a video zero-copy claim.
- Capture raw resource series for all three processes. Apply the existing
  steady-state second-half regression method at1800s: memory≤64KB/min,
  handles≤1/min, threads≤0.5/min. Short tests do not establish these slopes.
- Keep17ms(60fps)/34ms(30fps) whole-path arrival diagnostics. Report every miss
  and loss, including startup and drain. These are not isolated-hop budgets.
  Added-hop p95≤2ms remains **proposed**, not an approved acceptance threshold.
- Trace-off performance and explicitly bounded trace-on identity/copy accounting
  are separate runs. Trace-on must have zero dropped events/rejected threads;
  validate operation-specific joins rather than assuming absent records mean
  zero operations. Do not size every recorder to an unbounded endurance run.
- Failure must preserve partial evidence and terminate only owned child processes.
  A missing/truncated record, malformed footer or failed child is a failure,
  never an implicit skip or a reason to select a more favorable repeat.

## Validation order

1. Local analyzer negatives: missing/duplicate identity, changed PCM/PTS,
   premature completion, truncated data, trace loss, nonfinite/degenerate
   resource series. Include valid controls to prove usable schemas.
2. Focused Windows build and tiny-geometry functional smoke, retaining genuine
   authenticated peers, GPU completion, last-reader hold and terminal handling.
3. One target-resolution five-second trace-off preflight after review.
4. Matched short trace-on identity/copy accounting with explicit recorder capacity.
5. One1800s trace-off target cell only when its earlier gates pass. Preserve
   failures and investigate a concrete mechanism before another attempt.

No result here establishes physical scanout/acoustic alignment, arbitrary decoder
compatibility, real-decoder two-hop performance, other platforms, API freeze or
M6 completion. Existing startup/native-lifetime failures remain suspended rather
than waived. Product D2/D3 and isolated performance-budget decisions remain Jon's.

## Following gap: real decoder with authenticated discovery

Read-only inventory confirms the existing `gst_native_relay_decoder_win` passed
at461584b (1.02s):16 distinct decoded frames/25600 synthetic PCM sample frames,
reversed same-allocation identities,37ms offset, held-reader and terminal checks.
See [decoder relay evidence](windows-relay-decoder/README.md) and
[integrated receipt](windows-integrated-461584b/LastTest.log). That fixture is
broker-composed with inherited handles and per-frame controls, not FAR4→FAR5
discovery or rate qualification. Existing automatic authenticated decoder A/V
evidence comprises four tiny same-process sessions; it is not a target-rate
three-process result.

The stock `d3d11h264dec` private decoder arrays are not directly exportable into
this pool. Supported integration uses `FrameLinkD3DProducer::acquire()`, the exact
shared GstD3D11Device wrapper and `gst_d3d11_converter_convert_buffer()` into the
issued reservation, retaining decoder storage until actual GPU completion, then
`submit()`. This needs GPU_SAMPLE|GPU_RENDER and explicitly performs conversion.

After the new synthetic harness passes its focused Windows gate, the next bounded
validation implementation is a separate authentic-discovery decoder-conversion
fixture. First retain the proven320×240 decoder input/tiny output, distinct
decoded-pixel digests, actual decoder PTS/duration/absent DTS, synthetic PCM37ms
alignment, held-reader proof and explicit conversion/oracle-readback accounting.
Then extend to paced target resolution. No new public decoder API is justified;
only transport/relay **after conversion** can claim no payload copy at that boundary.

## Decoder functional gate completed; paced extension partially validated

The tiny authenticated decoder fixture passed at4d38a09:
[source, build and runtime evidence](windows-local-decoder-4d38a09/README.md).
The separate paced extension passed a clean build,9/9 focused tests, tiny smoke,
and both short1080p60 trace arms with full SHA256 at77afa1a. The4K30 preflight
failed timing despite exact distinct frame/PCM delivery; a bounded encoded-AU
queue control atc539d99 also failed. See [CNG results](windows-decoder-cng-77afa1a/README.md)
and [queue control](windows-decoder-queue-c539d99/README.md).

The completed [source-only diagnostic](windows-decoder-source-throughput/README.md)
measured clean150-frame generation5.1418495s and generation/encoding5.5626633s;
those wall times include startup/drain. Separate instrumented arms established
exact150-buffer/PTS accounting, not a clean performance result. This does not
waive either on-demand4K failure.

Atb247e5d, [explicit compressed-AU replay](windows-decoder-replay-b247e5d/README.md)
moves generation/encoding into a separate preparation step while preserving
actual decoding, full-pixel SHA256, PCM and real native completion. The first
five-second4K30 trace-off arm passed150 distinct frames/240000PCM with zero34ms
misses (max30.735534ms). Its matched trace-on arm retained exact media and passed
trace accounting, but missed deadlines for identities116/117 (max36.6673ms).
The pair therefore **FAILED**; no decoder endurance run has occurred. Source
oracle readback/hash reached31.6217ms. The completed
[isolated source-oracle diagnostic](windows-decoder-oracle-a50841a/README.md)
recorded both150-frame/240000PCM arms with zero34ms misses, maximum arrival
23.4155/25.0974ms, and Map/SHA/Unmap phases. The original outlier did not recur.
Added probes and buffered source CSV change observation overhead: this is not a
qualified result, causal fix or waiver of the original pair. No diagnostic repeat
is planned. See the ledger for the exclusive owner and subsequent ready work.

The immutable synthetic11d1b65 [BGRA1080p60 duration](windows-native-relay-duration-bgra1080-11d1b65/README.md)
passed1800s,108000 video frames/86400000PCM frames, zero17ms misses and resource
gates. This does not qualify the newer checked helper, remaining native duration
cells, decoder performance, or physical presentation.

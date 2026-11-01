# Combined Windows performance qualification (2026-10-31)

Isolated FrameLink source: 7f73ea8 +713dbec +5d228c7 +a260601.
Isolated renderer source: f3a856e +b8a1489 +811cc6c +c315830 +ce86138 +7a09fe8 +17d88a2.
Caspar RTX5090; MSVC Release; GStreamer1.28.0. No existing demo modified.

## Instrumentation and interpretation

Acquisition FPS is measured from first/last successful native acquisition trace
completion events, not configured FPS and not monitor scanout. Native present
submission counters do not prove physical display refresh or audio output timing.

The combined renderer places non-BGRA metadata into the bounded six-frame queue
under FL_GPU_SYNC, then checks the imported producer-ready timeline with a
zero-timeout host wait before CPU mapping. A timeout retains the frame. FlRelease
is bookkeeping under GPU_SYNC; consumed fence signals are ordered after prior
render GPU reads. CPU-converted mirror slots share the source slot's retirement
lifetime, so no original or mirror slot can be recycled before consumption.

## Validation layer setup

Downloaded official LunarG SDK1.4.363.0 from
https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/vulkan-sdk.exe .
Authenticode status Valid, signer LunarG, Inc. Used 7-Zip to extract the layer DLL
and manifest to C:/Users/jonsu/dev/vulkan-validation-local/Bin. No installer run,
registry changes or global SDK installation. Process-local VK_LAYER_PATH plus
VKR_VALIDATION=1 selects it. DLL SHA256:
2ACC317EF880F73A9862F23A964694C03F531B866FAB6D70F1412FCBAE60CAAC.

## Known failed checks retained

- Initial test setup lacked framelink.dll alongside nested sample EXEs. Renderer
  builds it into top-level Release. Copied the exact built DLL beside sample
  EXEs; initial no-load attempt is not a performance measurement.
- Explicit wasapisink is not an allowed CLI value; corrected to autoaudiosink.
- Delayed-startup completeness on Windows FAILED: producer submitted 100/100,
  zero ring-full drops, but each standalone GStreamer consumer traced 95/100.
  BGRA missing PTS 0,20,40,80,120ms; NV12 missing20,40,60,100,120ms.
  All >= 140ms frames survived. appsink max-buffers=3/drop=true plus synchronous
  100 ms scheduled consumption is a likely receiver-side mechanism. This gate
  must not be described as passed just because producer submission is complete.
- Intentional37 ms and123456789ns offset tests and timestamp-reset negative
  control passed 3/3 with autoaudiosink/producer clock; this does not erase the
  observed startup frame loss.

## Combined sustained measurements

Each case 45 seconds, source phase and trace enabled, no browser preview:
- 160x90@50 BGRA/NV12:49.92fps acquired (2238/2250 input frames each).
- 1080p30 BGRA:30.02fps,1346/1350; NV12:30.04fps,1347/1350.
- 1080p60 BGRA:59.04fps,2647/2700; NV12:59.11fps,2650/2700.
All six producer/renderer exit codes 0. All complete 2,160,000 audio samples, no late
audio blocks. Native queue <= 6, no invalid PTS or failed imports; each 60 FPS case
has one late video drop. This recovers throughput, not lossless delivery.
The 160x90 BGRA case overlapped a short independent offset test and is supporting,
not isolated baseline evidence; the four 1080p cases were run without another
media workload.

SSH runs elevated, so Vulkan loader ignored VK_LAYER_PATH. Those initialization
failures are preserved and are NOT validation passes. Re-ran through a Limited
interactive scheduled task to honor process-local layer discovery. Manifest and
renderer log explicitly confirm Khronos layer enabled there.

## Windows GPU and native presentation

- Combined renderer CTest: 16/16 passed, including SIMD pixel equality and pacing.
- NV12 1080p60 with Khronos enabled: producer/renderer exits 0, zero VUID or
  Validation Error messages, queue bounded at six.
- 40 ms actual GPU execution gate plus 40 consecutive injected import failures:
  exits 0, 96 gate checks recorded, all 600 audio blocks complete, zero validation
  errors, no invalid PTS. It resumed scheduling after the failures.
- Native Vulkan demo was visibly captured on the interactive desktop (screenshot
  retained locally, not uploaded here). 1,201 present submissions, zero present
  errors; full 500 audio blocks. 447 due video decisions and 12 late drops mean
  this is functional native presentation, not full-rate/lossless qualification.

## Additional native-output bottleneck isolated

Matched 10-second NV12 1080p60 native-window cases:

- With Khronos, default 100 ms common latency: 561/600 submissions,
  558 acquisitions, 56.64 acquired FPS, 1,282 presentation submissions, 0 errors.
- Without Khronos, same 100 ms: 560 submissions, 554 acquisitions, 56.28 FPS.
- Without Khronos, ONLY common latency reduced to 80 ms: 599 submissions,
  594 acquisitions, 60.46 FPS. This is a causal diagnostic control, NOT a change
  to the production default or an assertion that 80 ms is the universal budget.

The 100 ms case reports 39/40 acquire-buffer timeouts around 5 ms, recurring
roughly every 230–270 ms, with 56 submissions in each media second. It is not
only startup loss and is not validation overhead. An eight-slot ring has no
adequate spare capacity when the 100 ms / 60 FPS delay occupies six pending
frames plus the retained current frame and writer/in-flight frame. Reducing
hold time releases the capacity bottleneck. Ring/scheduler compatibility needs
an explicit policy or retirement redesign; simply claiming the earlier
headless 59 FPS result as native-window qualification would be incorrect.

## Remaining gates

Standalone source draining/startup losses and the native default-latency/ring
budget remain unresolved. Physical scanout/optical audio-video alignment remain
unmeasured. The root coordinator received both failure mechanisms while tests
were still running. No existing demo was replaced; all tasks were finite,
manual-only, isolated test tasks. No production source changes in this report.


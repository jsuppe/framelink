# Explicit shared-CPU benchmark selection

The existing independent-process A/V benchmark now accepts `--shared-cpu` in
bin mode on Linux/Android/Windows. It selects the public authenticated CPU session path
on both ends, propagates selection to child processes, and labels summaries
`shared-cpu` rather than conflating this path with native-memory CPU readback.
Default behavior is unchanged. Native-memory, element mode, source-only,
local-consumer and lifecycle combinations reject before launching children.
Windows uses the same explicit CPU selection after the allocator, authenticated
named-pipe setup and local bins were implemented. See
[Windows benchmark integration](windows-cpu-benchmark/README.md).

Small traced smoke: BGRA and NV12, 64x64 at30fps for5s on Linux and Android.
Each independently accounts150video frames,240000PCM frames and complete output
queues. Each trace records exactly150sink copies and150source copies:
2457600bytes per endpoint forBGRA;921600bytes per endpoint forNV12. This is
**two explicit CPU copies**, not a zero-copy GStreamer path. Prior functional
fixtures qualify exact pixels; this benchmark checks counts and copy accounting.
Neither small smoke nor traced timing qualifies full-resolution rate/endurance.

Both Android ABIs build. Linux affected regression suite3/3 passed; Windows compatibility rebuild and2/2 tests passed. Five invalid
option combinations reject. The initial negative-driver assertion expected the
literal `CHECK`, whereas the macro prints the failed expression; the benchmark
correctly rejected the option. Driver corrected to check `!gSharedCpu`.
Windows preflight initially rejected a comment-only difference from local HEAD;
review found exactly three missing diagnostic documentation lines, with no code
difference. The retry explicitly allows that exact reviewed delta, retaining
historical-source checks for other drift. No unreviewed remote file overwritten.

Evidence scripts/logs/copy accounting are under `shared-cpu-benchmark/`.
Full-resolution rate/resource gates and Windows shared-CPU support remain open.

## First full-resolution sustained cell

Revision65a1268, Linux BGRA1920x1080@60,1800s:108000video frames and
86400000PCM sample-frames,180000contiguous audio blocks, complete queues and
zero17ms arrival misses. Video arrival p95=6.682544ms,max=11.908798ms;
common inferred A/V mapping delta=0ns. Both RSS slopes3.23KB/min pass64KB/min.
Consumer CPU24.08%; raw manifest and complete summary retained in
`linux-bgra-1080p60-sustained/`. This is transport-consumer evidence, not
physical presentation or independent full-stream pixel identity.

Three preceding attempts were interrupted by command ownership/timeout,
not failed benchmark verdicts; originals remain at the external evidence root.
The third established gateway dynamic-tool timeout600000ms despite requested
2000s, with no surviving process or result. Native execution completed and
was collected successfully. No threshold, duration or binary changed.
Other Linux formats/rates, Android endurance and Windows support remain open.

## Second sustained cell

Linux NV12 1920x1080@60,1800s, docs revision16ddeba with unchanged65a1268
binaries:108000video/86400000PCM,180000contiguous audio blocks, complete
queues and zero17ms video/audio arrival misses. Video p95=2.504085ms,
max=4.297075ms, inferred A/V mapping delta0ns. Consumer/producer RSS slopes
3.23KB/min each, CPU9.62%/13.07%; handle/thread slopes nonpositive.
Evidence: `shared-cpu-benchmark/linux-nv12-1080p60-sustained/`.
Same transport-only and inferred-mapping limitations apply; no physical output
or full-stream pixel-identity claim. Linux sustained cells2/4 complete.

## Third sustained cell

Linux BGRA3840x2160@30,1800s, docs448251e with unchanged65a1268 binaries:
54000video/86400000PCM,180000contiguous audio blocks, complete queues and
zero34ms video/audio arrival misses. Video p95=24.317275ms,max=27.683631ms;
inferred A/V mapping delta0ns. Consumer/producer RSS slopes3.23/3.24KB/min
pass64KB/min; CPU35.32%/45.96%, handle/thread slopes nonpositive.
Evidence: `shared-cpu-benchmark/linux-bgra-4k30-sustained/`.
Same transport-only/inferred-mapping scope, no physical-output or full-stream
pixel-identity claim. Linux sustained cells3/4 complete.

## Fourth sustained cell — Linux matrix complete

NV12 3840x2160@30,1800s, docs81996d0/unchanged65a1268 binaries:
54000video/86400000PCM,180000contiguous audio blocks, zero34ms video/audio
arrival misses, p95=10.382194ms,max=23.787211ms, inferred A/V mapping delta0ns.
Consumer/producer RSS slopes3.24KB/min each pass64; CPU17.99%/25.44%,
handle/thread slopes nonpositive. Evidence: `shared-cpu-benchmark/linux-nv12-4k30-sustained/`.
All four Linux BGRA/NV12 1080p60/4K30 cells pass count, frozen diagnostic
timing and resource gates. Transport-consumer scope only; not physical output,
full-stream pixel identity, zero-copy, or resolution of existing M6 acceptance
decisions. Android full-resolution qualification and Windows support remain open.

## Android full-resolution preflight: BGRA timing failure retained

Same deployed binary hash verified against local arm64 build. BGRA1080p60
5s delivers300/300video and240000PCM but fails17ms diagnostic:20/300 late
frames, p95=19.511436ms,max=37.949795ms. First11frames are late; nine other
misses remain, so startup exclusion would not explain all failures. Consumer
CPU97.36%, producer54.70%. Audio500contiguous blocks, zero misses, A/V mapping0ns.
No endurance launched for this failed cell. Five-second RSS slopes are not
endurance evidence. Root cause unresolved; Evelyn owns bounded copy/consumer
profiling before BGRA qualification can resume. No budget waiver.

Single-format control NV12 at identical resolution/rate/options passes300video/
240000PCM and zero17ms misses. Payload/format sensitivity is supported, not
proof of a memcpy root cause. Advance this ready NV12 sustained cell sequentially
while BGRA qualification remains suspended for diagnosis. Evidence directories:
`shared-cpu-benchmark/android-{bgra,nv12}-1080p60-preflight/`.

## Android sustained NV12 and next diagnostic

NV121080p60 1800s passes108000video/86400000PCM,180000contiguous audio
blocks, zero17ms video/audio misses, p95=12.031767ms,max=16.375776ms;
inferred A/V mapping0ns. RSS consumer/producer -75.10/-86.40KB/min,
CPU61.19%/30.76%, handle/thread slopes nonpositive.
NV124K30 5s preflight counts150video/240000PCM pass, but first3frames exceed
34ms (max48.378655ms); original startup failure retained, no endurance launched.

BGRA1080p60 traced diagnostic retains exact300copies per endpoint, but copy
events have zero duration: they cannot isolate memcpy time. srcCreate includes
waiting and is not copy cost. Local no-transport BGRA control300video/240000PCM
passes zero17ms misses,max0.99ms, CPU39.81% (includes local test-source cost).
This narrows investigation to transport/allocation/copy, not a proven root cause.
Source review finds new output allocation and full-buffer zero followed by
row copies on every frame. Next bounded Android memory control compares
allocation/zero/row-copy versus no-zero/contiguous-copy/reused allocation.
No production edit or threshold change yet. BGRA and4Kstartup acceptance open.

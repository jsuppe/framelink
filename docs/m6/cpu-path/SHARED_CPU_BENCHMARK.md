# Explicit shared-CPU benchmark selection

The existing independent-process A/V benchmark now accepts `--shared-cpu` in
bin mode on Linux/Android. It selects the public authenticated CPU session path
on both ends, propagates selection to child processes, and labels summaries
`shared-cpu` rather than conflating this path with native-memory CPU readback.
Default behavior is unchanged. Native-memory, element mode, source-only,
local-consumer and lifecycle combinations reject before launching children.
Windows explicitly rejects selection until its allocator/endpoint exists.

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

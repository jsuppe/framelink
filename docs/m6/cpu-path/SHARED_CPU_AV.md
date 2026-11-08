# Explicit shared-CPU video with coordinated PCM

Linux/Android CPU elements now accept A/V coordinator contexts only with an
already authenticated event stream. Caller-provisioned authenticated PCM and
broker-authorized CPU sockets remain required; no discovery or implicit fallback
is added. Existing per-track readiness, common mapping and terminal accounting
are reused. Native paths and public overload defaults remain unchanged.

Six integrated cases exercise real appsrc → CPU sink/source → appsink video and
appsrc → authenticated GDP PCM sink/source → appsink in shared sender/receiver
pipelines. BGRA and NV12 logical pixels and all PCM bytes match; eight video
frames and eight 1ms stereo S16/48kHz PCM blocks preserve the independent 37ms
source offset through a common +1ms mapping. Audio-first and video-first EOS
submission and empty streams pass. The receiving pipeline cannot publish aggregate
EOS while the other input remains open. Deliberately contradictory terminal
accounting and authenticated PCM peer shutdown produce ERROR, not EOS. Teardown
is bounded below one second in every case.

Linux five affected tests pass, Android five runtime binaries pass, and both
Android ABI builds pass on final source hashes in shared-cpu-av/. No negative
failed during this task. The two actual terminal *arrival* orders are not forced:
only application EOS submission orders are controlled. The conflict test injects
a coordinator observation, not a forged authenticated GDP packet. Broader audio
format/hostile coverage is supplied by the existing audio-source regression, not
by these integrated cases.

Not rate/endurance, physical output, pause/resume or convenience-bin qualification.
Next: explicit selection in A/V bins and authenticated CPU rendezvous. Windows
shared-CPU allocator still pending. M6.8b/c remain open; M7 maintenance hold remains.

# Android GPU benchmark provenance

TCL8483A,API35,arm64-v8a,Mali-G52MC2; NDK28 API29 Release.
Binary SHA256 `c24f793a216ab898f8b273b8a928d70614df4c15faa89aca484bdd3cb43e46c0`.
Source based on1bad0b4 plus recorded shader/FBO preflight and mismatch diagnostics.
1920x1080 RGBA, two original AHardwareBuffers,60Hz,3720frames/120warmup.
Same-UID command-line processes: this does not extend signed-app security tests.
CPU source fills AHBs; GPU consumer samples original AHBs into private output.
Private36byte verification readback is explicit. No PCM/physical presentation.

Cold-start negative: first acquire returned at3308884449042293ns; GPU retirement
at3308884482239831ns; private readback finished3308884491479754ns. Frame1 arrived
3308884463168139ns; frame2 overwrote it at3308884486316447ns. Consumer expected1
but receivedPTS33333333(sequence3). This is documented latest-frame-wins behavior
under initial reader delay, not an unexplained transport loss. Startup shader/FBO
preflight precedes independent pacing; no per-frame control handshake was added.
Cold-start no-drop qualification remains failed, distinct from warmed throughput.

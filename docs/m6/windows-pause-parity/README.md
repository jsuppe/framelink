# Consumer-pause parity: cancellation reporter race

The clean integrated `0df689aeb31701af7e05927fc382ce957becfa3d` Windows run
completed build and API inspection (119 exported names unchanged), but failed
`gst_av_parity`: **142/143 passed**, 570.85s. All eleven subsequent diagnostics
were not reached. Original logs remain in
`<linux-home>/dev/evidence/m6-cpu-path/windows-integrated-0df689a`.

All four consumer-pause combinations reported the same exact consumer
receive-queue replacement error, after 13 of 22 pushed video frames. The
producer cancellation was reported by the audio sink in element/element,
element/bin and bin/bin, but by the video sink in bin/element. The respective
messages are `required session tracks not ready or cancelled` and
`coordinated session not ready or cancelled`. Both call the same shared
`fl_gst_session_wait` check; stream scheduling determines the first bus error.
Comparing those diagnostic strings verbatim is not a behavioral parity gate.

Scoped test-only repair: require the exact consumer queue-overflow cause,
require one of those two specific producer cancellation errors, require
incomplete video delivery and both sides to finish within six seconds of
resume, then compare the common cancellation class. Unexpected errors, timeout,
EOS, successful full delivery or another consumer cause still fail. Raw
messages remain in the log. No production, public API, wire or default change.

Focused validation evidence:
`<linux-home>/dev/evidence/m6-cpu-path/windows-pause-parity-0df689a`.
The remote test overlay is on the original exact base; it is not a clean
integrated revision. The original failure is retained, not waived by rerunning
unchanged assertions. API baseline remains 3b210e6, not frozen. M6 open/M7 hold.
Loom pre-edit briefing was attempted but the CLI is unavailable.

Validated: four successive focused pause runs (16 element/bin combinations),
then **4/4 affected Windows tests passed**,137.85s: native bin cancellation,
A/V bin, full parity matrix (108.62s), and event channel. Both producer error
variants occurred in the focused run. Build and all commands exit0. No broad
Linux/Android campaign was run for this test-only change.

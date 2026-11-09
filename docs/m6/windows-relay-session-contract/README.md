# Coordinated relay session contract prerequisite

Base: `0d2e40cb83e4c6545c49e4af5485cf757d432a72`; exact changed-file hashes in status.json.
Windows focused build, relay diagnostic and **6/6 regressions passed** (22.68s).

Added a reusable sample-level `SessionRelayContract`, exercised before video,
PCM, barrier and terminal forwarding in the Windows two-session relay fixture.
It requires distinct session identities, identical source clock/epoch/generation,
track identities and media formats; per-hop queue capacities may differ. An
explicit identity intermediate receiver mapping is required so decoded PCM
cannot silently acquire a second offset while raw video retains source PTS.
Final receiver mapping remains independent (the fixture still tests +1ms common
mapping and exact 37ms relative A/V offset). Canonical wire snapshots reject any
session replacement, and cancellation propagates before publication.

Controls: 14 individual clock/epoch/generation/session/format/track mismatches;
queue independence; missing/nonidentity mapping; use before initialization;
double initialization; stale replacement and cancelled upstream. Positive
nonempty/empty broker-composed relay retains 16 same-allocation frames, 768 exact
stereo PCM frames, PTS/DTS/duration, held-reader backpressure and terminal drain.

This is a **preflight building block**, not an authenticated setup API or complete
relay adapter. Sessions are borrowed; one controller must serialize configuration,
checks and publication. It does not make independent API calls atomic against a
concurrent reconfiguration. No runtime DLL API, ABI, wire or default changes.
Only the Windows test uses the new sample header; no Linux/Android campaign run.

Still required: compose the checked contract with frame generation validation,
GAP/FLUSH/END ordering, cancellation and safe lease drain in a reusable adapter;
then independent-process coordinated A/V qualification. No physical playback,
performance, reconnect-memory or full M6 acceptance claim. D2/D3 remain open.
Loom pre-edit briefing was attempted but unavailable (missing chromadb).

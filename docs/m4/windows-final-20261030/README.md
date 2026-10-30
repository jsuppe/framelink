# Windows integrated M4 validation — 2026-10-30

FrameLink ea6f530; renderer 7960f33 (includes metrics).
Caspar, Windows, MSVC19.44, GStreamer1.28, RTX5090. Isolated development trees
`C:/Users/jonsu/dev/framelink-m4-20261030` and `vkrender-m4-20261030`;
live interactive demo was not replaced.

Fresh standalone Release build succeeded. Full CTest **46/47 passed** (113s).
All newly added M4 transport timing, authenticated offset, recovery and six-case
source drift tests passed. The existing gst_timestamp_contract failed on sample
pull timeout. Isolated quiet repeat also failed; enabling GStreamer INFO diagnostics
passed5/5, adding per-push diagnostic output passed20/20. These repeats do **not**
resolve the timing-sensitive failure. Initial failure and repeat evidence retained.
Do not report this run as fully green or treat instrumented repeats as proof of fix.

Tests use fakesink clock scheduling and do not qualify physical audio output,
long-term hardware oscillator discipline or seamless adaptive source resampling.

Renderer7960f33: rebuild succeeded; targeted CPU4/4 passed. Native BGRA and
NV12 smoke (including new audio metrics assertions), pause/fresh-session,
selected-session switch and ten-generation tests all passed. Sampled queue depth
never exceeded6; invalid PTS0. Release build: these tests do not qualify deliberately
stalled GPU submission or Vulkan validation-layer safety. BGRA native GPU import;
NV12 existing CPU conversion fallback. Frame drops are allowed/reported, not hidden.
Raw status snapshots and producer/renderer logs under `native/`.

A second diagnostic variant storing only the last PTS and printing solely on
failure passed20/20. No production patch was applied, and diagnostic test source
was restored to the committed original afterward. This still leaves the initial
intermittent missing-sample event unexplained.
Final restored/unmodified test, rebuilt Release: isolated quiet repeat20/20 passed.
The full-suite result remains46/47; later repeat success is not a causal fix.

Follow-up: see `../windows-readiness-20261030/` for a reproduced Windows endpoint
publication race, readiness-handshake fix and post-fix validation. Earlier failure
logs here remain unchanged as historical evidence.

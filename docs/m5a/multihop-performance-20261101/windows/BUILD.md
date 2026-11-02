# Windows matched boundary provenance

Caspar RTX5090, Release MSVC17.14.23, Windows SDK10.0.26100.0.
Source44192eb. Benchmark SHA25648440B6CF0BA925DF39683FB24E881793099E6038A11BAC353BA8CC3CC6B8B88;
FrameLink DLL SHA2562F07503EEF44BBEE1662D95EA9482540ABDEAAF9A6C0E876040325C1DF6E1D8E.
All cases record both hashes.1920x1080@60, two original NT shared allocations,
3720 frames including120 warmup, independent source pacing, no per-frame controls.
CPU producer mapping includes staging/upload. GPU consumer copies original native
texture to private staging verification output, then fences reader retirement.
Full private logical image readback counted: NV123110400bytes/frame; BGRA8294400.
No shader rendering, physical display, simultaneous PCM, or zero-copy oracle claim.
Relay boundary itself neither maps nor copies pixels. Source pool identities
and exact timestamps are verified; no forced per-hop buffering delay.

DLL transport recorder and EXE completion recorder use separate prefixes to avoid
exit overwrite. Missing completion-file negative failed before accepting results.
Trace checker joins both sets on shared channel/fence/PTS and checks zero loss.

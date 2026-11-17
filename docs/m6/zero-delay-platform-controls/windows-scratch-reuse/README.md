# Windows upload scratch reuse investigation

Original NV12 4K30 run has six late arrivals (max159.06ms). Two 1800s traced
reproductions delivered all frames with no misses. This does not resolve that
intermittent failure. Its producer private commit briefly rose by about16MiB;
that is a correlation, not causal attribution.

Inspection found a fresh GLib byte array allocated and freed for every NV12
frame (12441600 payload bytes at4K). An isolated allocate/free versus warm-reuse
control, repeated A/B/A/B on Caspar, measured about1828800 page faults per600
frames versus1–2. Median allocation+copy time fell from1.38–1.40ms to0.456ms.
The control omits D3D11 and transport and did not reproduce159ms stalls.

3e0cd85 keeps one scratch buffer per sink, clears it on caps changes,
disconnect, stop and finalization; the D3D11 upload snapshots CPU bytes before
returning. GPU slot ownership and fences are unchanged. This fixes demonstrated
allocation churn, NOT yet the original intermittent timing failure. Copy/upload
remain explicitly present. GByteArray may reserve above payload size; retention
is bounded to current geometry, not historical peak.

Windows rebuilt plugin and repeated-stop/start padded NV12/RGB10A2 pixel tests
passed2/2, including mismatched-format rejection. Windows host baselineb5236a4
plus gst/test files from3e0cd85; Linux-only prior changes do not affect runtime.
Short traced comparison and broader regression/sustained gates are pending.
Loom pre-edit context unavailable (missing chromadb); repository requirements
and low-latency plan were used directly. No public policy/default changed.

Matched60s traced controls: sink median 2.8756 to 0.9818ms; map/allocate/repack median 1.9733 to 0.4490ms. Patched1800/1800frames,2880000PCM,zero34msmisses,AVmapping0ns,zero trace drops. Full Windows regression is next; original159ms stall remains unresolved.

Full Windows CTest on scratch-reuse runtime passed112/112, no failures or skips,551.28s. See windows-full.log. Next gate is trace-off1800s NV12 4K30, unchanged34ms threshold and two-buffer pool.

# Explicit Windows native sink adapter

Windows affected 5/5; Linux 3/3; Android native-source, pool and timing suites
passed. Android arm64/x86_64 builds passed. Native sink implementation is Windows
only; other platforms retain supported sources and legacy sinks.

Internal producer adapter: RGBA/NV12 original ID3D11Texture2D resource identity,
GPU-produced pixels, readiness ordering, timing metadata, reference-pinned reuse,
owner lifetime, duplicate/foreign/truncated/CPU-upload-pending rejection.
Actual appsrc -> native framelinksink -> native API consumer: RGBA pixels and
metadata exact; foreign buffer errors with no publication; retained buffer remains
valid after sink shutdown. Tests create content by synthetic GPU upload and read
it back for verification, not a zero-copy end-to-end playback demonstration.

Explicit acquire-native-buffer action only. No generic ALLOCATION/decoder support,
remote memory agreement, cross-process pipeline, throughput or physical output
claim. Application completes GPU work before abandoning unpublished allocations.

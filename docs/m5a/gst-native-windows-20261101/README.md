# M5a.7b Windows native source

Native source tests **2/2**: positive RGBA/NV12 original-storage alias, pixels,
PTS, two-slot reuse and retained GstMemory after stop; deliberate GPU-copy
negative verified after successful setup. Windows legacy regressions **7/7**.
Linux native source rerun passed after shared lifetime refactor; Android both
ABI static plugin builds passed. No Android native source claim.

The initial NT-handle object-equality assertion failed despite original texture
wrapping; independent exports are not an allocation-identity oracle. Replaced
with a synchronized test-only alias mutation. Copy negative rejects exactly
that alias criterion and cannot pass on setup failure. Initial new Windows test
build lacked the SDK-generated include directory; CMake corrected it.

Native output uses wrapped D3D11 memory, retaining channel/reader ownership.
Source does not copy frame pixels. GStreamer probes empty staging pitch on wrap;
no throughput claim is made. Remote negotiation, decoder/native sink, cross-device
GPU completion and variable frame metadata remain task7 work.
See GST_NATIVE_MEMORY.md and EXTERNAL_POOLS.md for the identity limitations.

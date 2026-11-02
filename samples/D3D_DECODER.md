# Windows GPU decoder example (M5a.8)

Build with GStreamer D3D11 and samples enabled. Run on the interactive desktop:

```powershell
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
# Terminal 1
.\fl_d3d_decode_consumer.exe example.decode
# Terminal 2 (within 15 seconds)
.\fl_d3d_decode_producer.exe .\gstframelink.dll example.decode
```

The producer generates60 synthetic H.264 frames at320x240/30fps, hardware-decodes
NV12, and GPU-converts into BGRA FrameLink allocations. The consumer uses a
2-image ring and a native swapchain. This is a finite diagnostic, not a full
player. x264enc and d3d11h264dec must be installed. No audio is generated here;
existing A/V session support is separate.

`fl_d3d_decoder_probe` reports the real decoder texture properties/exportability.
On the measured RTX5090 the stock decoder owns non-shareable arrays. The color
conversion example does NOT claim raw NV12 zero-copy. No implicit CPU fallback is
performed. GPU completion waits protect lifetime but are not optimized scheduling.
Do not use process-average startup-inclusive fps as a steady-frame-rate metric.

Current evidence: [decoder qualification](../docs/m5a/windows-decoder-20261101/README.md).
## Explicit alternative and qualification runner

Select `cpu-readable` as the producer's third argument to use hardware decoding,
explicit D3D11 download, CPU NV12→BGRA conversion and the legacy sink upload. It
is never selected automatically and is not a zero-copy path. `gpu-color` is the
default and retains hardware-decoded input on the GPU through color conversion.
Unknown modes reject. Both paths carry PTS/DTS/duration through FrameLink metadata.

For bounded two-process execution with checked exit codes and accounting:

```powershell
.\scripts\run_d3d_decode_example.ps1 -BuildDir .\build\Release -Mode gpu-color
.\scripts\run_d3d_decode_example.ps1 -BuildDir .\build\Release -Mode cpu-readable -VerifyPixels
```

`-VerifyPixels` intentionally reads back each received frame to verify seven SMPTE
color bars (lossy H.264 tolerance). These readbacks are reported separately and are
not part of the default GPU display path. GPU-completion and accepted-present
counts are not physical scanout counts. Paths are fixed320x240/30fps/60frames for
reproducible hardware qualification; this is not a general media-player frontend.
Use the existing authenticated A/V SessionProvider for production trust/negotiation;
this local diagnostic uses a unique channel per runner invocation, not an A/V
control connection or new authentication protocol.

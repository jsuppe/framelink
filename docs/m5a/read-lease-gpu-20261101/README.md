# Separate-process native GPU reader qualification

One targeted hardware test passed on each platform, no skips:

- Linux: NVIDIA RTX3090; EGL imports the FrameLink DMA-BUF, an external-texture
  shader samples it, and EGL exports a driver-created native sync-file.
- Android: TCL8483A / Mali-G52 MC2; EGL imports the FrameLink AHardwareBuffer,
  samples it, and exports a driver-created native sync-file. Both ABI builds pass;
  x86_64 runtime is not claimed.
- Windows/Caspar: D3D11 reads the shared texture into a verification staging
  resource and signals a real shared fence. Every verification pixel is checked.

Each launches a separate producer process. Producer reserves the second of two
buffers before submitting the first, then waits for that submitted buffer to be
retired/reacquired. Consumer uses a read lease and native completion handoff, closes
its input fence handle, polls retirement, and verifies the independent output.
Linux/Android verify a pixel of the constant-color output; Windows verifies all.
PTS is checked/retained by the frame/lease path. This qualifies RGBA8 GPU reads,
not all formats, decoder allocation negotiation, multi-hop throughput, signed
cross-UID APK deployment or physical scanout. The verification surface/readback is
part of the test, not a claimed zero-copy final renderer.

Important negative observation: the initial Linux attempt bound the imported
linear allocation as a GLES framebuffer attachment and failed framebuffer
completeness with GL_INVALID_OPERATION (0x502) on NVIDIA. External-texture sampling
passes. Sampling support does not imply render-target support; strict usage
negotiation must preserve that distinction. This observation is not suppressed
or labeled a driver-generic GPU-render capability.

## Abrupt producer death

Both normal and producer-death cases pass on Linux, Windows and Android (2/2
per target). Child terminates immediately after publishing its frame, bypassing
FrameLink cleanup (_exit on POSIX, TerminateProcess on Windows). The consumer
still imports/reads the allocation, verifies pixels and retires its native fence.
This proves producer lifetime does not invalidate a retained consumer allocation;
it does not cover all consumer-GPU reset/driver failure scenarios. Targeted lease
reconnect tests separately establish replacement is refused while readers remain.

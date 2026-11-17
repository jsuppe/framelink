# Explicit Linux GPU-to-CPU staging

Implementation: `gst/fl_gst_egl_readback.hpp`, used by `framelinksrc` only when
`gpu-readback=true`. The default remains false. This is not native zero-copy,
not a replacement for the shared-CPU allocation work, and not M6 closure.

## Selection and contract

- Requires Linux EGL/GLES2 + GBM at build time. Other builds reject the option;
  they do not silently select a different path.
- Requires CPU output (`native-memory=false`), an adopted channel, and an applied
  `GstFrameLinkMemoryPolicy` before start. Both authenticated endpoint policies
  must permit `FL_MEMORY_COPY|FL_MEMORY_READBACK`; local path operations must
  declare both. A property flag alone does not authorize these operations.
- Bind the policy to the connected allocation generation. A later pool-generation
  change still requires fresh agreement; the existing guard is not bypassed.
- Format scope: exact single-plane linear BGRA8/RGBA8. NV12, RGB10A2, unknown
  device identity, incompatible imports, insufficient output storage, and GPU
  readiness-bypass adoption reject. No conversion/fallback is silently attempted.
- Configure the property before adoption. Applied policy freezes it along with
  existing session properties. The A/V bin does not yet expose this option.

## Ownership and visibility

The ready source frame remains owned through the entire synchronous readback,
including failure cleanup. The adapter makes a GPU copy into a private RGBA8
surface, followed by full CPU readback. BGRA swizzling preserves output byte
order; no vertical flip is inserted. Dithering is disabled for exact bytes.

A fresh EGL image/texture binding is created for each borrowed source; none is
retained after the call. This avoids stale externally-written contents and stale
allocation/generation references. The private output surface is bounded to one
current geometry. Source objects are destroyed only after GPU completion.

Each adapter opens a matching render node and owns a separate GBM device/platform
EGL display. It never terminates the application's surfaceless display. Calls
save/restore the calling thread's current EGL context, surfaces and API; the
private context is unbound between calls, permitting serialized thread migration.
Stop/finalize release the context, surface, output texture, framebuffer, program,
GBM device and render-node FD. Instances must not be used concurrently.

The API's synchronous GL completion can still block on a hung driver. This work
makes no new bounded-device-hang recovery guarantee.

## Observability and verification

`gst.egl_readback` records the whole adapter call. Payload events separately
record `gst.gpu_copy_issued` (`COPY`) and `gst.gpu_readback_issued` (`READBACK`).
There is no extra CPU memcpy event: readback writes directly to the CPU GstBuffer.
A 32-frame trace checked 32 events and 663,552,000 bytes for **each** operation;
no `gst.cpu_copy_source` events occurred. Across all 32 mixed-size adapter calls,
the median was 3.616 ms and maximum **115.609 ms**, including first-use context
initialization. Cold startup is not hidden or discarded; this is not a latency
acceptance pass. Per-channel first/median/max are retained in the timing JSON.
Raw traces and logs are archived in `egl-adapter/`; the final exact-source trace
repeats all 32-frame/byte assertions and retains its own timing summary.

The adapter regression checks full spatial/channel/alpha patterns at 1080p and
4K in both formats, resized/recreated channels, insufficient storage, forbidden
operations at either endpoint, unsupported NV12, operation byte counts, thread
migration, and preservation of an already-current application EGL context.
The GStreamer test checks exact output format/pixels/PTS, retained samples,
permission rejection, policy immutability and an unbound opt-in rejection.
These are functional gates, not paced target-rate or A/V/endurance qualification.

Validated: Linux 9 affected checks plus final 2 adapter checks; Windows 9 affected
checks; Android 6 affected checks and both ABI builds. Production EGL staging
is Linux-only; other platforms retain their existing paths.

Remaining: real cached shared-CPU allocation comparison, coordinated-bin exposure,
and target-rate/A/V/endurance qualification. M6.8b/c remain open.

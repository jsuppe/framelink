# Vulkan interop

`include/framelink_vulkan.h` lets a Vulkan application be a FrameLink producer
or consumer on Windows, Linux and Android with one call sequence. It is
header-only: inline functions over the platform interop headers
(`framelink_d3d11.h`, `framelink_dmabuf.h`, `framelink_ahb.h`,
`framelink_sync_fd.h`). The library exports nothing new, never links Vulkan,
and calls Vulkan only through the `vkGetInstanceProcAddr` the caller passes, so
the loader, volk or any other dispatch works.

## What it covers

| | Windows | Linux | Android |
|---|---|---|---|
| Image import | D3D11 shared NT handle (`D3D11_TEXTURE_BIT`), dedicated | DMA-BUF with explicit DRM modifier layout; disjoint multi-object; per-plane NV12 fallback | AHardwareBuffer, dedicated; external formats for native YUV |
| Consumer wait | shared ready fence as timeline semaphore | frame SYNC_FD imported (temporary) into the caller's binary semaphore | same as Linux |
| Consumer completion | shared consumed fence (timeline) signalled by your submit | exportable binary semaphore -> SYNC_FD -> `flReleaseSyncFd` | same as Linux |
| Producer readiness | shared ready fence at `flBeginSubmit`'s value, then `flEndSubmit` | exportable binary semaphore -> SYNC_FD -> `flSubmitSyncFd` | same as Linux |
| Adapter check | `VkPhysicalDeviceIDProperties::deviceLUID` | `VK_EXT_physical_device_drm` render node | one GPU: always OK |
| Queue family across the ring | `VK_QUEUE_FAMILY_EXTERNAL` | `VK_QUEUE_FAMILY_FOREIGN_EXT` | `VK_QUEUE_FAMILY_FOREIGN_EXT` |

Images cross the ring in `VK_IMAGE_LAYOUT_GENERAL`; `flVkAcquireBarriers` and
`flVkReleaseBarriers` build the ownership transfers.

Producers need no channel flag: `flAcquireBuffer` already waits for the
consumer's completion fence. Consumers that wait or signal on the GPU need
`FL_GPU_SYNC` (GPU readiness wait) or `FL_DEFER_RELEASE` (CPU readiness wait,
GPU retirement); `flVkFrameConsumed` refuses a plain consumer rather than
order nothing.

## `FL_VULKAN_IMPORT`

The consumer allocates the ring, so the consumer decides whether Vulkan can
import it. On Linux with the NVIDIA driver, a GPU ring allocated with
`GBM_BO_USE_RENDERING` gets a compressed block-linear layout (modifier
`0x300000000e08013`) that NVIDIA's Vulkan driver does not list; EGL imports it.
`FL_VULKAN_IMPORT` adds `GBM_BO_USE_SCANOUT`, which selects the uncompressed
layout (`0x300000000606013`) Vulkan does list, and falls back to the default
allocation if a driver refuses. It changes nothing on Windows or Android
(their storage always imports), nothing for `FL_MAP_CPU` rings (linear), and
nothing for adopted external storage. Default allocation is unchanged.

Without the flag, the header still works wherever the default layout is
importable; where it is not, `flVkImportImage` returns `FL_FORMAT_UNSUPPORTED`.

## Consumer sketch

```c
flVkContext vk;  flVkInitContext(&vk, instance, phys, device, vkGetInstanceProcAddr, NULL);
flChannelInfo info;  flQuery(ch, &info);
if (flVkCheckAdapter(&vk, &info) != FL_OK) { /* wrong GPU */ }
flVkSync sync;  flVkCreateSync(&vk, ch, &sync);         // ch: FL_GPU_SYNC | FL_VULKAN_IMPORT

flFrame f;  flAcquireFrame(ch, &f, 100);
flVkImage *img = cache_lookup(f.slot);                   // per slot, per generation
if (!img || !flVkImageCurrent(ch, img)) flVkImportImage(&vk, ch, f.image, &desc, img);
flVkSemaphoreOp wait, signal;
flVkFrameReady(&vk, &sync, &f, perFrameBinarySemaphore, &wait);
flVkFrameConsumed(&vk, &sync, &f, &signal);
// record: flVkAcquireBarriers ... sample img->view[0] ... flVkReleaseBarriers
// vkQueueSubmit waiting on `wait`, signalling `signal` (timeline values on Windows)
flVkFrameRelease(&vk, &sync, &f);
```

Producer: `flAcquireBuffer` -> import -> record -> `flVkBeginSubmit` (signal op)
-> `vkQueueSubmit` -> `flVkEndSubmit`.

## Tests

`test/fl_vulkan.cpp`, one binary for all three platforms. The loader is opened
at run time; no loader or no device with the extensions exits 77 (skip). The
producer and consumer use **separate `VkDevice`s**: in real use they are
different processes, and one shared queue would serialize their work and hide
a missing wait (the first version of the ordering test did exactly that and
its negative control passed).

| CTest | What it proves |
|---|---|
| `vulkan_formats` | BGRA8, RGBA8, RGB10A2, NV12 x {Vulkan->CPU map, Vulkan->Vulkan `FL_GPU_SYNC`, Vulkan->Vulkan `FL_DEFER_RELEASE`, CPU->Vulkan}; every pixel compared byte for byte on the other side. RGBA8 must pass everywhere. |
| `vulkan_ordering` | Host-gated GPU work: the consumer's GPU read does not start until the producer's GPU write completes, and the producer cannot reacquire the slot until the consumer's GPU read completes. |
| `vulkan_adapter` | The ring's GPU is accepted; an altered identity is rejected (Windows, Linux). |
| `vulkan_generation` | `flRequestGeometry` invalidates imports (`flVkImageCurrent`); re-imported images write the new ring. |
| `vulkan_header_contract` | GPU-free, C99: argument checks, loader without entry points, barrier contents, one-signal-in-flight rules. Runs in hosted CI. |

Negative controls (run by hand; each must fail):

| `FL_VK_NEGATIVE=` | Breaks | Fails at |
|---|---|---|
| `skip-ready-wait` | consumer drops the readiness wait | consumer read finished while producer write was still gated |
| `early-consumed-signal` | consumer signals completion before its reads | producer reacquired the slot during the consumer's read |
| `no-import-flag` | ring created without `FL_VULKAN_IMPORT` | Linux/NVIDIA: tiled RGB imports refused, RGBA8 requirement fails |

Headers come from a pinned Khronos release (`vulkan-sdk-1.3.296.0`, SHA-256
checked) on every platform; `FRAMELINK_BUILD_VULKAN_TESTS=OFF` skips the
download and the tests.

## Results (2026-11-20)

| | Windows (RTX 5090) | Linux (RTX 3090) | Android (Mali-G52 MC2) |
|---|---|---|---|
| `vulkan_formats` | 12/16; NV12 refused (below) | 16/16 | 12/16; NV12 import-only (below) |
| `vulkan_ordering`, `_adapter`, `_generation`, `_header_contract` | pass | pass | pass |
| Vulkan tests repeated in parallel (`-j4`/`-j8`) | 10 rounds | 30 rounds | - |
| Negatives `skip-ready-wait`, `early-consumed-signal` | both fail as intended | both fail as intended | both fail as intended |
| Negative `no-import-flag` | passes (storage always importable) | fails: 6 tiled RGB rows refused | passes (always importable) |
| Full suite | 186/186; old 060c54e4 suite on the new DLLs 180/180; exports unchanged (88/47/2) | 177/177 (1 known skip) | 100/102 (known skip, known `gst_av_bench_smoke`) |

Bugs the hardware found and this change fixes:

- Windows: a ring reallocation replaces the shared fences, so an imported
  timeline went stale and frames after `flRequestGeometry` never became ready.
  The sync calls now follow the ring generation and park old semaphores until
  `flVkSyncCollect`.
- Windows: NV12 D3D11 textures import "successfully" on NVIDIA but the chroma
  plane reads back as zeros. The header now refuses NV12 on Windows.
- Linux: per-plane NV12 aliased luma and chroma (see Known limits).
- Linux/NVIDIA: default GPU rings are not importable (`FL_VULKAN_IMPORT`).

## Known limits

- **Long-lived imports of linear rings on NVIDIA Linux.** In the three-process
  relay rate harness, a Vulkan import of a linear (`FL_MAP_CPU`) DMA-BUF read
  zeros after about 30–38 s at BGRA 1080p60. The storage itself was correct,
  and a fresh import read it. A single-process reproduction did not show it, and
  the trigger is not established. Until it is, re-import such rings rather than
  caching imports for a whole generation
  ([evidence](m6/linux-native-relay-rate-b5c942a/README.md)).

- Windows NV12 is refused (`FL_FORMAT_UNSUPPORTED`): NVIDIA's D3D11 NV12
  import silently loses chroma. Use an RGB ring for Vulkan, or D3D11 for NV12.
- Android native YUV (NV12) is an external format: Vulkan can only sample it
  through a YCbCr conversion, so a Vulkan *producer* cannot write it and the
  test proves the consumer import, view and conversion but not its pixels
  (that needs a sampling shader; not in this test).
- The NVIDIA Linux driver did not apply `pPlaneLayouts[0].offset` for the
  per-plane NV12 images (chroma landed on luma). The header binds each plane
  at its offset instead; the spec defines the two as equivalent.
- Vulkan 1.1 is required by the header; the test uses 1.2 (timeline
  semaphores are required on Windows).
- No allocation helper: Vulkan-allocated storage can be adopted with the
  existing `flCreateChannelFrom*` entry points, but this header does not
  allocate.

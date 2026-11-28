# GPU device loss: FL_DEVICE_LOST, injection and real resets (decision 13) — 2026-11-27

This record is dated; current status is in [STATUS.md](../../STATUS.md). Jon chose A3, B2 and C2 in [decision 13](../decisions-20261126.md#decision-13-gpu-hang-and-device-loss-testing-2026-11-27). It covers the review, the test-only injection, the fix and its verification, and then real resets on all three platforms, run after Jon said the hosts were free (same evening).

## Review: what a GPU reset reached before this change

| Platform | Path | Gap |
|---|---|---|
| Linux, Android | `flAcquireFrame` waits for the producer's ready fence with `poll()` | A fence that completes **with an error** (what a reset leaves on abandoned work) is readable too. Only `SYNC_IOC_FILE_INFO` shows the error; FrameLink never read it. A frame whose GPU write failed was delivered as valid. |
| Linux, Android | Read-lease completion polls the consumer's fence | An errored fence retired the lease as a successful read, and nobody was told. |
| Windows | `flAcquireFrame` compared `GetCompletedValue() >= ready` | A removed device reads every fence as `UINT64_MAX`, which passed as readiness. |
| Windows | Attach, producer acquire, lease collection, pool snapshot | Device removal was detected (`GetDeviceRemovedReason`, `UINT64_MAX`) but returned `FL_DISCONNECTED`, and was untested. |
| Vulkan helpers | `framelink_vulkan.h` | No change needed. They call no Vulkan function that reports device loss (queue submit, fence and semaphore waits are the application's own calls, which return `VK_ERROR_DEVICE_LOST` themselves); `flVkFrameReady` passes `FL_DEVICE_LOST` through from `flFrameReadySyncFd`. |

## The change (C2)

- **`FL_DEVICE_LOST = 12`** in `flResult`: terminal for the handle like `FL_DISCONNECTED` (the handle stays valid and must be closed); no frame is delivered after it; recreate GPU objects before opening a new channel.
- **Linux/Android:** every fence check goes through `fl::fenceState` (pending, signalled, or signalled with an error, from `SYNC_IOC_FILE_INFO`; `poll()` for fds that are not sync files). An errored ready fence makes `flAcquireFrame` (and `flFrameReadySyncFd` for `FL_GPU_SYNC`) return `FL_DEVICE_LOST`, sticky. An errored lease fence still retires the lease, so the producer gets its storage back, and collection then returns `FL_DEVICE_LOST`.
- **Windows:** every fence read and device-removed check goes through two wrappers. `FL_DEVICE_LOST` is returned when **this process's own device** reports removed. A shared fence also reads `UINT64_MAX` when the peer process dies (measured below), so `UINT64_MAX` with a live own device stays `FL_DISCONNECTED`, and the delivery rule for a live device is unchanged. A pool poisoned for an unsafe retirement on a live device stays `FL_DISCONNECTED`.
- **`framelinksrc`:** one stable error, "FrameLink GPU device lost" (`GST_RESOURCE_ERROR_FAILED`; details `framelink-device-lost` and `framelink-local-failure`, so it is posted at once and never relabelled as a peer failure).
- **Docs:** `framelink.h`, [READ_LEASES.md](../../READ_LEASES.md), the [Windows app contract](../WINDOWS_APP_CONTRACT.md).

Windows product change: requalification on the frozen API returns to Jon with decision 14.

## Test-only injection (B2)

`FRAMELINK_TEST_FAULTS`, read once per process, inert when unset:
- `ready-error=V` (POSIX): the ready fence of the frame with ready value V reports an error once it has completed.
- `lease-error=N` (POSIX): the Nth read-lease completion fence in the process reports an error.
- `device-removed-after=N` (Windows): once N frames have been acquired in the process, every fence reads `UINT64_MAX` and every device reports removed.

Tests (CTest sets the variable): `device_lost_ready_fence`, `device_lost_lease_fence` (Linux, Android), `gst_device_lost` (Linux), `device_lost_windows`. They need a GPU device (GBM or D3D11), so they are not in the hosted `ci` preset.

## Results

Commits: `14fca2ec` injection and tests only (behaviour unchanged); `b3cd877a` the fix; `cfcd1283` and `12ef0bd8` two Windows corrections found by the full Windows suite (below).

| Test | Before the fix (`14fca2ec`) | After |
|---|---|---|
| `device_lost_ready_fence`, Linux | **fails**: frame 2 delivered as `ok` | pass |
| `device_lost_lease_fence`, Linux | **fails**: lease collected as `ok` | pass |
| `gst_device_lost`, Linux | **fails**: no device-lost error | pass: "FrameLink GPU device lost", frame 3 not delivered |
| `device_lost_windows` | **fails**: producer gets "peer disconnected" | pass, 4 of 4 runs |
| the two POSIX tests, Android tablet | not run (same code as Linux) | pass |

Full suites at the fix: Linux 204/204 (one test skipped as always); Android 152/152 (1 skip); Windows 198/198 at `12ef0bd8`.

**Peer death is not device loss.** Killing the producer or consumer process at 1080p60 native (the M6.4 at-rate cells): Linux NV12 6/6 and Android RGBA 4/4 report "FrameLink peer closed or failed" within the declared bound, not device loss.

### Two Windows mistakes, found by the full suite and corrected

1. **`b3cd877a` read the ready fence and device on every `flAcquireFrame`**, before a ring was attached: a null dereference (`gst_native_source_win` crashed). `cfcd1283` guards both.
2. **`b3cd877a` treated every `UINT64_MAX` fence as device loss.** On Windows a shared fence reads `UINT64_MAX` when the peer process dies, too. Three death tests (`read_lease_d3d11_producer_death`, `gst_relay_downstream_death_win`, `gst_av_bench_fault_producer_kill`) passed 3/3 at `14fca2ec` and failed 3/3 at `cfcd1283`; the full suite there had exactly those 3 failures. `12ef0bd8` limits device loss to our own removed device; all five targeted tests then passed 3/3 and the suite 198/198. One of the three had first been put down to the known flake of decision 3; the before/after runs show it was this regression.


## Real resets (Jon said the hosts were free)

Run on the Windows test host (RTX 5090, driver defaults for timeout detection), this Linux workstation (RTX 3090, NVIDIA 580.178.04, the desktop running on the same GPU) and the Android tablet (Mali-G52 MC2). Every case ran 3 times; every case then ran a fresh normal lease test on the same device (recovery), which passed every time. The desktop on Linux and the host on Windows survived every reset.

**How a hang is made.** A shader whose loop bound the compiler cannot see: GLSL ES 3.00 with the count in a uniform, an integer LCG body; HLSL compute with the count in a constant buffer. Calibrated first: run time grows linearly with the count on Mali and NVIDIA. Two first attempts were not real hangs and are kept as lessons: with a constant bound, or a `sin`/`fract` body, the Mali compiler ran 2·10⁹ iterations in 20 ms.

| Case | Windows | Linux (NVIDIA) | Android (Mali) |
|---|---|---|---|
| **Consumer's read hangs** (context reset by the watchdog) | `FL_DEVICE_LOST` after 5.3–6.3 s (`DXGI_ERROR_DEVICE_HUNG`); terminal; the producer, whose device survived, gets `FL_DISCONNECTED` | `FL_DEVICE_LOST` after 5.1–5.4 s: the fence completes with **−110 (`-ETIMEDOUT`)**; slot handed back; terminal. The GL context reports **no** reset | **Not visible to FrameLink.** The fence completes cleanly (status 1) after ~5.1 s; FrameLink retires the lease as a normal read; slot handed back. Only GL reports the reset (`GL_GUILTY_CONTEXT_RESET`) |
| **Producer's frame write hangs** | The producer's next `flSubmit` returns `FL_DEVICE_LOST`, so the frame behind the hang is never published; the consumer gets `FL_DISCONNECTED` | The consumer gets `FL_DEVICE_LOST` after 5.3–5.5 s (fence −110); **the frame is not delivered**. The producer's GL context reports no reset | **An unwritten frame is delivered as valid**: the fence completes cleanly after ~5.5 s and the frame's pixels are all zero. The producer's GL context reports a guilty reset; the producer process then dies from a signal |
| **Whole-device reset** | `DXCap -forcetdr` (run as an interactive highest-privilege task; it refuses from ssh): both sides `FL_DEVICE_LOST` (`DXGI_ERROR_DEVICE_RESET`) about 1.3 s after the reset | Not possible on this host: `nvidia-smi -r` refuses ("the primary GPU") | No interface without root |
| **Physical loss** (adapter disabled while streaming, then re-enabled) | Both sides `FL_DEVICE_LOST` (`DXGI_ERROR_DEVICE_REMOVED`) within 0.40–0.43 s, after the fix below | — | — |

**A fix the physical-loss test found (`d05b106c`).** Before it, the consumer reported `FL_DEVICE_LOST` within 0.5 s, but the producer, whose device reported removed, got `FL_TIMEOUT` from `flAcquireBuffer` for 34 s: the fences still read normally and the consumer had stopped releasing. `flAcquireBuffer` and `flBeginSubmit` now check the producer's own device. The Windows suite at `d05b106c`: 198/198. The same check is what refused the producer's submit after its own hang above.

### What this means

- **Windows and Linux:** a real reset reaches FrameLink and is reported as `FL_DEVICE_LOST` (or, on the side whose device survived, `FL_DISCONNECTED`); no frame written behind a hang is delivered. On Linux the fence is the *only* place the reset showed: NVIDIA's GL robustness query stayed silent.
- **Android (Mali):** a reset never reaches the fence. FrameLink retires an abandoned read as a good one and delivers an unwritten frame as valid. Only the application's own GL context learns of the reset. Jon chose **C**: documented now (Android applications detect resets through GL/Vulkan robustness; `framelink.h`, READ_LEASES.md, GST_AV_REFERENCE.md, PLATFORM_PARITY.md); an API through which an application reports a reset to FrameLink (`flReportDeviceLost`) is added only when a consumer needs it. It could not stop a frame already submitted.

### Not established

- Whether the Mali producer's death from a signal after its reset comes from the driver or the test's teardown.
- Real-reset cases run as manual tools, not CTest: they disturb the host. Their sources are `fl_read_lease_egl --gpu-hang-consumer|--gpu-hang-producer` (`FL_TEST_HANG_PROBE=1` calibrates), `fl_read_lease_d3d11 --gpu-hang-consumer`, and `fl_device_loss_stream_win`, driven by `d13-tdr.ps1`, `d13-producer-tdr.ps1`, `d13-forcetdr.ps1` and `d13-adapter.ps1`.

## Not established (steps 1–3)

- Whether a Windows shared fence also reads `UINT64_MAX` after a *normal* producer close. The live-device delivery path is left exactly as it was, because decision 12's end-of-stream drain depends on frames submitted just before a close.

## Files

- `d13-win.ps1`, `d13-win2.ps1`, `d13-win3.ps1`: the Windows builds and runs (before and after; regression separation; the corrected build).
- `logs/`: Linux before and after, the three Windows runs (scrubbed), suite summaries, and the native kill results; `logs/real/`: every real-reset run on the three platforms (Windows logs scrubbed).
- `d13-tdr.ps1`, `d13-producer-tdr.ps1`, `d13-forcetdr.ps1`, `d13-adapter.ps1`, `d13-suite.ps1`: the real-reset and suite runs on Windows.

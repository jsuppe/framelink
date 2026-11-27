# Linux native relay: the BGRA stalls and the stale Vulkan import (decision 11) — 2026-11-27

This record is dated; current status is in [STATUS.md](../../STATUS.md). Jon chose A3 (find the stall's cause now) and B3 (reproduce the stale import first) in [decision 11](../decisions-20261126.md). Host and cells as in the [`b5c942a` record](../linux-native-relay-rate-b5c942a/README.md): Linux, NVIDIA GeForce RTX 3090, driver 580.178.04, GStreamer 1.28.2, the paced three-process relay (source → relay → receiver), pool 2, one unread frame. Another process held 4 GB on the same GPU, mostly idle, throughout. Whether it was there for the original runs is not known.

## A3: the stall is the test receiver, not FrameLink

**Reproduced.** At `b5c942a` (rebuilt from `git archive`; binaries not byte-identical to the recorded ones because the build path is embedded), BGRA 1080p60 failed after 187 s of its 1800 s run: "frame replaced: expected frame 11201 … 30.428 ms after the previous acquisition", the same failure as recorded. Two further repeats were stopped by hand to instrument the receiver instead. That failure stands.

**Where the time went** (per-frame CSVs of the failing run; `stall/attribute.py`, `stall/busy.py`):

| Stage | p99 | Max |
|---|---|---|
| Source upload | 3.20 ms | 6.81 ms |
| Source to relay | 2.15 ms | 2.25 ms |
| Relay forward | 0.04 ms | 0.08 ms |
| Relay forward to receiver pickup | 2.18 ms | **16.49 ms** |
| Receiver completion (copy and lease) | 5.11 ms | 7.15 ms |

The source and relay were on time throughout. 57 frames were picked up more than 5 ms after the relay had delivered them: the receiver was still busy with the previous frame. In the passing `0e8da988` 1800 s runs, the same pickup delay never exceeded 2.38 ms (BGRA 1080p60) or 2.27 ms (BGRA 4K30). The CSVs of the failing run end about 35 frames before the failure (that harness loses rows not yet written when it aborts; fixed since `efa86dec`), so the failing frame itself is not recorded.

**Step timers** (`stall/b5c942a-receiver-timers.patch`, test-only): the receiver logged each frame whose own work exceeded 12 ms. That run passed 1800 s. Of 108,000 frames, 12,769 exceeded 12 ms and 942 exceeded the 16.67 ms frame period:

| Receiver step, frames over 12 ms | p50 | Max |
|---|---|---|
| Payload check (byte by byte, 8.3 MB) | 9.07 ms | 15.90 ms |
| Lease completion and collection | 3.30 ms | 5.47 ms |
| Copy submit (fresh import) | 1.80 ms | 6.35 ms |
| CSV row | 0.01 ms | 0.69 ms |
| Total | 14.17 ms | 21.90 ms |

**Cause.** At `b5c942a` the test receiver needed close to a whole frame period per BGRA 1080p60 frame, mostly its byte-by-byte payload check. Pool 2 absorbs one slow frame; two slow frames in a row replace one. That is the recorded stall. It explains why only BGRA failed (2.7 times NV12's bytes), and why both BGRA cells passed at `0e8da988`, whose vectorized check takes 5.8 ms per frame at p50 (verification including the copy). It is a harness limit, not a FrameLink, relay or driver stall.

## B3: a long-lived import goes stale at the second slot's first reuse

A test-only switch (`FL_BENCH_KEEP_IMPORT=1`) restores what `framelink_vulkan.h` advises, one import per slot and channel generation. With `FL_BENCH_DIAG=1` the receiver checks the copy while it still holds the lease. On a mismatch it then reads the same storage on the CPU, copies through the same import again, and copies through a fresh import.

| Cell | Runs | Stale | What the stale import read |
|---|---|---|---|
| BGRA 1080p60 | 10 (5 without diagnostics, then 5 with; 3 phases each in 4 of those) | every run, in every phase | in the 5 diagnosed runs: the whole frame zeros, at sequence 3, slot 1 |
| NV12 4K30 | 4 | every run failed its payload check (stamp correct) | in the one run with payload diagnostics: the chroma plane zeros past its first 64 KiB, at frame 1, slot 1 |
| NV12 1080p60 | 3 | none | |
| BGRA 4K30 | 3 | none | |

In every diagnosed case (5 BGRA runs, 14 phases, and the NV12 4K30 run):
- **The storage was correct.** The CPU read and the fresh import held the right frame.
- **The import was not obsolete.** It was the same DMA-BUF (same device and inode as at import), and `flVkImageCurrent` reported it current.
- **The stale import stayed stale.** A second copy through it read the same zeros.
- **A re-import healed it.** In the 90 s and 10 s runs, no further stale copy occurred after the re-import.

The trigger is the second slot's import, made in the first frames, at its first reuse. At `b945c5f`, with no pre-roll, a long-lived import went stale only after 30–38 s; the pre-roll now cycles every slot at stream start. The underlying cause (driver, allocation flags, or FrameLink's GBM ring) is not established. The fresh-import control passed NV12 4K30. The harness's default (a fresh import per copy) is unchanged and passes every cell.

**Consequence.** An application that follows the header exactly would show black or partly black frames on this path: CPU-written, linear (`FL_MAP_CPU`) rings imported once, on NVIDIA Linux. The `fl_vulkan` tests, whose rings are written on the GPU and which keep one import per slot, pass in the suite.

## For Jon

- **The BGRA stall:** the cause is established as the test receiver's check. The `b5c942a` failures stand as recorded. The `0e8da988` passes are on a receiver with margin.
- **The header guidance (B1 or B2):** Jon chose B1 on the same day; the header and `VULKAN.md` now carry the limit. The options were: B1 changes the comment only (re-import linear `FL_MAP_CPU` rings for each frame on NVIDIA Linux; cost not measurable here). B2 makes `flVkImageCurrent` report such imports as never current, a public-header behavior change that affects Windows.
- **Harness gap at `b5c942a` only:** that harness loses its last CSV rows when a run aborts. The current harness already flushes pending rows before an abort (`efa86dec`).

## Not established here

- Why the import goes stale, and whether other GPUs or drivers do the same.
- Kernel messages during the runs: not readable by this user.
- Whether the old receiver's check-time variance (8–16 ms) is CPU frequency or scheduling.

## Files

- `stall/`:
  - the failing run's analysis and receiver log;
  - the timer run's analysis and every slow-frame line (`.log.xz`);
  - the timer patch;
  - the attribution scripts;
  - the rebuild hashes.
- `stale-import/`:
  - every reproduction's runner log and receiver log;
  - harness hashes for the first build (`binaries-keep-1-5.sha256`) and the final diagnostic build (`binaries-final.sha256`).
  - The `keep-diag-2` to `-5` builds in between are not hashed.
- `raw-sha256.json`: hashes of the raw CSVs and logs, which are kept outside the repository.

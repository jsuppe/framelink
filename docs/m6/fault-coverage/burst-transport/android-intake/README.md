# Diagnostic: what sets FrameLink's burst intake on Android 1080p RGBA — 2026-11-26

Follow-up to the [queued-burst record](../README.md), where FrameLink took a burst in at 7.6–8.6 ms per 1080p RGBA frame on Android, about 2x real time. **Diagnostic only:** these runs qualify nothing, and no product code changed. Host: TCL 8483A (Mali-G52), Android 15; Linux comparisons on the RTX 3090 host. Profile as in the qualification runs (pool 2, unread queue 1, output queue 1, latency 0, transport-only, `--burst-queue`, 200 ms burst on frame 360) unless stated.

## 1. Where the producer's 8 ms goes (traced, 3 runs)

`--trace` records FrameLink's existing GStreamer and core trace events. The per-frame split (`split_producer.py`) uses the order of `gst.frame_receive`, `gst.frame_acquired`, `gst.cpu_copy_sink` and `gst.sinkRender`, and the core call durations. Values are medians (max) across frames, ranges across the 3 runs:

| Per frame | Steady (frames 121–340) | Burst drain (frames 361–382) |
|---|---|---|
| Waiting for a free slot (`flAcquireBuffer`) | 0.13 ms | **2.30–2.44 ms** (max 4.86) |
| CPU copy into the slot (acquire → copy end) | 4.88–4.95 ms | 4.06–4.38 ms |
| `flImageMap` + `flSubmit` | about 0.4 ms | about 0.4 ms |
| Whole `sinkRender` | 5.37–5.47 ms | 6.73–7.31 ms |

Tracing adds some overhead; the traced drain rates (7.35–8.70 ms) are in the untraced range, and 1 of the 3 traced runs exceeded the 8 ms drain check, as in qualification.

**Margin:** the longest slot wait during a drain (4.86 ms) is close to `buffer-wait-ms=5`, after which the sink drops and counts the frame. A longer or denser burst on this cell could turn into counted drops.

## 2. Why the producer waits: the consumer holds each slot one frame period

From the consumer's core trace (`split_consumer.py`, by frame order; both processes use the same monotonic clock):
- In steady state the consumer holds a slot (`ipc.frame_received` to `reader.retire`) for 18.3 ms, of which 16.7 ms is between `reader.acquire` and `reader.complete`: one frame period.
- During the drain the hold falls to 8.7–9.9 ms and tracks the interval between frames (7.1–8.4 ms).

So a slot is released only when the next frame has arrived. With pool 2, one slot is always taken by the previous frame, and each burst frame waits for the next to make the whole trip (producer copy, transfer, consumer delivery). The drain rate is the length of that loop, of which the producer's copy is about 4.2 ms.

**What holds it:** a native frame's lease completes when its last buffer reference is dropped (`NativeSourceReader`). A weak reference on each video buffer, set in the harness's `appsink` callback, shows when it is freed:

| | Buffer freed after the callback returns (median, p99) |
|---|---|
| Android, default | 16.05 ms, 20.0 ms |
| Android, `enable-last-sample=false` | 15.97 ms, 20.0 ms |
| Linux native, default | 17.00 ms, 17.2 ms |
| Linux native, `enable-last-sample=false` | 16.96 ms, 17.2 ms |

The harness drops its own sample in the callback and keeps no other reference, so **the buffer, and with it the FrameLink slot, is held inside `appsink` after the callback until the next buffer arrives.** The basesink last-sample reference is not the cause: disabling it changed neither the lifetime nor the traced drain (3 runs, 7.67–7.92 ms). Which `appsink` field holds it was not identified. Reference counts read inside the callback (`refcount/`) came out 3 vs 2 on Android and 2 vs 3 on Linux for the same two settings. They depend on when references are swapped and are not evidence either way. An earlier reading of them as pointing at `appsink`'s internal sample was withdrawn.

The same holding applies on Linux, where the loop is shorter (3.5–3.8 ms drain).

## 3. A bigger pool does not fix it under the coordinated policy

| Configuration (200 ms burst) | Runs | Result |
|---|---|---|
| pool 3, unread queue 1 (traced) | 3 | **all failed**: the consumer's fatal "coordinated video frame replaced before it was read: the producer delivered faster than real time", after 364–373 frames |
| pool 3, unread queue 2 | 3 | refused at setup: "negotiated video queue exceeds ring capacity" |
| pool 4, unread queue 2 | 3 | 2 failed with the same replacement error; 1 passed, draining at 3.37 ms per frame |

With more free slots than the unread queue can take, the producer outruns the consumer during the burst and overwrites an unread frame, which is fatal by design. Pool 2 with an unread queue of 1 survives bursts only because the held slot throttles the producer to the consumer's pace.

## Findings

1. The Android burst intake (about 8 ms per frame) is the producer–consumer loop with one effective free slot: about 4.2 ms producer copy plus about 2.4 ms waiting for the consumer to release the previous frame, plus submit.
2. **The slot is held by the consumer application's `appsink` until the next frame**, on both platforms. A native receiver whose application uses `appsink` therefore always has one slot fewer than the pool. The reference already tells applications to size `video-pool` to "cover unread depth and downstream-held buffers"; this shows that `appsink` is such a holder, and the bench's primary profile (pool 2, unread queue 1, allowed as "at least 1" under the pool − 2 limit) leaves no slot beyond it.
3. **Under the coordinated policy a burst is either throttled (pool 2) or fatal (larger pools).** The producer is bounded by free slots, not by the consumer's unread queue. Faster draining with a larger pool is therefore not available without changing that, which belongs with the deferred overload decision (D2).
4. The slot wait under a 200 ms burst on Android comes within 0.14 ms of `buffer-wait-ms=5`.

Decisions for Jon, not taken here: whether ~2x burst intake on the Android cell is acceptable (budget); whether the coordinated producer should be credit-bounded by the consumer's unread queue (D2); and whether the guidance should name `appsink` as holding the last frame.

## Not established

- Which `appsink` field holds the buffer, and whether other sinks (e.g. `glimagesink`, `waylandsink`) hold the last frame the same way.
- The Android copy cost alone (about 4.2–4.9 ms for 8.3 MB, about 1.7–2 GB/s) against the platform's memcpy bandwidth.
- Bursts at other cells, sizes or `buffer-wait-ms` values.

## Binaries and files

All from sources at the commit carrying this record, apart from earlier builds, as noted:
- Traced pool-2 and pool-3 runs: the qualification bench at `063d6234` (Android `918d15cf4d8f2845`).
- `--no-last-sample` traced runs: an intermediate build with that option only (Android `036b70b36c7b4266`).
- Queue-depth and refcount runs: an intermediate build that added the refcount histogram; its hashes were not kept.
- Buffer-life runs: this commit's source (Android `159d3603f589db11`, Linux `13d881507c8afd27`).

Files:
- `summaries/`: per-run summaries, by experiment;
- `split_producer.py` and `split_consumer.py`: the per-frame splits, taking trace CSV pairs as arguments;
- `raw-sha256.json`: traces and raw CSVs, retained outside the repository.

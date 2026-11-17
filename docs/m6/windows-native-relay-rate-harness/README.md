# Authenticated native relay rate harness

Test-only executable `fl_gst_local_native_bench_win` composes source/relay/final-reader processes through existing FAR4→snapshot/FAR5 APIs, native ownership and authenticated PCM. It adds no public API or production behavior.

Local analyzer: **30 cases pass**, including valid trace/resource controls and missing identities, bad mapping/completion, trace loss/copy, invalid resource counters and growth failures. Fresh local CMake configuration and its focused analyzer CTest **1/1 pass**. GStreamer was unavailable in that local configure; **this is not a Windows compile or runtime result**. Independent static review findings were corrected and verified.

## Invocation and records

```
fl_gst_local_native_bench_win <gstframelink.dll> --out <new-directory> --seconds 2 --width 64 --height 32 --fps 30 --format BGRA
python tools/check_local_native_bench.py <new-directory>
```

Supports BGRA/NV12,30/60fps, up to1800seconds. `--trace` is limited to5seconds and sizes the core recorder explicitly; actual no-loss capacity still requires runtime validation. Per-role video/PCM/resource CSVs and terminal summaries remain in the output directory. Final reader verifies independent payload markers/pixels, metadata and real native completion; a held-last-frame control is distinct from completion. Source uploads and final private verification copies are explicit, not end-to-end zero-copy claims. Producer mapping uses the documented optional identity mapping; intermediate/final receiver mappings are required and recorded.

The [frozen plan](../WINDOWS_NATIVE_RELAY_RATE_PLAN.md) defines qualification order and scope. Corrected11d1b65 clean Windows build and2s functional smoke passed; [raw records and pinned analysis](../windows-native-relay-rate-smoke-11d1b65/README.md). Matched short BGRA1080p60 preflights also passed; current target qualification state is in the [ledger](../LOW_LATENCY_PLAN.md). No endurance, physical playback, decoder or M6-completion claim is made here.

Source acquisition, upload and submit calls and relay forward calls have explicit start/end stamps; software call duration is not GPU execution time. The broader acquire-to-record span is reported separately.

## First Windows build, preserved failure

Exact4f46f42 configured successfully but MSVC rejected the runtime-selected `flFormat` aggregate field as narrowing to uint32_t (C2397). No media ran. The harness now casts that existing positive format enum explicitly. See initial-build-failure/; the lane was verified idle after failure. Production code is unchanged.

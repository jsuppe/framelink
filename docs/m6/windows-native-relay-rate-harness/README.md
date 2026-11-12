# Authenticated native relay rate harness — not yet Windows-validated

Test-only executable `fl_gst_local_native_bench_win` composes source/relay/final-reader processes through existing FAR4→snapshot/FAR5 APIs, native ownership and authenticated PCM. It adds no public API or production behavior.

Local analyzer: **25 cases pass**, including valid trace/resource controls and missing identities, bad mapping/completion, trace loss/copy, invalid resource counters and growth failures. Fresh local CMake configuration and its focused analyzer CTest **1/1 pass**. GStreamer was unavailable in that local configure; **this is not a Windows compile or runtime result**. Independent static review findings were corrected and verified.

## Invocation and records

```
fl_gst_local_native_bench_win <gstframelink.dll> --out <new-directory> --seconds 2 --width 64 --height 32 --fps 30 --format BGRA
python tools/check_local_native_bench.py <new-directory>
```

Supports BGRA/NV12,30/60fps, up to1800seconds. `--trace` is limited to5seconds and sizes the core recorder explicitly; actual no-loss capacity still requires runtime validation. Per-role video/PCM/resource CSVs and terminal summaries remain in the output directory. Final reader verifies independent payload markers/pixels, metadata and real native completion; a held-last-frame control is distinct from completion. Source uploads and final private verification copies are explicit, not end-to-end zero-copy claims. Producer mapping uses the documented optional identity mapping; intermediate/final receiver mappings are required and recorded.

The [frozen plan](../WINDOWS_NATIVE_RELAY_RATE_PLAN.md) defines qualification order and scope. Next: one clean Windows focused build/short smoke after the sole current timing owner releases the lane. Analyze the collected smoke before a target preflight. No rate, physical playback, decoder or M6-completion claim is made here.

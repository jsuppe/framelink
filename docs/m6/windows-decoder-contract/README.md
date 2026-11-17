# Real Windows decoder strict-contract prerequisite

Base `a6b05da69f89fbceb69f5ec35ec3bdf550334462`; exact diagnostic source hashes in status.json. Opt-in `--decoder-contract-diagnostic` requires installed x264, h264parse and d3d11h264dec (GStreamer 1.28.0 on Caspar RTX5090). No production code/default/API changed.

Final build and diagnostic exit0: **8/8** actual 320x240 H.264 frames decoded into D3D11 NV12, exact relative 30fps PTS verified, ArraySize7 / BindFlags512 (DECODER) / MiscFlags0. Each foreign decoder buffer returns FL_INVALID from the strict producer and leaves the consumer empty. This extends the earlier M5a allocation probe with current producer-contract rejection; it is not decoder pass-through integration or a GPU-copy fallback. No payload maps in this diagnostic.

Initial 64x32 all-intra decode failed before producing frames; retained under initial-64x32. A 320x240 control reached a malformed expected-width assertion (2400, fixture string-replacement error); retained under control-320x240. Final fixture corrects the width and uses the existing example's key interval30. These runs do not isolate a driver root cause or qualify small-frame decoding. Existing focused production checks passed5/5 before final CLI-only fixture correction; final diagnostic was separately rebuilt and passed. Do not rerun unrelated tests solely for this correction.

Next: explicit upstream GPU conversion into an issued ring allocation through authenticated coordinated A/V; declare conversion separately from transport and preserve real decoder timing. Stock private NV12 arrays still require an explicit adapter; do not remove completion/ownership guards. Real zero-copy decoder and two-hop PCM acceptance remain open, owner Evelyn. API not frozen; M6 open; M7 hold.

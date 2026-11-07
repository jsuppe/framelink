# Native BGRA 4K30 layout-cache validation

Exact runtime revision and binary hashes: status.json and run.log. Untraced1800s run after112/112 Windows regression pass; pool2/unread1/output1, latency0, transport-only, no-output-preroll. Unchanged34ms diagnostic and64KB/min resource gates.

54000/54000 video frames and86400000 PCM samples;180000 contiguous audio blocks. Zero video/audio arrival misses, inferred A/V mapping delta0ns. Video arrival p9513.556ms/max15.769ms. Consumer privatecommit6.93KB/min (previous69.47); producer6.52KB/min. Both pass64KB/min. CPUconsumer5.81%,producer14.44%; handles/threads slopes effectively flat.

Together with the matched wrapper allocation probe and native NV12 sustained result, this validates native resource repair for both failing4K cases. Original rare159ms stall is not causally closed. BGRA CPU remains a distinct resource failure; this cache is only used on native-memory output. No physical playback or end-to-end zero-copy claim. Original CSVs remain in persistent evidence archive with hashes recorded here.

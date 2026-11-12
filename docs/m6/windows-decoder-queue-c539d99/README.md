# Decoder queue correction: smoke passes, 4K target fails

Exact source `c539d99e538a37a667582fd1d8120d936573792b`.

- [Build/tiny smoke](build-smoke/README.md): 9/9 focused tests PASS in 10.18s;
  60 distinct frames / 96,000 PCM frames, zero misses.
- [4K30 initial control](4k30-initial-control/README.md): complete 150 distinct
  frames / 240,000 PCM frames, but **118 video 34ms misses**, zero audio misses;
  maximum video age **542.183067ms**. Trace-on and endurance were not run.

**The queue change was not sufficient for the 4K30 timing gate.** Next diagnosis
at archival handoff concerned source generation/encoding throughput; this archive
contains no result from that investigation. The [original 77afa1a 4K failure](../windows-decoder-cng-77afa1a/4k30-initial-failure/README.md)
remains intact and is not superseded by a passing claim. These are different
exact-revision captures, not repeated measurements of an identical workload.

Each subdirectory preserves original uploaded/collected manifests, verified raw
files and its own original-byte manifest. Original byte hashes are distinct from
any parent-added post-filter Git-blob manifest. No target qualification, endurance
or physical-playback claim follows from the passing tiny smoke.

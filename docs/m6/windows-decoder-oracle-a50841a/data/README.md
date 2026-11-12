# Prepared source-oracle phase diagnostic — NOT executed

External-only overlay of `test/fl_gst_local_decoder_rate_win.cpp` from exact
`a50841aad44fbc9431234a4f326bad036d193bf5`; source is byte-identical at
`b247e5d11b3ba540f78beeef172874f0606e83fc`. The original, diagnostic source,
unified patch, provenance and SHA256 manifest are retained here. No repository
source was edited. No Windows build/run has occurred.

## Measurement

Existing video CSV columns are retained in original order. Twelve columns follow
`digest`: Map, SHA and Unmap each get QPC wall begin/end and current-thread
user+kernel CPU begin/end (nanoseconds). Other roles emit zeros in these fields.
The original oracle begin/end encloses the additional probes. Each phase's wall
interval excludes its surrounding GetThreadTimes calls; CPU intervals include
measurement overhead. Thread CPU accounting can be much coarser than its 100 ns
unit; zero CPU or CPU greater than a short wall interval is not proof of a stall.

Source video rows are reserved before the epoch and buffered in a bounded
150-entry vector, then written only after media processing and local-session
finish. This removes the original source's per-frame CSV writes and adds no
per-frame diagnostic logging. Existing audio, receiver/relay and resource logging
are unchanged. Early fatal termination can leave only a source CSV header; retain
that failure rather than invent missing observations. Buffering changes observation
overhead: this diagnostic cannot retroactively close the preserved trace-on failure.

The overlay rejects anything except 5 s, 3840x2160@30, existing tape replay.
It changes no media bytes, hashing algorithm/full-frame checks, PTS, queue capacity,
lease completion, pacing, timeout or original 34 ms acceptance threshold. No
encoder, fixture preparation, decoded cache or proposed production fix is added.

## Intended bounded execution after the integrated lane completes

1. In a fresh isolated exact-a50841a archive, verify original source SHA against
   `artifact-sha256.json`; apply `diagnostic.patch`, verify resulting source SHA.
   Build only `fl_gst_local_decoder_rate_win` and its existing dependencies.
   Retain full build logs and diagnostic EXE/DLL hashes; never overwrite production.
2. Reuse the existing tape identified in `provenance.json`: 650192 bytes, SHA256
   `5c3797a3fe9d4c86992cd3be15627fa18bb184334cb557606b3224c30554cb1e`.
   Verify before/after every arm; do not regenerate media. Keep its prior independent
   tape inspection and provenance.
3. With explicit exclusive Windows lane handoff, run one 5 s trace-off and one
   5 s trace-on arm serially in fresh output directories using the same diagnostic
   binary and tape. Do not rerun or extend. Keep the existing finite owned watchdog,
   process isolation, clean environment, original analyzer and final idle proof.
   Preserve timing failures; this is attribution, not a clean-pass waiver.
4. Run the exact-source existing analyzer (150 unique full SHA matches,
   240000 PCM frames, original all-frame 34 ms gate and trace joins), then:

```sh
python3 analyze_phases.py <trace-off>/source.video.csv
python3 analyze_phases.py <trace-on>/source.video.csv
```

Compare slow oracle identities with receiver GPU waits and source conversion
completion already captured. Long Map wall with little thread CPU is consistent
with waiting/scheduling, not proof of GPU causation. Long SHA CPU supports CPU work;
long SHA wall without CPU supports scheduling or another wait. No recurrent outlier
means the earlier transient remains unexplained, not disproved.

## Prepared runnable controller (no execution performed)

```sh
python3 <linux-home>/dev/evidence/m6-cpu-path/windows-decoder-oracle-phase-prepared/run.py \
  --final-build-run <linux-home>/dev/evidence/m6-cpu-path/windows-final-integrated-prepared/runs/final-windows-a50841a-20261112t0819z \
  --revision a50841a \
  --run-id oracle-phase-a50841a-<unique-lowercase-id> \
  --runtime-lane-released
```

The prerequisite must already be collected and successful. The controller rechecks
its complete evidence hashes, CTest coverage, API and unchanged-client compatibility,
revision and final idle. This is not permission to run concurrently with it.
`--runtime-lane-released` attests a new explicit handoff, not just a stale idle receipt.

The controller uploads an exact Git archive and the pinned scripts to a fresh unique
<win-host> directory. The PS1 verifies uploads and refuses a busy/mutex-held lane. Remote
execution checks **every regular source file**, applies the reviewed patch with
`git apply`, and checks the resulting full source set differs only at the intended
file. It builds the decoder executable and required plugin/dependencies only.
No source checkout or production binary is reused or overwritten.

Both arms retain original analyzer gates. **If trace-off fails, trace-on is withheld.**
The phase analyzer still runs on a completed arm whose timing analyzer failed.
Timeouts terminate only owned process trees. Collection verifies manifest coverage,
upload identity, fresh final idle, diagnostic source pin, four EXE/DLL pins and tape
pins before/after each arm, then independently reruns analyzers locally. There is no
retry or automatic recovery execution. An SSH timeout marks remote execution uncertain
and requires manual owned-result recovery instead of competing collection commands.

## Independent review repairs

The integrated validator is synchronized with the current 26-case validator including the optional-PDB policy. Prerequisites now require exact successful controller exits, complete collected manifest, original uploaded script/config/reference identity, exact reviewed reference set, archive hash, full CTest/API/client outcomes, and required runtime-artifact proof; optional missing PDBs are accepted, absent runtime binaries are not. Both diagnostic arms must have raw source CSVs and independently successful local gate JSON with matching trace state. Four diagnostic binary pins must be valid SHA256 values and join before/after; the initial tape must match the frozen fixture. Local phase summaries reject missing or nonfinite results.

Validation:34 local tests pass (26 integrated controls plus8 prerequisite/phase controls); all Python syntax passed. No Windows commands or repo edits were performed during review. The one-file diagnostic overlay itself was reviewed without changes; it preserves full hashing and native lease logic and explicitly remains a short diagnostic, not qualification.

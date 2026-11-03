# M5b final integration — merged

PR #8 merged as `9f86c35` at 2026-11-03 05:08 UTC.

Final source `4fafe51` (M5b tasks 1–13; the following commit `8da5315` only
installs `gstreamer1.0-tools` in hosted CI).

| Gate | Result | Log |
|---|---|---|
| Linux full serial suite | 112/112 | [linux-full.log](linux-full.log) |
| Windows (Caspar) full suite, Release | 110/110 | [windows-full.log](windows-full.log) |
| Validation matrix x1, Linux | `m5b_av` 14/14, `gst_video_regression` 12/12 | [linux-matrix-x1.log](linux-matrix-x1.log) |
| Validation matrix x1, Windows | 14/14, 13/13 | [windows-matrix-x1.log](windows-matrix-x1.log) |
| Validation matrix x1, Android arm64 tablet | 14/14, 7/7 | [android-matrix-x1.log](android-matrix-x1.log) |
| Android builds (arm64, x86_64 plugin/tests; JNI apps) | no warnings | — |
| Hosted CI on `8da5315` (Linux, Windows): build, registration, `gst-inspect-1.0` of six elements, CPU-only regressions incl. six GPU-free M5b tests | pass | [run 41579899377](https://github.com/jsuppe/framelink/actions/runs/41579899377) |

The Windows tree was checked byte for byte against `4fafe51` before the
Windows gates: SHA-256 of all 204 tracked source, test, tool and sample files
matched ([source-sha256.txt](source-sha256.txt)).

Repeat evidence (x10 on all three platforms) is in
[platform qualification](../platform-qualification/README.md) and
[bin parity](../bin-parity/README.md); per-task evidence is linked from the
[roadmap](../../ROADMAP_TASKS.md).

## Review

- Branch-wide hygiene before merge: no debug hooks left (one test-only debug
  print removed), no screenshots, earlier M5b Windows logs redacted to
  `<win-tree>`, all relative documentation links resolve.
- Each task was validated on Linux, Windows and Android before the next began;
  defects found during later tasks (consumer-bin drain after failure, refused
  seeks, late failure on replaced frames, Windows plugin PATH in CI) were fixed
  with regressions or CI checks.

## Limits carried forward

Physical display/speaker alignment is not measured. Coordinated video requires
real-time delivery. Linux discrete-GPU CPU readback (M6.8) and native producer
allocations in sessions (M6.9) are open. Android x86_64 is build-only.

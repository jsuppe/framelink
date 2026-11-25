# Windows full suite at `c485ea44` — 2026-11-25

Revision `c485ea44`: master `0d72a94b` (including the drain-report fix, PR #46) plus PR #47. The PR #47 changes are not built on Windows. Qualification host: RTX 5090, VS 2022 BuildTools, GStreamer 1.28 MSVC; a fresh clone, `cmake --preset ci`, then serial `ctest --test-dir build` under the validation mutex (`suite.ps1`). This record is dated; current status is in [STATUS.md](../../STATUS.md).

## Result: 185/187, two failures

| Test | Result | Output |
|---|---|---|
| `read_lease_d3d11_producer_death` (#22) | **Failed**, 0.44 s | `line 64: v[0]==17&&v[1]==91&&v[2]==203&&v[3]==255` (the consumer read wrong pixels after the producer died) |
| `gst_native_producer_win` (#82) | **Timeout** at 20.05 s | no failure line before the limit |

Total 720 s (`suite-c485ea4.log`). The failed run stands; the repeats below are diagnostics, not replacements.

## Diagnostics (same build)

**`read_lease_d3d11_producer_death`**
- Under ctest `--repeat until-fail`: 30/30 passed (`rep-d3d11-death.log`), then a second series failed on repeat 102 with the same line 64 (`rep2-d3d11-death.log`). Together with the suite, that is 2 failures in about 133 ctest runs.
- Run directly 400 times each (`frozen-compare.ps1`, `frozen-compare.txt`): the frozen revision `060c54e4` passed 400/400, and this build passed 400/400. These runs **neither show that the failure is new nor rule it out**. 0 failures in 800 direct runs against 2 in about 133 ctest runs suggests the failure depends on how ctest launches the test; this was not established.
- What the test does: the producer maps a slot (CPU staging), writes the pixels, submits (`flImageUnmap` queues the upload copy, then the ready-fence signal and a flush on the producer's context), and terminates itself straight away. The consumer acquires the frame once the ready fence completes, copies it, and checks every pixel.
- Hypothesis, not established: when the producer process dies right after the flush, Windows completes its pending ready-fence signal at process teardown without the upload copy having completed, so the consumer sees a "ready" frame with incomplete contents. The failing pixel values were not captured. Telling a product race from test timing needs a diagnostic build that prints the pixels and the ready-fence value.

**`gst_native_producer_win`**
- 30/30 and then 100/100 passed under ctest repeats (`rep-native-producer.log`, `rep2-native-producer.log`). In the 100 repeats, duration was 18.70–19.47 s (mean 19.03) against the 20 s limit.
- At the frozen revision it already took 19.01–19.05 s (`windows-integrated-final` logs). The timeout is the test running out of margin, which predates this work. Splitting the test or raising its limit is a test change for Jon to approve.

## Not done

- No fix. Windows is frozen at `060c54e4` (`m6-windows-api-freeze`); a change to the Windows submit path, or to these tests, is Jon's decision.
- Whether either failure involves the drain-report fix (#46) was not checked.

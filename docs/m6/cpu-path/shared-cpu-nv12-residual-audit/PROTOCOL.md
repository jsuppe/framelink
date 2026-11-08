# Android BGRA4K30 packed-plane copy ABBA control

Independently revalidated all four120s cells: exit/pull0, complete EOS,3600video/5760000PCM at both endpoints,12000contiguous audio timing rows,3600unique sink and consumer stage indices, correct mode, all-core affinity, zero output-cache fallbacks. Diagnostic3a07c721 based93283ac; no production changes. Raw evidence hashes in manifest.sha256; exact commands/build hashes and reproducible validate.py retained. All audio has zero34ms misses and common inferred A/V mapping delta0ns. Inferred PTS mapping is not independent identity proof. Diagnostics are not trace-off qualification.

| Cell | Video >34ms | Arrival p95/max ms | Copy wall p95 us | Copy CPU p95 us | Wall-minus-CPU p95 us |
|---|---:|---|---:|---:|---:|
| a1-row | 0 | 21.231/30.175 | 13146 | 12245 | 3815 |
| b1-plane | 0 | 21.097/30.324 | 13188 | 12317 | 3731 |
| b2-plane | 0 | 21.277/27.203 | 13227 | 12227 | 3880 |
| a2-row | 4 | 22.856/35.779 | 14594 | 12874 | 3885 |

Frozen criterion FAILED: both plane cells must reduce consumer copy p95 by >=10% against both row cells. Versus A1, B1/B2 are0.32%/0.62% slower; versus A2 they improve only9.63%/9.37%. No consistent material improvement; reject promotion. Four A2 misses and neighbors retained in validated-stage-analysis.json; do not selectively compare only A2. Chronological variation remains; neither copy granularity nor prior affinity control demonstrates a remedy. Do not rerun these controls until clean or infer hardware impossibility.

Next task: diagnose the separate NV121080p60 failure on the unchanged production path, using existing all-frame stage instrumentation in a single1800s diagnostic (preceded by5s coverage check). Original one-off miss occurred1313s into the1800s run: index78795,arrival17.863579ms,push1.983386ms,residual15.880193ms. Thus a short450s sample cannot adequately cover that observed phase. This is instrumentation collection, NOT a qualification repeat or waiver. Verify108000video86400000PCM180000audio and108000unique stages each side; preserve all misses/neighbor stages, CPU versus wall, faults, fallback counts and A/V mapping. If no misses recur, conclude mechanism unresolved, not passed; no blind repeated diagnostic. BGRA4K acceptance remains suspended with Evelyn pending a new evidenced mechanism; NV121080 acceptance remains open. M6 not complete; hold before M7.

# Consumer packed-plane memcpy diagnostic (in progress)

See PROTOCOL.md for frozen scope and interpretation. Changes exist only in diagnostic build3a07c721, based on93283ac. Production sources restored and ARM64 production benchmark rebuilt to unchanged dee5c211. No production copy-policy change.

Existing source (padded BGRA/NV12, metadata/retained-output/lifetime/error checks), roundtrip and A/V tests pass in BOTH modes:6/6 Android executions. Five-second BGRA4K30 preflights both deliver150video240000PCM500audio,150stage rows per side,zero fallback and verified all-core masks/copy-mode markers. Row p95/max24.768/30.182ms, plane22.686/24.458ms; zero34ms misses in both. These are coverage checks, not performance acceptance.

Sole durable executor: framelink-m6-packed-copy-20261108T202121.service, observed controller2122466 and Android3394/3395/3397 at launch. Four sequential120s row/plane/plane/row cells. Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/android-packed-copy-control/campaign. Collect campaign-status and each terminal status; independently verify3600video5760000PCM12000audio and3600unique stage rows eachside, then compare all-frame/late-neighbor copy wall/CPU and arrivals. No duplicate executor or blind endurance rerun.

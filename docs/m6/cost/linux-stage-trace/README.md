# Linux native stage captures and rejected CPU setup

Four native-consumer profiles completed with both OS exit codes0, EOS, exact300/150video and240000PCM, zero frozen17/34ms diagnostic misses and16zero-loss trace footers. Native labels describe consumer output; producer still uses CPU upload. These five-second traced captures are not sustained qualification.

NV12 producer map mean .009/.012ms at1080p/4K; copy1.229/4.386ms. BGRA sinkRender3.019/10.166ms; native consumer srcCreate includes waiting for cadence and cannot be interpreted as pure processing time.

All four initial CPU attempts exited1 BEFORE pipelines: benchmark requires seconds>=WARMUP_S+3 (five), so requested one-second samples were rejected. No CPU stage data was obtained. This is a controller-duration error, not a transport regression. Preserved failed logs; CPU-only rerun uses five seconds and distinct output root, without repeating native captures. CPU target support remains unqualified.

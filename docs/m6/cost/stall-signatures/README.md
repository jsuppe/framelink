# Controlled source/post-push timing signatures

Six 5-second native NV12 4K30 controls completed on Linux and Windows. Each delivered 150 frames and 240000 PCM samples with complete queue accounting. Delays were injected once at frame 90 for 150 ms. These are CPU-thread delays, not GPU fault injection.

- linux none: push 0.138 ms; arrival 5.535 ms; residual 5.397 ms.
- linux source: push 150.276 ms; arrival 156.641 ms; residual 6.365 ms.
- linux post-push: push 0.137 ms; arrival 155.655 ms; residual 155.517 ms.
- windows none: push 0.999 ms; arrival 6.001 ms; residual 5.002 ms.
- windows source: push 151.495 ms; arrival 155.507 ms; residual 4.011 ms.
- windows post-push: push 0.894 ms; arrival 156.126 ms; residual 155.232 ms.

The historical first late frame had push 0.725 ms and arrival 159.060 ms (158.335 ms residual). Its first delay therefore occurred after the measured producer source push; later push lateness reflects catch-up. This excludes a simple pre-push source delay as the first-event signature, but does not distinguish upload, driver/GPU scheduling, transport or receiver scheduling. The historical event remains unresolved; no clean-repeat waiver.

Persistent raw CSV and logs: <linux-home>/dev/evidence/m6-cost/stall-signatures. Android arm64 benchmark build and all three RGBA native controls passed (150 frames,240000 contiguous PCM samples,complete queues,verified injection and affine PTS mapping). Android native NV12 was rejected by the existing exact-layout guard; this negative is retained and is not a newly supported cell. RGBA source delay: push150.543ms/residual12.199ms; post-push: push0.212ms/residual162.148ms.

## Remaining dependency

M6.5c causal attribution is suspended, owner Evelyn. Resume when joined per-frame boundary and GPU-completion evidence (M6.6a) can distinguish driver/GPU/receiver scheduling for the historical class of event. CPU delay controls alone cannot resolve it. Original acceptance stays open under M6.2b/e.

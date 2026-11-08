# NV12 1080p60 residual attribution

Single 1800-second diagnostic on Android, base93283ac / diagnostic0f1e89a4. Independently verified 108000 video / 86400000 PCM frames at each endpoint, 180000 contiguous audio blocks, and complete unique 0..107999 sink/consumer stage coverage. Exit/pull/analysis0, zero output fallbacks. All startup and drain frames included.

No 17ms miss reproduced: arrival p95 8.499278ms, maximum12.928569ms; audio maximum6.354042ms, common inferred A/V mapping delta0ns. Consumer copy wall p95 3837us, CPU p952887us; wall-minus-CPU p952819us. Separate quantiles are not additive. Original failure phase index78795 stages retained. Resource slopes consumer6.41KB/min; see raw summary for producer/resources.

**Unresolved, not qualified.** The prior trace-off17.863579ms failure remains an acceptance failure. Trace instrumentation changes scheduling; this successful diagnostic does not waive it or identify a remedy. No blind repeat. NV12 1080p60 and BGRA4K30 acceptance suspended, owner Evelyn, resume after an evidenced mechanism plus matched control. No production changes.

Raw durable evidence: <linux-home>/dev/evidence/m6-cpu-path/android-nv12-residual-audit. manifest.sha256 records all raw inputs and scripts. validate.py reproduces analysis from that directory. Next ready independent task: refresh Linux shared-CPU final-revision rate cells after scoped input-retention change, starting BGRA1080p60 preflight then1800s; maintain existing budgets and all-frame gates. M6 remains open; hold before M7.

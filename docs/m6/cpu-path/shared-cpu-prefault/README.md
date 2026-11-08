# Shared CPU producer startup preparation

Baseline4f37f25 BGRA1080p60 1800s:108000video/86400000PCM, two startup arrival failures(index0/1), max21.381ms. All later frames meet17ms. This remains a failed timing diagnostic, not qualification (see adjacent android-bgra-1080p60-cache-diagnostic).

Bounded independent Android memfd control:200/200 full-byte comparisons pass. Fresh shared mapping write median11.058ms with2025minor faults; preparing physical pages before write reduces median copy to2.101ms with0faults, moving median7.788ms into setup versus0.116ms. Interleaved100samples per mode; all raw samples retained. This supports first-write fault cost; it does not alone prove every end-to-end startup delay.

Bounded fix: explicit shared-CPU GStreamer sink acquires/cancels each existing slot, initializes active rows before track-ready. No synthetic frames, new pool slots, timestamp changes, hidden fallback or ownership bypass. Setup bounded by connect timeout with per-row cancellation checks. Native/GPU and Windows paths unchanged (POSIX include excluded on Windows). Padding remains untouched, per-frame copy accounting unchanged; initialization is setup work, not payload transport.

Linux6/6 affected source/sink/A-V/event/bin tests pass; Android roundtrip/A-V/events pass; arm64 and x86_64 Android builds pass. Initial build field-name typo preserved in raw log, corrected before tests. Loom pre-edit context unavailable: missing chromadb; no traceability success claimed.

BGRA1080p60 post-fix5s preflight:300video/240000PCM, zero17ms misses, max11.796143ms. Inferred A/V mapping delta0ns. No endurance claim. Preflight status names base4f37f25 plus the uncommitted sink change SHA256f2dc91a312f646d3e3de00e6f07d33f946d103f0a346b1acbc60c12d28769c77; exact binary8266261c62c9e10a32623db4f950aaf791bbd811b99d6621bba34b4cb48e00aa. The following commit freezes that same source. No performance budget relaxed. Next: BGRA1080p60 sustained, then BGRA4K30 preflight; affected final gates still need exact-revision refresh.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/android-shared-first-touch and <linux-home>/dev/evidence/m6-cpu-path/shared-cpu-benchmark/android-shared-bgra-1920x1080-60-5-prefault.

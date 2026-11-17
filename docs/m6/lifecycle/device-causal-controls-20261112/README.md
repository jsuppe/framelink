# Windows device allocation and deferred-reclaim controls

Collected serially by qualification_recovery on 2026-11-12, 16:56:06–17:03:49 UTC.
No FrameLink/GStreamer production changes, transport, rendering, or gate waiver.
This is attribution instrumentation, **not production resource qualification**.
The earlier 21.10/84.09 KiB/cycle and held-lease82.528 KiB/cycle failures remain.

## Method

Standalone D3D11 diagnostic; 100 short cycles per batch. Every cycle creates a
hardware/WARP device, optionally two shared BGRA1920x1080 textures, flushes,
waits50ms, releases textures/context/device (final reported COM refs0), and samples
process private commit plus VirtualQuery committed-private/mapped/image totals.
After churn stops, sample for30s. No FrameLink or GStreamer DLL linked.
No GPU rendering means this does not measure in-flight texture/fence/lease leaks.
Feature level11.0, explicit ClearState and50ms pacing differ from the historical
5s/300-frame minimal probe; these new slopes must not be compared as matched fixes.

All six arms ran sequentially under Local\\FrameLinkFocusedWindowsValidation.
Source/CMake uploads and raw outputs match27 remote SHA256 entries in the three
remote-manifest files; each isolated executable's before/after hash is unchanged.
Observed no matching benchmark/Python processes before/after each controller;
logs include SSH warnings/module-progress noise, not runtime rows. No driver update,
process kill, Trim, working-set purge, hidden sleep inside production, or cache fix.
The three controller exits0 mean completed diagnostics, not acceptance.

## Allocation-factor control

After five warmup cycles, private-commit KiB/cycle:

| Backend | Empty device | Two shared textures |
| --- | ---: | ---: |
| Hardware |63.524|61.586|
| WARP |0.050|0.936|

Growth tracks committed-private regions, not mapped/image commitment. That is a
memory-category observation, not an allocation-stack attribution. Textures,
FrameLink, and GStreamer are unnecessary for this fast hardware-churn symptom;
WARP differs in more than vendor driver alone, so this does not identify a module.
Hardware private commit fell23–25MiB after teardown, predominantly around6s.

## Repeated batches in one hardware-empty process

| Batch | Last-cycle KiB | After30s idle KiB |
| --- | ---: | ---: |
|1|37684|13524|
|2|39364|15072|
|3|45316|14804|

Peak churn memory rises, but these three settled observations are not monotonically
increasing. This establishes substantial deferred reclamation; it does not prove
bounded memory over arbitrary repetitions or eliminate residual retained memory.

## Persistent-device anchor control

Keep a distinct hardware device alive during100 empty-device churn cycles and30s
idle. Private commit goes from56164KiB after anchor creation to88772 after churn,
then61584 after idle **while the anchor remains alive**. Release anchor:24372KiB
immediately,10040 after another30s. Therefore, the simple hypothesis that cleanup
requires every device in the process to close is rejected by this control.
A real persistent peer has additional queues/resources; no production equivalence.

## Consequence

Do not insert sleeps/Trim/cache or weaken the per-cycle gate on this evidence.
A global device cache changes ownership and ordering and remains unjustified.
The existing fresh-device production profile remains unqualified. The next
resource acceptance evidence must be a real backend/driver remedy with matched
production measurements or an explicitly approved device-lifetime contract—not
more equivalent teardown probes or a clean duration rerun.

Reproduce raw analysis: `python analyze_device_causal_controls.py` from this folder.
It checks800 churn cycles,270 settle samples, phase counts and zero final refs.
Original-byte hashes in artifact-sha256.json are distinct from committed-Git
blob hashes because Git may normalize text line endings. Raw CSVs are UTF16LE.

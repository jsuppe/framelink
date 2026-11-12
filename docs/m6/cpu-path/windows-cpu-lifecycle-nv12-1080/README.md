# Shared-CPU production lifecycle — NV121080p60

Frozen461584b production DLLs, test-only benchmark guard correction. No production
lifetime/API/default change. Original harness rejected shared-cpu plus cycles before
media; rejection retained. Both lifecycle roles already use shared-CPU-enabled bins.
Initial renderer-parent build forcibly disabled tests; standalone focused build passed.
New benchmark SHA256 acbac149eff82555f134c653ba5d100e7b35bc38209716967e1a8021326f175d.

Persistent consumer / fresh producer:2x5s control PASS, then100x5s PASS23:31:16Z.
All100 cycles deliver300video/240000PCM each:30000video/24000000PCM total.
Original first5 warmup exclusion and16KiB/0.1handle/0.02thread-per-cycle limits:
private-commit slope **-2.192665KiB/cycle**, handles **-0.151568**, threads **-0.065356**.
Independent local slope/count reconstruction passed;126remote hashes/fullcoverage,
original3DLL pins unchanged and finalidle0 verified. This does not qualify native
D3D reconnect, other targets, persistent producer, physical output or per-frame timing.

Raw evidence and original byte hashes are under the two external directories with
matching names in <linux-home>/dev/evidence/m6-lifecycle. Executables/DLLs remain
external (excluded from repository archive); committed text uses localenv path
normalization, so separately pin Git blobs. Original failure is not relabeled pass.

Next serial task: opposite persistent role, same limits and production DLLs.
M6 open; M7 held.

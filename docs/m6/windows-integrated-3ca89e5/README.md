# Exact 3ca89e5 Windows integrated gate — FAILED

Source revision `3ca89e54e64d7a0baf8ba52e4c12ee0cdfd9c6f0`; Windows host <win-host>.
Clean build and API inspection exited 0. Full serial CTest: **130/131 passed**, 533.37 seconds, exit 8. `gst_av_qualification` failed in its bin CPU-mapped native transport cleanup check: handles 1563,1567,1567,1571,1571,1573,1573,1573. This is NOT the newly added shared-CPU transport path. No failed gate waived; API is not frozen.

Raw status/test/API logs preserved here; full build/archive/controller at `<linux-home>/dev/evidence/m6-cpu-path/windows-integrated-3ca89e5`. Production DLLs remain exact revision. After collection, the remote test source was changed for a temporary 100-cycle bin-only handle-type diagnostic (not a clean full-suite retry); canonical repo source is unchanged. Diagnostic evidence at `<linux-home>/dev/evidence/m6-cpu-path/windows-av-handle-diagnostic`.

Owner Evelyn: determine handle types and bounded initialization versus retained per-cycle ownership before fixing or qualifying lifecycle. Windows automatic CPU endpoint discovery remains next after resolving this failure. Linux BGRA already archived, not rerun. All M6 acceptance gates remain open; hold before M7.

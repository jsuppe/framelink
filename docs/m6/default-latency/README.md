# Primary transport timing default

Confirmed requirement: no intentional per-boundary playback delay. The public
`framelinkavsrc` default `latency-ms` is now0, both its GObject property metadata
and instance value. A session-owning bin installs0 pipeline latency by default.
Explicit `latency-ms=150` preserves the former scheduled-playback setting.
Application-managed contexts still own their pipeline clock/latency setup.

The default test checks actual top-level pipeline latency after READY, not just
the property. Existing scheduled-playback cases now explicitly request150ms and
retain their original per-track timing, content, lifecycle and failure checks.
Linux and Windows src-bin+benchmark smoke checks passed2/2 each; Android src-bin
suite passed. gst-inspect reports default0. Logs are alongside this file.

Pool size, queue bounds, overload policy, GPU readiness/lifetime and final sink
synchronization are unchanged. Zero configured latency is not zero elapsed time,
and does not disable sink preroll or hardware buffering. Migration is documented
in GST_AV_MIGRATION.md; final-playback samples retain their explicit setting.

The prior full Linux114/114 run predates this default change; affected tests here
do not substitute for final integrated M6 qualification or a merge claim.

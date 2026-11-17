# Windows preconnect negative-control contract repair

Base a2d25b4, Windows <win-host>. Original exact integrated suite failed 1/133: legacy BGRA received88/scheduled78/late10. Ready BGRA and NV12 each received/scheduled100, late0. Original media/logs/traces retained in original/. Runtime trace verifies deliberately delayed lazy-open: 501.3901ms, starting21.2us after first frame receive, no preconnect. This is an expected injected regression, but the subprocess helper applied its ordinary <10 late limit before the parent could evaluate the negative control.

Repair is test-only: explicit --preconnect-negative-control, restricted to producer clock and exact350ms lazy-open injection. Negative requires actual loss or late video; normal late budget unchanged. All original PCM, hashes, PTS, durations, terminal frame, changing pixels, exit status, runtime injection and readiness assertions retained. No public API/production binary changes; no waiver of prior uninjected548fe19 startup failure.

Windows focused tests **2/2 pass**,28.27s (external pipeline and preconnect). Ready and both launch orders100/100/0late. Legacy BGRA85received/8late, NV1281received/4late. Missing-consumer bounded cancellation passes. Four CLI guard negatives pass locally. Six actual assertion-block replays pass against retained original Windows records: normal rejects original10late; explicit negative accepts; healthy data rejects negative; healthy normal passes; corrupted pixels and missing PCM reject even in negative mode. Reproduce with:

```
python3 docs/m6/windows-preconnect-negative-control/replay.py --helper test/m5_external_pipeline.py --original docs/m6/windows-preconnect-negative-control/original/m5-preconnect-87wwdyod
```

No deferred Linux/Android campaign. Loom briefing unavailable (CLI absent). Exact integrated verification follows this focused correction; M6 and API acceptance remain open.

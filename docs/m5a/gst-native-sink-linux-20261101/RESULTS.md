# Task 7 partial: Linux native DMA-BUF sink

Parent-reviewed native sink wraps the original LINEAR allocation, with producer
reference pins, explicit readiness FD, exact metadata and bounded acquisition.
RGBA and NV12 fixtures verify backing-object identity, pixel content, capacity,
readiness blocking, metadata, and retained memory after sink stop. Missing
readiness rejects publication. Linux affected tests passed 4/4; Windows affected
compatibility tests passed 5/5; Android arm64/x86_64 plugin builds passed.

The content oracle uses CPU writes; eventfd is a controlled readiness test, not
GPU-driver SYNC_FD qualification. No new Android runtime run for this Linux-only
slice. This is an explicit application adapter, not generic decoder integration.
POSIX ALLOCATION pools and authenticated remote memory agreement remain open.

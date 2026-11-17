# Source/local-consumer controls after NV21 optimization

Same NV12 source formats/rates and10s duration, no transport. Source-only CPU21.53%/27.29% at1080p60/4K30, bothEOS. Local-consumer includes usual queues/appsinks/timing collectors:600/600 and300/300frames, allPCM, zero video/audio deadline misses. Steady video-arrival p95 .80/.85ms; CPU27.83%/32.02%.

These exclude source-only generation or local queue/recorder cost as sufficient explanations of the failed whole transport profiles. They do not individually identify transport substage cost or quantify interaction under full-path contention. Next: bounded diagnostic core/GStreamer stage trace, not a qualification rerun.

# Android4K30 sustained primary-profile control

Runtime:b5236a4, deployed binary hash verified. TCL8483A/API35, arm64 API29.
RGBA3840x2160 at30fps,1800seconds, native consumer, CPU-copying producer,
pool2/unread1/output1, latency0, sync=false, `--no-output-preroll`, dual-track
raw producer/consumer timestamps. Trace off. One qualification case at a time.
The34ms arrival diagnostic was selected before the preflight and long run.

-54,000/54,000 video frames;86,400,000 PCM sample-frames; EOS on both sides.
-180,000 continuous10ms audio blocks; no queue pending/unmatched records.
-0/54,000 video and0/180,000 audio arrivals over34ms.
-All-frame video arrival p9512.00ms,p9913.90ms,max23.48ms;
 appsink output p9512.81ms,max24.06ms (startup included).
-All-frame audio arrival max6.76ms; appsink output max7.42ms.
-Constant producer-to-consumer PTS mapping on each track, same offset for both:
 relative A/V mapping delta exactly0ns throughout the recorded streams.
-Producer58.12% CPU,consumer15.64%; RSS slopes-2.92/-2.30KB/min;
 handle/thread slopes approximately0. No sustained resource growth observed.

Raw CSV hashes, full summary, derived correlation and binary hash are saved
alongside this document; raw files remain in the persistent evidence archive
under m6-soak/android-4k30. Inferred PTS alignment does not independently prove
allocation identity. Arrival latency includes producer processing/copy and
consumer work; zero configured delay is not zero elapsed time. This capture
does not qualify GPU sampling, physical playback, arbitrary formats or drivers.

This completes this single case, not M6.2's full cross-platform matrix or M6.
The previous1080p60 soak used older instrumentation/default preroll; it remains
separately labeled and is not silently promoted to this exact profile/revision.

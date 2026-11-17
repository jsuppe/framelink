# Windows consumer-restart lifecycle: resource gate FAILED

Production29bbddb, benchmarkd514fc7. Two-cycle preflight complete; 100x5-second native BGRA1080p60 consumer restarts all delivered 300video/240000PCM and EOS. Persistent producer private-commit slope after frozen five-cycle warmup: **84.09 KB/cycle >16**. Handles slope -0.112, threads -0.041 percycle. Resource endpoints63464→64256KB do not override the failing regression fit. Controller exit1 correctly rejects qualification despite benchmark exit0. Raw fresh-peer files remain under <linux-home>/dev/evidence/m6-lifecycle/windows-consumer, hashes retained here.

Both Windows directions now have resource failures. Device churn reproduces growth without FrameLink, but this does not attribute all84.09KB/cycle to that mechanism. No threshold adjustment, Trim production change, or global-device sharing.

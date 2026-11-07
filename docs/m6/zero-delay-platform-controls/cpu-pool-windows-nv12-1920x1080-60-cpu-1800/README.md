# NV12 CPU 1080p60 output-cache sustained regression

Runtime29bbddb CPU cache; exact component revision/binary hashes in status.json/run.log. Untraced1800s, unchanged pool2/unread1/output1,latency0,transport-only,no-output-preroll;17ms diagnostic and64KB/min resource gates.

108000video/86400000PCM,180000contiguousaudio, complete queues, zero video/audio arrival misses, inferred A/V mapping delta0ns. Video arrival p955.969ms/max7.883ms. Consumer privatecommit3.25KB/min,producer7.34KB/min: both pass64KB/min. CPUconsumer15.02%,producer8.27%; handle slopes0.03/min each, threads0.01/0.02 per minute, below fixed limits.

CPU-cache sustained matrix2/4 complete; BGRA1080p60 andNV124K30 remain. Original native159ms intermittent stall and other M6 gates remain open. No physical playback or end-to-end zero-copy claim.

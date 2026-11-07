# BGRA CPU 1080p60 output-cache sustained regression

Runtime29bbddb CPU cache; exact component revision/binary hashes in status.json/run.log. Untraced1800s, unchanged pool2/unread1/output1,latency0,transport-only,no-output-preroll;17ms diagnostic and64KB/min resource gates.

108000video/86400000PCM,180000contiguousaudio, complete queues, zero video/audio arrival misses, inferred A/V mapping delta0ns. Video arrival p956.530ms/max10.139ms. Consumer privatecommit7.18KB/min,producer7.22KB/min: both pass64KB/min. CPUconsumer14.96%,producer9.50%; handles/threads effectively flat.

CPU-cache sustained matrix3/4 complete;NV124K30 remains. Original native159ms intermittent stall and other M6 gates remain open. No physical playback or end-to-end zero-copy claim.

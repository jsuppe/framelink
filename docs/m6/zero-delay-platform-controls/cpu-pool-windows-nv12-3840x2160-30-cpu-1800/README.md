# NV12 CPU 4K30 output-cache sustained regression

Runtime29bbddb CPU cache; exact component revision/binary hashes in status.json/run.log. Untraced1800s, unchanged pool2/unread1/output1,latency0,transport-only,no-output-preroll;34ms diagnostic and64KB/min resource gates.

54000video/86400000PCM,180000contiguousaudio, complete queues, zero video/audio arrival misses, inferred A/V mapping delta0ns. Video arrival p9512.653ms/max15.066ms. Consumer privatecommit7.32KB/min,producer46.69KB/min: both pass64KB/min. CPUconsumer15.40%,producer9.18%; handles/threads flat.

CPU-cache sustained matrix4/4 complete on29bbddb. All selected current Windows native/CPU resource cells pass; original failures remain historical evidence. Original native159ms intermittent stall and other M6 gates remain open. No physical playback or end-to-end zero-copy claim.

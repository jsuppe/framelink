# Linux lifecycle endurance — primary profile

Harness5712397, native BGRA1080p60, pool2/unread1/output1,latency0, sync=false/async=false;100cycles×5s per direction, sequential. All200 cycles delivered independently expected300video/240000PCM each with EOS. Each fresh-peer file retained in persistent <linux-home>/dev/evidence/m6-lifecycle/linux with hashes here; long-lived per-cycle counts/resources and summaries included.

- Restart producer, persistent consumer:100/100,506.6s. After5warmup RSS14768→14996KB, slope2.17KB/cycle; handles8→8,threads7→7.
- Restart consumer, persistent producer:100/100,509.0s. RSS48788→48868KB,slope0.81KB/cycle; handles7→7,threads6→6.

Both pass unchanged16KB/cycle,0.1handles/cycle,0.02threads/cycle gates. Small positive RSS growth is retained, not described as zero. These cases use fresh session paths and real peer process restarts; they do not alone qualify held native leases, midstream generation change, pause, crash recovery or physical GPU use. M6.3a Windows/Android andM6.3b remain; M6.2 acceptance dependencies remain open.

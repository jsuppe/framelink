# Windows lifecycle resource failure — retained

Production29bbddb + harness5712397. Both2x5s direction pilots passed;100x5s producer restarts with persistent native BGRA1080p60 consumer delivered100/100 EOS cycles, independently300video/240000PCM each. Pool2/unread1/output1,latency0,sync=false/async=false unchanged.

Memory gate FAILED: after5warmup, privatecommit fitted slope21.10KB/cycle exceeds frozen16KB/cycle. First/last warm samples38540→37372KB decrease, but middle/later samples fluctuate36520–41884KB; the regression still fails and is not waived by endpoint comparison. Handles1490→1478,threads18→14; no corresponding handle/thread growth. Consumer-restart100-cycle case was not launched after this failure.

No leak cause established. Next discriminating control repeats the same Consumer local-source pipeline lifecycle without FrameLink transport/GPU wrapping; keep full privatecommit snapshots and expected PCM/video/continuity. This tests shared GStreamer/harness setup/teardown rather than repeating an identical failing campaign. Original failure remains open until attribution and validation, no threshold or warmup adjustment.

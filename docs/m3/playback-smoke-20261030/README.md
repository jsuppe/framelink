# Optional playback branch smoke check

Linux BGRA and NV12 --play runs (30 verified video/audio pairs each) passed using
Xvfb and a dedicated PulseAudio null sink. No existing default sink was changed;
the temporary sink module was unloaded afterward. This exercises videoconvert,
automatic video/audio sinks and the simultaneous verification branch, but is NOT
physical speaker/display synchronization or audible-glitch qualification.
Reproduction script uses the local build path /tmp/framelink-audio-build; change
that path for another checkout. Xvfb/xvfb-run and a PulseAudio-compatible server
are required. Logs record test output only, no rendezvous keys.

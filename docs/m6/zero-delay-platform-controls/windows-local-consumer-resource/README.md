# Windows local-consumer resource baseline

1800s NV12 4K30 source→queue→appsink control (e109432 benchmark/harness over b5236a4 + gst/test3e0cd85). No FrameLink transport or native wrapper. Complete54000video/86400000PCM,180000 contiguous10ms audio blocks, exact queue accounting, zero34ms video/audio misses. Privatecommit slope7.34KB/min, flat handles/threads, CPU5.66%. Raw CSV counts/PTS and hashes independently checked.

Source-only baseline7.36KB/min and local-consumer7.34KB/min are below64KB/min; neither reproduces native-consumer69.28KB/min. This narrows the investigation to work absent from local control (transport/session/native wrappers), not proof of any one cause. Next bounded diagnostic isolates GStreamer D3D11 wrapping and its per-frame temporary layout probe. Production remains unchanged.

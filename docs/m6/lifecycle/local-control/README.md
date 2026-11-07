# Local-consumer lifecycle baseline

Harnessd514fc7, no FrameLink transport or native GPU wrapping; same local BGRA1080p60 sources, appsinks, queues and300video/240000PCM per5s cycle. Windows100/100 completedEOS, continuousaudio, independentcounts. After5warmup privatecommit slope-1.994KB/cycle;15008→14436KB. Handles1477→1428,threads16→7, both non-growing. This does not reproduce original transport-native21.10KB/cycle failure and does not qualify transport.

Next isolate D3D11 device/texture lifecycle from GStreamer native wrapping with a bounded A/B probe, holding identical2textures and5s lifetime percycle. No threshold/warmup change or claim of a leak from the original oscillating series.

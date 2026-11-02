# GStreamer frame metadata

Linux affected suites 3/3; Windows Caspar 4/4 (includes deliberate-copy negative);
Android tablet timing, native-source and pool-policy executables all passed.
Android arm64/x86_64 API29 builds passed; x86_64 runtime not tested.

Timing cases include legacy PTS controls; optional DTS and variable duration;
missing metadata fields remaining absent; variable-rate caps; offset/2x segment
mapping; overflow rejection; full GstBuffer-copy generation retention; two-boundary
relay preserving generation and using current buffer duration rather than stale meta.
Native source lifetime/identity and bounded pool regressions remain passing.

This validates timing metadata separately from native source memory. It does not
claim a decoder-to-native-sink-to-GPU end-to-end path, remote memory agreement,
performance qualification, audio clock alignment, or physical output measurement.

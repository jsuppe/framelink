# Post-wake-fix Windows native two-boundary A/V checks

Wake fix ef80d2c; benchmark executable87AFD556FB4F9BA202CE6B5322E76EFBCF89C1384828ED6F10DFE5FFD52F5212, DLL1A1262E18E0763B8E4CF27986ED27112173EED58D0F7D58316CFF94ABF899B7F.

NV12 and BGRA1920x1080/60,pool2,3720frames,warmup120; three independent processes with two boundaries and simultaneous48k stereoPCM. NV1259.997122fps, BGRA59.997925fps; both3720/3720 unique frames,7440boundary joins,3720GPU completions, zero recorder loss. Both2976000PCM sample-frames and unchanged37ms producer/consumer A/V timestamp offset. Exact artifacts/analyzer summaries retained.

Source fill/upload and private full-frame consumer GPU copy/readback remain explicit. Relay payload maps0; no end-to-endzero-copy or physical-output claim. These native checks do not resolve the separate historical39/40 CPU-copy startup loss.

# Linux BGRA1080p60 current-profile sustained case

Runtime b5236a4; 1800seconds, native consumer, pool2/unread1/output1, latency0, transport-only, no-output-preroll, both-track producer/consumer raw capture. Frozen17ms arrival diagnostic.

108000/108000video+86400000PCM,180000continuousaudio blocks; EOS, complete queue accounting, no arrival diagnostic misses. Inferred A/V PTS mapping delta0ns. All-frame video arrival p958.08/max14.95ms; output p958.16/max15.07ms. Producer39.22%CPU,consumer1.93%; RSS slopes+3.23KB/min on both, handles/thread slopes effectivelyzero. Small RSS growth is recorded, not claimed zero; resource qualification remains M6.5.

Producer still copies to ring. Native caps do not prove GPU sampling or end-to-end zero-copy; no physical-output claim. Remaining Linux formats/sizes and Windows current-profile soaks remain open. Raw archive linux-current under persistent M6 soak evidence, hashes retained here.

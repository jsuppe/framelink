# Linux BGRA4K30 timing diagnostic failure — retained

Runtime b5236a4;1800seconds,pool2/unread1/output1,latency0,transport-only,no-output-preroll,dual-trackraw,traceoff. 54000/54000video and86400000PCM,EOS/queueaccounting complete, inferredA/Vmappingdelta0ns. **165video arrivals exceeded frozen34ms diagnostic**, max43.64ms;p9517.79ms. Audio0/180000misses. No frame loss.

Producer pushmax12.66ms, but sampled late frames had ~0.1ms pushlateness and37–40ms post-pushresidual. Residual includes producer copy/transport/consumerwork, not sufficient to attribute cause. A later passing control would not prove this fixed. Bounded traced reproduction is next. Threshold/pooldepth unchanged. No physical-output or native-producer zero-copy claim.

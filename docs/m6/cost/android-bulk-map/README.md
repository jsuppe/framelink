# Diagnostic bulk-row NV21 control

Same acquired AHB and lock/unlock contract as the baseline. Bulk native Y/VU row copies followed by cached chroma swapping. Distinct U=79/V=173 and padded 130x66 checks passed. 300 measured iterations after 20 warm-up at each target size.

Map/unmap mean: 1080p 4.385/0.885 ms; 4K 17.105/3.331 ms. This is a throwaway control, not production qualification or AV throughput evidence. Production integration must retain generic-layout fallback and AHB unlock/cache boundary, with independent tests.

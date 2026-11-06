# Linux allocation-lifetime CPU mappings

M6.2 BGRA4K30 failure: complete54000frames but165 arrivals>34ms. A60s traced reproduction retained7/1800misses,zero trace loss; sinkRender max38.15ms while mapmax0.445ms,unmapmax0.330ms,submitmax0.042ms. Map-return→unmap-entry span max37.71ms. This attributes delay to the mapped write interval, not waiting for transport queue capacity.

A20s matched no-trace process sampling control recorded producer4,455,000minor/0major faults, consumer0. flImageUnmap destroyed the mmap every frame, causing page faults on each touched page. Repair retains per-allocation VMAs until freeImage, while DMA_BUF_SYNC START/END remain per CPU-access epoch. Partial map failures end successful epochs; retained mappings are destroyed on allocation teardown. Resizing/reconnect and reader/fence ownership are unchanged. This is not removal of producer copies or the deferred CPU-readback/native-producer integrations.

After repair, same20s control sampled0additional minor/major faults for both processes;600frames+960000PCM complete,0/600video and0/2000audio diagnostic misses. Video arrivalp9513.57/max16.53ms;producerCPU44.32%. Short control supports mechanism but does not close long-run gate.

11 affected tests passed: portable maps allformats, leases/ownership/metadata/receivequeue/resize/reconnect. strace on8patternedframes recorded32successfulDMA_BUF_IOCTL_SYNC calls, two shared65536-byte mappings (producer/consumer), bothmunmapped at teardown. Long-run repair validation pending. Original failing captures retained.

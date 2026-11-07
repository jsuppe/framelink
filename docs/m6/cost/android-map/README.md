# Android CPU mapping stage-cost baseline

Single acquired buffer, no AV, no GPU load, same production FL_MAP_CPU allocation usage.300measurements after20warmup. NativeYUV planes expose NV21 (U/Vpixelstride2,Vaddress=U-1). Contentverified before/after. Diagnostic, not profile qualification.

1080pNV12: rawlock/unlock0.028/0.003ms; publicmap/unmap389.37/17.55ms.4KNV12: raw0.027/0.003ms; publicmap/unmap1531.69/70.17ms. ThreadCPUapprox405.83/1597.48ms permapcycle. PublicNV12copy addsabout1.01/2.81ms oncachednormalizeddata. RGBAraw/publicmapisfast; bulkCPUcopy10.98/42.24ms at1080p/4K. Noaveragesrelabelledasdeadlines.

Codeinspection: non-nativeNV12 map readsindividualY/U/Vbytes fromAHBlockmemory; unmap reverses the conversion even for a read-oriented caller because existingAPI isread/write. Lockitself doesnotexplain measuredcost. Nextisolatedcontrol copiesnativeY/NV21rows inbulk then swapsUV incachedmemory; outgoingrows convertedincachebeforebulkstore. TestdistinctU/V andpadded130x66 case. No productionchange orremovalofunlock/cachevisibilityboundary.

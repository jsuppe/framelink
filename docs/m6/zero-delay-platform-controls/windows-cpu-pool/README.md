# Windows CPU output cache validation

Production/test revision 29bbddb: Windows-only generic GstBufferPool with maximum two reusable buffers, nonblocking acquisition and fresh allocation fallback when downstream retains both. Old pool deactivated on ring generation/geometry change, stop and finalize; references held by downstream outlive the source. Native texture/fence/lease path unchanged. This cache does not limit total downstream-owned memory.

Affected Windows6/6 passed including four retained samples beyond cache capacity and stop/start in padded NV12/RGB10 roundtrips. Additional native-source/copy-negative2/2 passed with CPU NV12 grow/shrink64→640→128width; retained old samples preserve exact extent,PTS and pixels across resize/teardown. Linux rebuild plus both modified roundtrips2/2 passed (2.94s).

The isolated allocation probe establishes reduced churn, not the original fitted98.11KB/min retained-growth root cause. Full Windows suite and sustained BGRA CPU resource gate remain pending. Keep the historical failure and original159ms native stall open.

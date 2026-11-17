# Shared-CPU descriptor extension v1

`framelink_shared_cpu.h` defines an explicit 128-byte `FCS1` little-endian message.
It does **not** alter channel protocol v6, session protocol v1/160 bytes, or native
DMA-BUF/AHB/D3D11 descriptors. Selecting the shared-CPU extension and authenticating
both endpoints remains a prerequisite to transport integration.

A descriptor identifies one allocation within a nonzero generation. Allocation
IDs are logical identities, not OS handles or authentication evidence. Handles
are transferred separately and must be checked against the descriptor, access
rights and a live ownership lease before media is accepted.

Wire offsets (bytes): magic0, version4, resource8, format12, width16, height20,
plane count24, flags28, generation32, allocation ID40, allocation bytes48,
reserved56–63, four 64-bit plane offsets64–95, four 64-bit strides96–127.
Only `FL_MEMORY_SHARED_CPU` is accepted. Flags/reserved/unused entries are zero.

Limits: nonzero dimensions <=8192, allocation <=1GiB, one packed BGRA/RGBA/RGB10A2
plane or two exact NV12 planes. NV12 geometry is even. Packed offsets/strides
are 4-byte aligned, NV12 2-byte aligned. Full-stride plane spans, including
padding, must fit the allocation, be ordered and disjoint. Prefix and inter-plane
padding are permitted. Checked division avoids overflow before multiplication.

Encode/decode preserve output on failure and support aliasing. Decode is not an
allocator, mapper, stable-file-size check, ownership transfer, or GPU capability
probe. Both participants must permit actual endpoint COPY operations through
`flMemoryEvaluateBoundary`; a CPU-only path cannot satisfy GPU usage demands.

Validation: C++ format/byte-order/overflow/overlap/alias tests, 1,024 deterministic
wire mutations, both-endpoint permission/GPU/resource negatives, exported C11 API,
and existing memory/session contracts. Seven tests passed on Linux, Windows and
Android arm64; both Android ABIs built. Windows' preexisting platform-specific
CMake omissions were preserved, not overwritten; exact build hashes are retained.

Next implementation step: stable-size shared allocation/import and retained map
ownership, then explicit channel/coordinator/GStreamer negotiation. No shared-CPU
media transport is implemented merely by this codec. M6.8b/c remain open.

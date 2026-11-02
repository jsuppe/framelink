# External allocation adoption — M5a development API

Consumer endpoint ownership is separate from physical allocation ownership.
The new factories adopt existing allocations into an immutable FrameLink ring;
they do not allocate replacement pixel storage or copy pixel payloads. Producer
reservation, readiness and consumed-fence rules remain mandatory. These APIs are
in development, not a released runtime negotiation protocol.

## Platform entry points

- Windows `flCreateChannelFromD3D11Handles`: borrowed shared NT texture handles,
  selected adapter LUID, explicit geometry/logical format. Independently duplicates
  handles and opens their existing resources. Rejects aliased kernel objects,
  mismatched geometry/format, texture arrays, mip chains and MSAA. Required render
  usage must be present. Windows CompareObjectHandles is required.
- Linux `flCreateChannelFromDmabufs`: borrowed plane descriptors. Duplicates FDs,
  checks geometry/layout bounds and GBM import compatibility. Slots sharing any
  backing object are rejected, including suballocated images in one DMA-BUF.
  Pool layouts use the existing common fourcc/modifier wire contract. CPU mapping
  requires LINEAR; explicit rendering requires a compatible GBM import. Original
  allocation usage is unknown, not fabricated from import flags.
- Android `flCreateChannelFromHardwareBuffers` and
  `flCreateChannelFromSocketFdWithHardwareBuffers`: retain independent AHB refs,
  validate native geometry/format/requested usage and distinct allocation IDs.
  Named and broker-socket APIs are provided. The socket variant consumes its FD
  on every path. API31+ allocation IDs are required for this optional path;
  legacy allocation remains API29-compatible. Different wrappers of the same
  AHB must not be mistaken for independent slots.

## Lifetime and scope

Input native references are borrowed during factory calls and remain caller-owned
on failure. Successful adoption retains its own references, allowing callers to
close/release the originals. The external owner must stop independent writers and
reuse storage ONLY under the ring's acquire/submit/consumed protocol. Do not bind
one allocation into independent writable rings. Keeping a native reference alive
prevents deallocation; it does not establish permission to overwrite.

Geometry requests do not replace external storage. Query the actual unchanged
configuration, or retire the old channel safely and create a new external ring.
All outstanding GPU access must finish before close/reconfiguration under the
existing API contract. Pending native references do not solve downstream reader accounting; use the
implemented generation-safe reader leases/completion contract in READ_LEASES.md.

The caller must deliver authorized native handles into the consumer's process
first. Factories do not broker arbitrary handles from another process, negotiate
remote allocator roles or make decoder-array textures compatible. Those belong
to runtime negotiation/GStreamer/relay integration tasks. Adopted-resource queries
advertise EXTERNAL_POOL locally; legacy wire peers are not sent new capability
claims. RELAY remains unadvertised. No physical GPU-residency guarantee is made.

[Local adoption and regression evidence](m5a/external-pools-20261101/README.md).

## Windows allocation identity limit discovered during native Gst integration

The duplicate-handle rejection compares NT object identities. Independent
CreateSharedHandle exports can refer to one GPU allocation while having different
NT wrapper identities. Therefore that rejection is not proof that arbitrary
external handles describe distinct backing storage. Callers must supply genuinely
distinct allocations from a trusted allocator; remote strict negotiation must bind
allocator-issued allocation identities, not infer them from distinct handle values
or NT wrappers. The canonical-export binding adapter is documented in GST_NATIVE_MEMORY.md;
arbitrary independent exports still cannot establish distinct storage. The native Gst source test uses a controlled alias oracle (test only), not
handle inequality as zero-copy evidence.

## Producer-side native references

`flRetainBufferReference` pins an acquired slot while a local wrapper retains its
allocation. The token survives publication or cancellation. Even after remote
consumer completion, that slot cannot be reacquired until every local token is
released with `flReleaseBufferReference`. Registration follows serialized producer
calls; release may run on a finalizer thread, never concurrently with channel close.
All local CPU/GPU accesses must finish before release. Pins do not permit writes
after publication, supply GPU completion, or keep the channel alive by themselves.
The wrapper owner must retain the channel. A maximum 256 references bounds storage;
geometry requests reject while any reference remains. Legacy callers need no pins.

[Native ownership tests](m5a/producer-references-20261101/RESULTS.md).

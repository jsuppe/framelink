# Shared-CPU coordinator pool preflight

Internal `fl::validateSharedCpuPool` is an integration prerequisite, not a new
public channel or a transport capability. Legacy channel/session wire and defaults
remain unchanged. The coordinator must explicitly agree the shared-CPU extension,
validate its actual session selection against both offers, and authenticate the
control peer before using this preflight. It must still bind each received handle
to the descriptor, verify OS rights/size/seals, and establish live frame leases.
Logical allocation IDs cannot prove that two handles refer to distinct OS objects.

The preflight binds every descriptor to the validated session's video format,
geometry and generation, rejects duplicate logical allocation IDs, and enforces
2..16 images, a 1GiB aggregate pool bound, and multi-frame unread depth plus two
ownership slots. Individually valid descriptors cannot bypass these pool checks.
Plane strides may differ between slots; each descriptor independently validates.
Audio-only sessions cannot accept a video pool. Non-increasing session replacement
is still the existing coordinator's responsibility, not this stateless helper.

The selected arrangement is producer CPU_WRITE and consumer CPU_READ, with only
explicit COPY operations at either endpoint. Both endpoints must permit the union
of copies. Strict native resource or GPU usage requirements reject; preferences
remain diagnostic. No native GPU interoperability is advertised. This helper
cannot detect an undeclared memcpy: the eventual adapter must report actual work.

Preflight failure preserves decisions. Once policy evaluation is reached, OK and
policy INCOMPATIBLE publish both endpoint diagnostics, following the existing API.

Validation: Linux 6/6 affected tests; Windows 5/5; Android arm64 pool regression
passes and arm64/x86_64 builds pass. Evidence in `shared-cpu-pool/`; remote Windows
CMake baseline was preserved with only the new test registration inserted.

**Still open:** extension handshake and channel dispatch, imported pool identity,
live read/write ownership and reconnect, Windows allocator, GStreamer/coordinator
adoption, A/V and target-rate qualification. This is not M6.8b/c completion.

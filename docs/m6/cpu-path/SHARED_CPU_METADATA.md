# Strictly negotiated CPU frame metadata

New `flSharedCpuOpenConsumerFdEx` / `flSharedCpuOpenProducerFdEx` accept
`FL_SHARED_CPU_FRAME_METADATA`. Both peers must select it; mismatched flags close
the handshake rather than downgrade. Existing factories select zero and preserve
the previous wire behavior. Unknown API flags reject while consuming the FD.
The explicit FCH1 handshake word at offset 16 is exactly 0 or 2; other reserved
bytes remain zero. An older implementation rejects metadata mode, not weakens it.

Metadata mode requires `flSharedCpuSubmitWithMetadata`. Ready uses type 14 / FCM1,
exactly 80 bytes: original 40-byte identity/PTS header followed by explicit LE
version, flags, DTS, duration, media generation and reserved words. Release messages
remain unchanged. Both send and receive validate the existing frame-metadata
contract: known flags/version, valid DTS/duration presence and ranges, nonzero
media generation, zero reserved words. This independent media generation is not
silently replaced by allocation generation. Legacy mode rejects nonzero metadata;
metadata mode rejects an absent/invalid metadata block. No fields are discarded.

`flSharedCpuReadMetadata` reads the retained read lease without consuming it and
remains valid after channel close. Legacy frames return zero metadata. Wrong-kind
calls reject. Public NV12 tests preserve DTS, duration, independent media generation
77 and a discontinuity on frame 4; BGRA still exercises the unchanged basic mode.

Linux 13/13 affected tests passed. Android public-channel, hostile-control and C11
runtime tests passed; both Android ABI builds passed. Nine hostile metadata cases
reject version/flags, negative DTS, oversized duration, zero generation, reserved
words, presence mismatch and truncated payload, preserving caller output. One
valid explicit metadata packet passes. A mismatched metadata-mode peer is rejected
on both ends. Evidence: `shared-cpu-metadata/`.

Ordered channel EOS is now available independently (SHARED_CPU_EOS.md).
Coordinator/broker selection and GStreamer/A/V-bin integration remain pending.
These media-field checks do not qualify A/V sync, scheduling or target rates.

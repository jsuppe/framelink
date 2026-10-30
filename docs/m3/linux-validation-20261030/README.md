# Native Linux M3 evidence

Source: `3074e2a`; expanded harness and acceptance documentation: `629a6f2`.
Release build, GCC 13.3, GStreamer 1.24.2, RTX 3090 native FrameLink host.

- CTest: 33/33 passed.
- Independent A/V process matrix: 10/10 passed.
- Each finite session: 50 verified video frames and 50 verified PCM blocks.
- Headless verification only; no speaker/display synchronization claim.

Each case contains original peer commands and output plus a structured result.
Expected errors in fault/crash cases are deliberate acceptance checks, not
unexpected failures. Private rendezvous files and authentication material are
excluded. Session instance IDs are non-secret identities.

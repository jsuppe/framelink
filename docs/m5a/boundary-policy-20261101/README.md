# M5a.6 boundary policy validation — 2026-11-01

Portable bounded agreement helpers; not runtime GStreamer negotiation.

- Linux: memory_boundary, memory_contract, C11 ABI and four actual-resource tests **7/7**.
- Windows Caspar: same **7/7**, Release build.
- Android TCL A06B4AABFB07983: same **7/7**, arm64 runtime.
- Android API29 arm64 and x86_64 builds passed; no x86 runtime claim.

Boundary negatives: producer copy/upload rejected by strict readers; reader
readback rejected without every participant's consent; endpoint usage cannot be
borrowed from another; conflicting resource masks; invalid/version inputs leave
outputs untouched; count bounds and sixteen-participant handling.
Explicit consent passes; unmet preferences do not waive hard requirements.

Commands: build fl_memory_boundary fl_memory_contract fl_memory_c_api
fl_memory_resource; desktop ctest -R '^memory_' --output-on-failure;
Android push and execute each binary, resource formats1–4. Logs retained here.
The first Android deployment command incorrectly assumed a shared library;
Android uses the static core library linked into executables. Corrected deployment
ran all seven tests successfully; no product change was needed.

No throughput, GPU-driver/fence, physical output, remote agreement or legacy-peer
runtime qualification is claimed here. Task7 must bind policy to the authenticated
session, actual allocation/device/layout/generation and GStreamer memory path.

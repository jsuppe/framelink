# Android native AHB sink and whole-buffer sharing repair

Android tablet: RGBA/RGB10 sink roundtrips plus missing-readiness rejection;
native source, pool policy and timestamp suites pass. Windows affected5/5,
Linux3/3, Android arm64/x86_64 API29 builds pass (x86_64 build only).

AHB allocation IDs match, pixels and frame metadata exact; two reserved images
bound capacity; pending readiness eventfd blocks delivery until signal; FD
ownership is duplicated; repeated/invalid readiness rejects; retained native
memory survives shutdown. Implicit maps, deep pixel copy and partial memory
shares reject. Content is generated via explicit AHB CPU mapping, and readiness
uses a test eventfd: this is NOT a new real-GPU/native-sync-file qualification.

Initial run crashed in gst_base_src_loop gstbasesrc.c:3070 (GStreamer1.24.13),
marking DISCONT after gst_buffer_make_writable returned NULL. NO_SHARE forced a
pixel copy which the opaque allocator rejects. Whole GstBuffer copies now share
the same memory reference without copying bytes; source and producer regressions
verify identity. Deep copies and partial subviews remain unsupported. The actual
retained-buffer appsrc path that crashed now passes without altering the fixture.

Explicit application native adapter only; Android ALLOCATION and generic decoder
integration, authenticated remote memory policy and full-rate GPU qualification
remain pending. Before abandoning unpublished buffers caller completes GPU work.

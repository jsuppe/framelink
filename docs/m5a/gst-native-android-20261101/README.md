# M5a.7c Android exact-RGB native source

On TCL A06B4AABFB07983 (API35), native RGBA and RGB10 cases passed:
original AHB allocation ID, exact pixels/PTS, bounded two-slot reuse, memory
retained across source shutdown. Implicit GstMemory CPU mapping rejects.
Opaque YUV-as-NV12 request rejects; no claimed NV12 native layout/conversion.
Existing Android pool policy suite passed. API29 arm64/x86_64 builds passed.
Linux native regression passed; Windows native/copy-negative **2/2** passed.

This custom allocator is not a stock GStreamer AHB interoperability claim.
A custom native consumer retains GstMemory through GPU completion and accesses
its borrowed AHB via gst/framelink_native.h. Tests explicitly CPU-lock the AHB
only for verification; the source performs no pixel lock/copy.

The shared source loop now retries outstanding reader retirement and reports
retirement errors rather than silently ignoring a failed completion delivery.
Native sink, remote negotiation, GPU consumer completion qualification, adopted
channels and variable metadata are still incomplete task7 work.

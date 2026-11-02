# Transport/presentation separation

M5a.4: Linux, Windows and Android each pass three cases: two-hop A/V with audio
37ms behind, audio37ms ahead, and intermediate video sync=true negative control.
Each positive verifies 100 pairs, exact PTS/running-time and relative offset,
constant-rate20ms duration, video identity and PCM samples. Media starts120seconds
in the future but each frame arrives within the2second fixture deadline. One
frame is sent/pulled at a time despite a four-image transport pool: no pool-fill
gate. These loose deadlines distinguish presentation waiting, not performance.
Four existing Linux M4 transport regressions also pass.

The negative proves the video reached the intermediate sink pad, confirms audio
arrives, and sees no final video while intermediate sync=true waits for future
media time. Initial strengthening exposed fixture appsink preroll coupling;
verification sinks now use async=false so a missing video does not prevent the
audio observation. This is a fixture repair, not a production pacing change.
Android target had previously been excluded by CMake; now builds with existing
static GStreamer registration and was executed on the tablet.

Production audit: framelinksink already defaults sync=false; preserve-timestamps
normalizes TIME segments to session running-time; framelinksrc preserves that
running-time when opted in. Core pools do not wait to fill. No production change
was necessary for this gate. Legacy arrival/submit timestamp defaults are unchanged.

Limits: ordinary Gst boundaries copy pixels and GDP copies PCM. Android legacy
CPU video has substantial differential arrival delay; exact PTS preservation is
not simultaneous arrival, full FPS or final physical lip sync. Tests use one
process with distinct pipelines, unlike task3's separate-process GPU fixture.
General variable-duration/DTS/segment-generation metadata and native GPU Gst
integration remain task7; current video source derives duration from configured
FPS. Do not advertise arbitrary metadata preservation based on this fixed-rate test.

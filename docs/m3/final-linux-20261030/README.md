# Final Linux M3 qualification

Runtime includes review lifetime fix9d1eed6 (parentb62bdbd), MSVC portability cast,
CLIhelp and CPUauth/rendezvous gates. Full Linux38/38 passed including the10case
independent-process acceptance matrix. Retained ctest.log records exact names.
Additional NV12 sample checks verified60video/PCM pairs each at44.1kHzmono59fps
(nonintegralsamples/frame) and48kHzstereo60fps. These are applicationdata/timestamp
checks, notphysicalaudio/scanout ordevicemasterdrift qualification.
Optional--play virtualdisplay/silentaudiosink evidence is in../playback-smoke-20261030.

# Interactive Windows M5 demonstration

Run from the logged-in user's PowerShell (not an SSH session if visible browser
and the user's audio session are desired):

```powershell
$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
python test/m5_visible_demo.py --build build/Release --renderer C:/src/vkrender/build/Release/vkrenderer.exe --output visible-demo --loops 30
```

The helper opens `http://127.0.0.1:18765` in the default browser, decodes a real
10-second MJPEG/PCM AVI into the external producer, and starts a separate native
renderer with WASAPI/default audio device. It loops **finite** sessions (30 loops
is approximately 5 minutes, with session startup gaps). This is not an unbounded
live protocol. The tone is 440 Hz at amplitude 0.01; device volume is unchanged.
Video is BGRA. Renderer control uses isolated port18766. Existing demos are not
stopped. Choose different `--port`/`--renderer-port` if occupied.

The browser is a JPEG preview, not native video scanout; its refresh rate and
additional JPEG latency do not qualify presentation smoothness or physical A/V
sync. Audio output uses the independent renderer, not the browser.

Stop with the page's **Stop this demo** button, or create `visible-demo/STOP`.
Remove that marker before restarting. The helper terminates only its own child
processes; logs and final status remain in the output directory. Each session
has a new private rendezvous directory. No scheduled task is created by the
Python helper itself.

## Caspar launch 2026-10-30

Interactive task `FrameLink-M5-Visible-Demo` runs90 loops (approximately15 minutes) and launches the helper from
`C:/Users/jonsu/dev/framelink-m5-pipeline/visible-demo.ps1`. Restart using
`Start-ScheduledTask -TaskName FrameLink-M5-Visible-Demo` after it has stopped.
Existing RGB/NV12 demo PID18580 was inspected and preserved. New demo producer,
renderer, and browser ran in interactive SessionId1. HTTP status reported real
audio-master selection; two JPEG captures had different SHA256s and displayed
moving ball textures on cubes. Browser title and desktop screenshot confirmed
that the page opened. This is not an audible/optical observation.

Initial launch correctly hit the producer's whole-process15s deadline when
using a60s clip. The helper was corrected to10s finite clips and60s producer
timeout (renderer receiver also has a15s bounded session window); this failed launch is not counted as a successful endurance test.

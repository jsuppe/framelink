#include "framelink_session.h"
#include <stdio.h>
_Static_assert(FL_SESSION_WIRE_SIZE == 160, "wire size");
int main(void) {
    flSessionConfig c = {0}, decoded = {0};
    unsigned char wire[FL_SESSION_WIRE_SIZE];
    int64_t offset = -1;
    c.version=FL_SESSION_VERSION; c.sessionId.bytes[0]=1; c.clockId.bytes[0]=2;
    c.generation=1; c.videoTrackId=1; c.audioTrackId=2;
    c.video.format=FL_FORMAT_NV12; c.video.width=1920; c.video.height=1080;
    c.video.fpsNum=60; c.video.fpsDen=1;
    c.audio.pcm=FL_PCM_F32; c.audio.rate=48000; c.audio.layout=FL_LAYOUT_STEREO;
    c.epochNs=1000000000; c.videoQueueFrames=3; c.audioQueueMs=40;
    c.connectTimeoutMs=1000; c.idleTimeoutMs=5000;
    if (flSessionValidate(&c)!=FL_SESSION_OK ||
        flSessionEncode(&c,wire,sizeof wire)!=FL_SESSION_OK ||
        flSessionDecode(wire,sizeof wire,&decoded)!=FL_SESSION_OK ||
        decoded.audio.rate!=48000 || decoded.generation!=1 ||
        flSessionSampleOffset(48000,48000,&offset)!=FL_SESSION_OK ||
        offset!=1000000000) return 1;
    puts("MSVC C11 header, DLL linkage, config codec and sample timing passed");
    return 0;
}

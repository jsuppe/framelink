/* This must compile as C11, not C++, and link the exported C ABI. */
#include "framelink_session.h"
#include <stdint.h>
int main(void) {
    int64_t ns = -1;
    flSessionCapabilities a = {0}, b = {0};
    flVideoFormat video = {0};
    flAudioFormat audio = {0};
    a.version = b.version = FL_SESSION_VERSION;
    a.videoCount = b.videoCount = a.audioCount = b.audioCount = 1;
    a.video[0] = (flVideoFormat){FL_FORMAT_RGBA8, 16, 16, 30, 1};
    b.video[0] = a.video[0];
    a.audio[0] = (flAudioFormat){FL_PCM_S16, 48000, FL_LAYOUT_STEREO};
    b.audio[0] = a.audio[0];
    if (flSessionNegotiate(&a, &b, &video, &audio) != FL_SESSION_OK) return 1;
    if (video.width != 16 || audio.rate != 48000) return 2;
    if (flSessionSampleOffset(48000, 48000, &ns) != FL_SESSION_OK || ns != 1000000000) return 3;
    if (flSessionValidateSelection(NULL, &a, &b) != FL_SESSION_INVALID) return 4;
    return 0;
}

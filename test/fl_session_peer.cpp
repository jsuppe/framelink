// Contract-only fixture: independently launched proposer and acceptor exchange
// config/ack over harness pipes. No media, discovery, authentication or M3 IPC.
#include "framelink_session.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static flSessionConfig initial() {
    flSessionConfig c{};
    c.version=FL_SESSION_VERSION;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;
    c.generation=1;c.videoTrackId=1;c.audioTrackId=2;
    c.video={FL_FORMAT_NV12,1920,1080,60,1};
    c.audio={FL_PCM_F32,48000,FL_LAYOUT_STEREO};c.epochNs=1000000000;
    c.videoQueueFrames=3;c.audioQueueMs=40;c.connectTimeoutMs=1000;c.idleTimeoutMs=5000;
    return c;
}
static flSessionCapabilities offer() {
    const auto c=initial();flSessionCapabilities caps{};
    caps.version=FL_SESSION_VERSION;caps.videoCount=1;caps.video[0]=c.video;
    caps.audioCount=2;caps.audio[0]={FL_PCM_S16,44100,FL_LAYOUT_MONO};caps.audio[1]=c.audio;
    return caps;
}
int main(int argc,char**argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc!=3)return 1;
    const auto baseline=initial();auto config=baseline;config.generation=2;
    auto producer=offer(),consumer=offer();
    unsigned char wire[FL_SESSION_WIRE_SIZE]{};
    if(!std::strcmp(argv[1],"propose")) {
        if(flSessionNegotiate(&producer,&consumer,&config.video,&config.audio)!=FL_SESSION_OK)return 1;
        if(!std::strcmp(argv[2],"unoffered"))config.audio.rate=96000;
        else if(!std::strcmp(argv[2],"stale"))config.generation=1;
        else if(!std::strcmp(argv[2],"changed-track"))config.audioTrackId=3;
        else if(!std::strcmp(argv[2],"unknown-clock"))config.clockId.bytes[0]=9;
        if(flSessionEncode(&config,wire,sizeof wire)!=FL_SESSION_OK)return 1;
        if(!std::strcmp(argv[2],"malformed"))wire[159]=1;
        if(std::fwrite(wire,1,sizeof wire,stdout)!=sizeof wire||std::fflush(stdout))return 1;
        unsigned char ack[FL_SESSION_WIRE_SIZE]{};
        const auto received=std::fread(ack,1,sizeof ack,stdin);
        if(received!=sizeof ack)return 2;
        // A peer cannot silently substitute another configuration in its ack.
        if(std::memcmp(wire,ack,sizeof wire))return 3;
        std::fprintf(stderr,"proposer: identical configuration acknowledged\n");return 0;
    }
    if(std::strcmp(argv[1],"accept"))return 1;
    if(std::fread(wire,1,sizeof wire,stdin)!=sizeof wire)return 2;
    auto result=flSessionDecode(wire,sizeof wire,&config);
    if(result==FL_SESSION_OK)result=flSessionValidateSelection(&config,&producer,&consumer);
    if(result==FL_SESSION_OK)result=flSessionValidateReplacement(&baseline,&config);
    flClockMapping mapping{};mapping.sourceClockId=baseline.clockId;mapping.targetClockId=baseline.clockId;
    int64_t mapped=0;
    if(result==FL_SESSION_OK)result=flSessionMapPts(&config,&mapping,0,123,&mapped);
    if(result!=FL_SESSION_OK){std::fprintf(stderr,"acceptor: rejected configuration (%d)\n",int(result));return 2;}
    if(mapped!=config.epochNs+123)return 1;
    unsigned char ack[FL_SESSION_WIRE_SIZE]{};
    if(flSessionEncode(&config,ack,sizeof ack)!=FL_SESSION_OK)return 1;
    if(!std::strcmp(argv[2],"changed-ack"))ack[40]++;
    if(std::fwrite(ack,1,sizeof ack,stdout)!=sizeof ack||std::fflush(stdout))return 1;
    std::fprintf(stderr,"acceptor: selected formats, generation and clock accepted\n");return 0;
}

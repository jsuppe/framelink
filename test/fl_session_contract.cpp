#include "framelink_session.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
static flSessionConfig config() {
 flSessionConfig c{}; c.version=1;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;
 c.videoTrackId=1;c.audioTrackId=2;c.video={FL_FORMAT_NV12,1920,1080,60,1};c.audio={FL_PCM_F32,48000,FL_LAYOUT_STEREO};
 c.epochNs=1000000000;c.videoQueueFrames=3;c.audioQueueMs=40;c.connectTimeoutMs=1000;c.idleTimeoutMs=5000;return c;
}
int main() {
 auto c=config();CHECK(flSessionValidate(&c)==FL_SESSION_OK);
 flSessionCapabilities p{},q{};p.version=q.version=1;p.videoCount=q.videoCount=1;p.video[0]=q.video[0]=c.video;
 p.audioCount=q.audioCount=2;p.audio[0]=q.audio[0]={FL_PCM_S16,44100,FL_LAYOUT_MONO};p.audio[1]=q.audio[1]=c.audio;
 flVideoFormat v{};flAudioFormat a{};
 CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_OK);CHECK(a.rate==48000&&a.pcm==FL_PCM_F32);
 q.audioCount=1;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_OK);CHECK(a.rate==44100);
 q.audio[0].layout=FL_LAYOUT_STEREO;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_INCOMPATIBLE);
 q=p;q.video[0].fpsNum=120;q.video[0].fpsDen=2;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_OK);
 q.video[0].width=1280;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_INCOMPATIBLE);
 q=p;q.audioCount=9;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_INVALID);
 q=p;q.audio[1].rate=UINT32_MAX;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_INVALID);
 q=p;q.version=2;CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_VERSION_ERROR);
 CHECK(flSessionNegotiate(nullptr,&q,&v,&a)==FL_SESSION_INVALID);
 auto next=c;CHECK(flSessionValidateReplacement(&c,&next)==FL_SESSION_STALE);
 next.generation++;CHECK(flSessionValidateReplacement(&c,&next)==FL_SESSION_OK);
 next.sessionId.bytes[1]=1;CHECK(flSessionValidateReplacement(&c,&next)==FL_SESSION_INVALID);
 next=c;next.video.width=1919;CHECK(flSessionValidate(&next)==FL_SESSION_INVALID);
 next=c;next.audioTrackId=1;CHECK(flSessionValidate(&next)==FL_SESSION_INVALID);
 next=c;next.audioQueueMs=2001;CHECK(flSessionValidate(&next)==FL_SESSION_INVALID);
 next=c;next.generation=0;CHECK(flSessionValidate(&next)==FL_SESSION_INVALID);
 next=c;next.clockId={};CHECK(flSessionValidate(&next)==FL_SESSION_INVALID);
 unsigned char wire[FL_SESSION_WIRE_SIZE];CHECK(flSessionEncode(&c,wire,sizeof(wire))==FL_SESSION_OK);
 CHECK(wire[0]=='F'&&wire[1]=='V'&&wire[2]=='S'&&wire[3]=='1');CHECK(wire[48]==1&&wire[49]==0);
 flSessionConfig decoded{};CHECK(flSessionDecode(wire,sizeof(wire),&decoded)==FL_SESSION_OK);
 CHECK(decoded.generation==c.generation&&decoded.epochNs==c.epochNs&&decoded.audio.rate==48000&&decoded.video.width==1920);
 unsigned char again[FL_SESSION_WIRE_SIZE];CHECK(flSessionEncode(&decoded,again,sizeof(again))==FL_SESSION_OK);CHECK(!std::memcmp(wire,again,sizeof(wire)));
 for(size_t n=0;n<sizeof(wire);n++) CHECK(flSessionDecode(wire,n,&decoded)==FL_SESSION_INVALID);
 CHECK(flSessionDecode(wire,sizeof(wire)+1,&decoded)==FL_SESSION_INVALID);
 wire[159]=1;decoded.generation=999;CHECK(flSessionDecode(wire,sizeof(wire),&decoded)==FL_SESSION_INVALID);CHECK(decoded.generation==999);wire[159]=0;
 wire[4]=2;CHECK(flSessionDecode(wire,sizeof(wire),&decoded)==FL_SESSION_VERSION_ERROR);wire[4]=1;
 wire[0]=0;CHECK(flSessionDecode(wire,sizeof(wire),&decoded)==FL_SESSION_INVALID);wire[0]='F';
 // Epoch occupies bytes 88..95; high bit rejects uint64 -> int64 wrap.
 wire[95]=0x80;CHECK(flSessionDecode(wire,sizeof(wire),&decoded)==FL_SESSION_TIME_RANGE);
 flClockMapping m{};m.sourceClockId=c.clockId;m.targetClockId.bytes[0]=3;m.sourceAnchorNs=1000000000;m.targetAnchorNs=2000000000;m.uncertaintyNs=100;
 int64_t result=0;CHECK(flSessionMapPts(&c,&m,100,500,&result)==FL_SESSION_OK);CHECK(result==2000000500);
 CHECK(flSessionMapPts(&c,&m,99,500,&result)==FL_SESSION_INCOMPATIBLE);
 CHECK(flSessionMapPts(&c,&m,100,-1,&result)==FL_SESSION_INVALID);
 m.sourceClockId.bytes[0]=9;CHECK(flSessionMapPts(&c,&m,100,500,&result)==FL_SESSION_INVALID);m.sourceClockId=c.clockId;
 m.targetAnchorNs=INT64_MAX;CHECK(flSessionMapPts(&c,&m,100,500,&result)==FL_SESSION_TIME_RANGE);
 m.targetAnchorNs=0;m.sourceAnchorNs=2000000000;CHECK(flSessionMapPts(&c,&m,100,0,&result)==FL_SESSION_TIME_RANGE);
 m.targetClockId=c.clockId;CHECK(flSessionMapPts(&c,&m,100,0,&result)==FL_SESSION_INVALID);
 m.targetAnchorNs=m.sourceAnchorNs;m.uncertaintyNs=0;CHECK(flSessionMapPts(&c,&m,0,500,&result)==FL_SESSION_OK);CHECK(result==c.epochNs+500);
 CHECK(flSessionMapPts(&c,&m,0,INT64_MAX,&result)==FL_SESSION_TIME_RANGE);
 CHECK(flSessionSampleOffset(48000,48000,&result)==FL_SESSION_OK&&result==1000000000);
 CHECK(flSessionSampleOffset(1,44100,&result)==FL_SESSION_OK&&result==22675);
 CHECK(flSessionSampleOffset(UINT64_MAX,8000,&result)==FL_SESSION_TIME_RANGE);
 CHECK(flSessionSampleOffset(1,0,&result)==FL_SESSION_INVALID);
 CHECK(flSessionSampleOffset(1,UINT32_MAX,&result)==FL_SESSION_INVALID);
 // Demonstrate exact cumulative timing, avoiding accumulated block-rounding drift.
 int64_t end,start;CHECK(flSessionSampleOffset(44100,44100,&end)==FL_SESSION_OK);CHECK(flSessionSampleOffset(44099,44100,&start)==FL_SESSION_OK);CHECK(end-start==22676);
 std::puts("session contract: negotiation, codec, generation and clock bounds passed");
}

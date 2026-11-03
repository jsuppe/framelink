#include "framelink_session.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line%d %s\n",__LINE__,#x);std::exit(1); } } while(0)
static flSessionConfig config(unsigned mask) {
 flSessionConfig c{};c.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;
 c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;
 c.connectTimeoutMs=500;c.idleTimeoutMs=1000;c.epochNs=37000000;
 if(mask&1){c.videoTrackId=1;c.video={FL_FORMAT_RGBA8,16,16,60,1};c.videoQueueFrames=2;}
 if(mask&2){c.audioTrackId=2;c.audio={FL_PCM_F32,44100,FL_LAYOUT_STEREO};c.audioQueueMs=80;}
 return c;
}
static flSessionCapabilities offer(const flSessionConfig& c) {
 flSessionCapabilities o{};o.version=c.version;
 if(c.videoTrackId){o.videoCount=1;o.video[0]=c.video;}
 if(c.audioTrackId){o.audioCount=1;o.audio[0]=c.audio;}
 return o;
}
int main() {
 for(unsigned mask=1;mask<=3;mask++) {
  auto c=config(mask);auto p=offer(c),q=p;
  CHECK(flSessionValidate(&c)==FL_SESSION_OK);
  flVideoFormat v{};flAudioFormat a{};
  CHECK(flSessionNegotiate(&p,&q,&v,&a)==FL_SESSION_OK);
  CHECK(v.width==c.video.width&&a.rate==c.audio.rate);
  CHECK(flSessionValidateSelection(&c,&p,&q)==FL_SESSION_OK);
  unsigned char wire[FL_SESSION_WIRE_SIZE],again[FL_SESSION_WIRE_SIZE];
  CHECK(flSessionEncode(&c,wire,sizeof wire)==FL_SESSION_OK);CHECK(wire[3]=='2'&&wire[4]==2);
  flSessionConfig out{};
  CHECK(flSessionDecode(wire,sizeof wire,&out)==FL_SESSION_OK);
  CHECK(out.videoTrackId==c.videoTrackId&&out.audioTrackId==c.audioTrackId&&out.epochNs==37000000);
  CHECK(flSessionEncode(&out,again,sizeof again)==FL_SESSION_OK);CHECK(!memcmp(wire,again,sizeof wire));
  for(size_t n=0;n<sizeof wire;n++)CHECK(flSessionDecode(wire,n,&out)==FL_SESSION_INVALID);
  for(size_t n=112;n<sizeof wire;n++) {
   wire[n]=1;out.generation=999;
   CHECK(flSessionDecode(wire,sizeof wire,&out)==FL_SESSION_INVALID);CHECK(out.generation==999);wire[n]=0;
  }
  wire[3]='1';CHECK(flSessionDecode(wire,sizeof wire,&out)==FL_SESSION_VERSION_ERROR);wire[3]='2';
  wire[4]=1;CHECK(flSessionDecode(wire,sizeof wire,&out)==FL_SESSION_VERSION_ERROR);wire[4]=2;
  auto next=c;next.generation++;
  CHECK(flSessionValidateReplacement(&c,&next)==FL_SESSION_OK);
  CHECK(flSessionValidateReplacement(&next,&c)==FL_SESSION_STALE);
  for(unsigned other=1;other<=3;other++)if(other!=mask){
   auto changed=config(other);changed.generation=2;auto otherOffer=offer(changed);
   CHECK(flSessionValidateReplacement(&c,&changed)==FL_SESSION_INVALID);
   v.width=777;a.rate=777;
   CHECK(flSessionNegotiate(&p,&otherOffer,&v,&a)==FL_SESSION_INCOMPATIBLE);
   CHECK(v.width==777&&a.rate==777);
   CHECK(flSessionValidateSelection(&c,&p,&otherOffer)==FL_SESSION_INCOMPATIBLE);
  }
  auto legacy=config(3);legacy.version=FL_SESSION_VERSION;auto old=offer(legacy);
  CHECK(flSessionNegotiate(&p,&old,&v,&a)==FL_SESSION_VERSION_ERROR);
  CHECK(flSessionValidateReplacement(&c,&legacy)==FL_SESSION_VERSION_ERROR);
  auto invalid=c;invalid.version=999;CHECK(flSessionValidate(&invalid)==FL_SESSION_VERSION_ERROR);
  if(!(mask&1)){
   invalid=c;invalid.video.width=16;CHECK(flSessionValidate(&invalid)==FL_SESSION_INVALID);
   invalid=c;invalid.videoQueueFrames=2;CHECK(flSessionValidate(&invalid)==FL_SESSION_INVALID);
  }
  if(!(mask&2)){
   invalid=c;invalid.audio.rate=48000;CHECK(flSessionValidate(&invalid)==FL_SESSION_INVALID);
   invalid=c;invalid.audioQueueMs=80;CHECK(flSessionValidate(&invalid)==FL_SESSION_INVALID);
  }
  // Old API callers still require both tracks; version2 must be explicit.
  if(mask!=3){invalid=c;invalid.version=1;CHECK(flSessionValidate(&invalid)==FL_SESSION_INVALID);}
 }
 auto none=config(0);CHECK(flSessionValidate(&none)==FL_SESSION_INVALID);
 auto empty=offer(none);flVideoFormat v{};flAudioFormat a{};
 CHECK(flSessionNegotiate(&empty,&empty,&v,&a)==FL_SESSION_INVALID);
 puts("optional tracks: audio/video/AV codec, absent-field, downgrade and replacement negatives passed");
}

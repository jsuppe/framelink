#define FL_EXPORTS
#include "framelink_session.h"
#include <cstring>
#include <limits>
#include <initializer_list>
namespace {
bool idValid(const flSessionId& id) { uint8_t v=0; for(auto b:id.bytes) v|=b; return v!=0; }
bool same(const flSessionId& a,const flSessionId& b) { return !std::memcmp(a.bytes,b.bytes,16); }
bool audioValid(const flAudioFormat& a) { return (a.pcm==FL_PCM_S16||a.pcm==FL_PCM_F32) && a.rate>=8000 && a.rate<=192000 && (a.layout==FL_LAYOUT_MONO||a.layout==FL_LAYOUT_STEREO); }
bool videoValid(const flVideoFormat& v) { return v.format>=FL_FORMAT_BGRA8 && v.format<=FL_FORMAT_RGBA8 && v.width && v.height && v.width<=16384 && v.height<=16384 && v.fpsNum && v.fpsNum<=1000000 && v.fpsDen && v.fpsDen<=1000000 && (v.format!=FL_FORMAT_NV12 || (!(v.width&1)&&!(v.height&1))); }
bool eq(const flAudioFormat&a,const flAudioFormat&b) { return a.pcm==b.pcm&&a.rate==b.rate&&a.layout==b.layout; }
bool eq(const flVideoFormat&a,const flVideoFormat&b) { return a.format==b.format&&a.width==b.width&&a.height==b.height&&uint64_t(a.fpsNum)*b.fpsDen==uint64_t(b.fpsNum)*a.fpsDen; }
flSessionResult caps(const flSessionCapabilities* c) {
 if(!c) return FL_SESSION_INVALID;
 if(c->version!=FL_SESSION_VERSION) return FL_SESSION_VERSION_ERROR;
 if(!c->videoCount||c->videoCount>FL_SESSION_MAX_FORMATS||!c->audioCount||c->audioCount>FL_SESSION_MAX_FORMATS) return FL_SESSION_INVALID;
 for(uint32_t i=0;i<c->videoCount;i++) if(!videoValid(c->video[i])) return FL_SESSION_INVALID;
 for(uint32_t i=0;i<c->audioCount;i++) if(!audioValid(c->audio[i])) return FL_SESSION_INVALID;
 return FL_SESSION_OK;
}
bool add(int64_t a,int64_t b,int64_t& r) { if((b>0&&a>INT64_MAX-b)||(b<0&&a<INT64_MIN-b)) return false; r=a+b; return true; }
void put(uint8_t*& p,uint64_t v,unsigned n) { for(unsigned i=0;i<n;i++) { *p++=uint8_t(v); v>>=8; } }
uint64_t get(const uint8_t*& p,unsigned n) { uint64_t v=0; for(unsigned i=0;i<n;i++) v|=uint64_t(*p++)<<(8*i); return v; }
}
extern "C" {
flSessionResult flSessionNegotiate(const flSessionCapabilities* p,const flSessionCapabilities* c,flVideoFormat* v,flAudioFormat* a) {
 if(!v||!a) return FL_SESSION_INVALID;
 auto r=caps(p); if(r!=FL_SESSION_OK) return r; r=caps(c); if(r!=FL_SESSION_OK) return r;
 flVideoFormat chosenV{}; flAudioFormat chosenA{};
 for(uint32_t i=0;i<p->videoCount&&!chosenV.width;i++) for(uint32_t j=0;j<c->videoCount;j++) if(eq(p->video[i],c->video[j])) { chosenV=p->video[i]; break; }
 for(int preferred=1;preferred>=0&&!chosenA.rate;preferred--) for(uint32_t i=0;i<p->audioCount&&!chosenA.rate;i++) {
  if(preferred&&p->audio[i].rate!=48000) continue;
  for(uint32_t j=0;j<c->audioCount;j++) if(eq(p->audio[i],c->audio[j])) { chosenA=p->audio[i]; break; }
 }
 if(!chosenV.width||!chosenA.rate) return FL_SESSION_INCOMPATIBLE;
 *v=chosenV; *a=chosenA; return FL_SESSION_OK;
}
flSessionResult flSessionValidate(const flSessionConfig* c) {
 if(!c) return FL_SESSION_INVALID;
 if(c->version!=FL_SESSION_VERSION) return FL_SESSION_VERSION_ERROR;
 if(!idValid(c->sessionId)||!idValid(c->clockId)||!c->generation||!c->videoTrackId||!c->audioTrackId||c->videoTrackId==c->audioTrackId||!videoValid(c->video)||!audioValid(c->audio)||c->epochNs<0||!c->videoQueueFrames||c->videoQueueFrames>16||!c->audioQueueMs||c->audioQueueMs>2000||!c->connectTimeoutMs||c->connectTimeoutMs>60000||!c->idleTimeoutMs||c->idleTimeoutMs>60000) return FL_SESSION_INVALID;
 return FL_SESSION_OK;
}
flSessionResult flSessionValidateSelection(const flSessionConfig* c,const flSessionCapabilities* p,const flSessionCapabilities* q) {
 auto r=flSessionValidate(c); if(r!=FL_SESSION_OK)return r;
 r=caps(p);if(r!=FL_SESSION_OK)return r;r=caps(q);if(r!=FL_SESSION_OK)return r;
 for(const auto* offer:{p,q}) {
  bool video=false,audio=false;
  for(uint32_t i=0;i<offer->videoCount;i++) video|=eq(c->video,offer->video[i]);
  for(uint32_t i=0;i<offer->audioCount;i++) audio|=eq(c->audio,offer->audio[i]);
  if(!video||!audio)return FL_SESSION_INCOMPATIBLE;
 }
 return FL_SESSION_OK;
}
flSessionResult flSessionValidateReplacement(const flSessionConfig* c,const flSessionConfig* n) {
 auto r=flSessionValidate(c); if(r!=FL_SESSION_OK)return r; r=flSessionValidate(n); if(r!=FL_SESSION_OK)return r;
 if(!same(c->sessionId,n->sessionId)||c->videoTrackId!=n->videoTrackId||c->audioTrackId!=n->audioTrackId)return FL_SESSION_INVALID;
 return n->generation>c->generation ? FL_SESSION_OK:FL_SESSION_STALE;
}
flSessionResult flSessionEncode(const flSessionConfig* c,void* data,size_t size) {
 if(!data||size!=FL_SESSION_WIRE_SIZE)return FL_SESSION_INVALID;
 auto r=flSessionValidate(c);if(r!=FL_SESSION_OK)return r;
 auto p=static_cast<uint8_t*>(data);std::memset(p,0,size);
 put(p,0x31535646,4);put(p,c->version,4);
 std::memcpy(p,c->sessionId.bytes,16);p+=16;std::memcpy(p,c->clockId.bytes,16);p+=16;
 put(p,c->generation,8);put(p,c->videoTrackId,4);put(p,c->audioTrackId,4);
 put(p,c->video.format,4);put(p,c->video.width,4);put(p,c->video.height,4);put(p,c->video.fpsNum,4);put(p,c->video.fpsDen,4);
 put(p,c->audio.pcm,4);put(p,c->audio.rate,4);put(p,c->audio.layout,4);put(p,uint64_t(c->epochNs),8);
 put(p,c->videoQueueFrames,4);put(p,c->audioQueueMs,4);put(p,c->connectTimeoutMs,4);put(p,c->idleTimeoutMs,4);
 return FL_SESSION_OK;
}
flSessionResult flSessionDecode(const void* data,size_t size,flSessionConfig* out) {
 if(!data||!out||size!=FL_SESSION_WIRE_SIZE)return FL_SESSION_INVALID;
 auto p=static_cast<const uint8_t*>(data);auto end=p+size;
 if(get(p,4)!=0x31535646)return FL_SESSION_INVALID;
 flSessionConfig c{};c.version=uint32_t(get(p,4));
 if(c.version!=FL_SESSION_VERSION)return FL_SESSION_VERSION_ERROR;
 std::memcpy(c.sessionId.bytes,p,16);p+=16;std::memcpy(c.clockId.bytes,p,16);p+=16;
 c.generation=get(p,8);c.videoTrackId=uint32_t(get(p,4));c.audioTrackId=uint32_t(get(p,4));
 c.video.format=uint32_t(get(p,4));c.video.width=uint32_t(get(p,4));c.video.height=uint32_t(get(p,4));c.video.fpsNum=uint32_t(get(p,4));c.video.fpsDen=uint32_t(get(p,4));
 c.audio.pcm=uint32_t(get(p,4));c.audio.rate=uint32_t(get(p,4));c.audio.layout=uint32_t(get(p,4));
 uint64_t epoch=get(p,8);if(epoch>uint64_t(INT64_MAX))return FL_SESSION_TIME_RANGE;c.epochNs=int64_t(epoch);
 c.videoQueueFrames=uint32_t(get(p,4));c.audioQueueMs=uint32_t(get(p,4));c.connectTimeoutMs=uint32_t(get(p,4));c.idleTimeoutMs=uint32_t(get(p,4));
 while(p<end)if(*p++)return FL_SESSION_INVALID;
 auto r=flSessionValidate(&c);if(r==FL_SESSION_OK)*out=c;return r;
}
flSessionResult flSessionMapPts(const flSessionConfig* c,const flClockMapping* m,uint64_t maxError,int64_t pts,int64_t* out) {
 auto r=flSessionValidate(c);if(r!=FL_SESSION_OK)return r;
 if(!m||!out||pts<0||!same(c->clockId,m->sourceClockId)||!idValid(m->targetClockId)||m->sourceAnchorNs<0||m->targetAnchorNs<0)return FL_SESSION_INVALID;
 if(m->uncertaintyNs>maxError)return FL_SESSION_INCOMPATIBLE;
 if(same(m->sourceClockId,m->targetClockId)&&(m->sourceAnchorNs!=m->targetAnchorNs||m->uncertaintyNs))return FL_SESSION_INVALID;
 int64_t absolute,result;
 if(!add(c->epochNs,pts,absolute)||!add(absolute,-m->sourceAnchorNs,result)||!add(result,m->targetAnchorNs,result)||result<0)return FL_SESSION_TIME_RANGE;
 *out=result;return FL_SESSION_OK;
}
flSessionResult flSessionSampleOffset(uint64_t index,uint32_t rate,int64_t* out) {
 if(!out||rate<8000||rate>192000)return FL_SESSION_INVALID;
 uint64_t seconds=index/rate,part=(index%rate)*1000000000ull/rate;
 if(seconds>uint64_t(INT64_MAX)/1000000000ull)return FL_SESSION_TIME_RANGE;
 uint64_t result=seconds*1000000000ull+part;if(result>uint64_t(INT64_MAX))return FL_SESSION_TIME_RANGE;
 *out=int64_t(result);return FL_SESSION_OK;
}
}

#include "av_memory_agreement.h"
#include <cstring>
namespace av { namespace {
using Wire=std::array<unsigned char,160>;
using Digest=std::array<unsigned char,32>;
constexpr unsigned char magic[8]={'F','L','M','E','M','O','F',1};
bool fail(std::string&e,const char*t){e=t;return false;}
bool io(GSocketConnection*c,void*p,size_t n,bool output,std::string&e){
 gsize done=0;bool ok=output?g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(c)),p,n,&done,nullptr,nullptr):g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(c)),p,n,&done,nullptr,nullptr);
 return (ok&&done==n)||fail(e,"memory agreement I/O failed");
}
bool equal(const unsigned char*a,const unsigned char*b,size_t n){unsigned diff=0;for(size_t i=0;i<n;++i)diff|=a[i]^b[i];return diff==0;}
bool nonzero(const unsigned char*p,size_t n){unsigned v=0;for(size_t i=0;i<n;++i)v|=p[i];return v!=0;}
void put(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));}
uint32_t get(const unsigned char*p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
bool valid(const MemoryOffer&o){return flMemoryRequirementsValidate(&o.requirements)==FL_MEMORY_OK&&flMemoryPathValidate(&o.path)==FL_MEMORY_OK&&nonzero(o.resourceBinding.data(),32);}
bool encode(const MemoryOffer&o,bool producer,Wire&w,std::string&e){
 if(!valid(o))return fail(e,"invalid memory offer or missing trusted binding");
 memcpy(w.data(),magic,8);w[8]=producer?'P':'C';
 if(!secureRandom(w.data()+16,32,e))return false;
 if(flMemoryRequirementsEncode(&o.requirements,w.data()+48,64)!=FL_MEMORY_OK)return fail(e,"invalid memory requirements");
 put(w.data()+112,o.path.version);put(w.data()+116,o.path.resourceType);put(w.data()+120,o.path.supportedUsage);put(w.data()+124,o.path.operations);
 memcpy(w.data()+128,o.resourceBinding.data(),32);return true;
}
bool decode(const Wire&w,bool producer,MemoryOffer&o,std::string&e){
 unsigned char zero[7]{};
 if(memcmp(w.data(),magic,8)||w[8]!=(producer?'P':'C')||memcmp(w.data()+9,zero,7)||!nonzero(w.data()+16,32))return fail(e,"memory extension version/role/nonce rejected");
 if(flMemoryRequirementsDecode(w.data()+48,64,&o.requirements)!=FL_MEMORY_OK)return fail(e,"invalid remote memory requirements");
 o.path.version=get(w.data()+112);o.path.resourceType=get(w.data()+116);o.path.supportedUsage=get(w.data()+120);o.path.operations=get(w.data()+124);memcpy(o.resourceBinding.data(),w.data()+128,32);
 return valid(o)||fail(e,"invalid remote memory path/binding");
}
Digest proof(const Endpoint&e,const unsigned char*session,const Wire&p,const Wire&c,unsigned char direction){
 auto*h=g_hmac_new(G_CHECKSUM_SHA256,e.token.data(),e.token.size());
 static const unsigned char domain[]="FrameLink-memory-agreement-v1";
 g_hmac_update(h,domain,sizeof(domain));g_hmac_update(h,&direction,1);
 g_hmac_update(h,session,FL_SESSION_WIRE_SIZE);g_hmac_update(h,p.data(),p.size());g_hmac_update(h,c.data(),c.size());
 Digest d{};gsize n=d.size();g_hmac_get_digest(h,d.data(),&n);g_hmac_unref(h);return d;
}
}
bool agreeMemory(GSocketConnection*c,const Endpoint&key,const flSessionConfig&cfg,bool producer,const MemoryOffer&local,MemoryOffer&remote,std::string&error){
 error.clear();unsigned char session[FL_SESSION_WIRE_SIZE]{};
 if(!c||!g_socket_get_timeout(g_socket_connection_get_socket(c))||flSessionEncode(&cfg,session,sizeof(session))!=FL_SESSION_OK)return fail(error,"memory agreement requires valid session and finite timeout");
 Wire own{},other{};if(!encode(local,producer,own,error))return false;
 // Consumer sends first. Fixed bounded messages; no advertised length allocation.
 if(producer){if(!io(c,other.data(),other.size(),false,error)||!io(c,own.data(),own.size(),true,error))return false;}
 else if(!io(c,own.data(),own.size(),true,error)||!io(c,other.data(),other.size(),false,error))return false;
 MemoryOffer candidate{};if(!decode(other,!producer,candidate,error))return false;
 const auto&p=producer?own:other;const auto&consumer=producer?other:own;
 auto sent=proof(key,session,p,consumer,producer?'P':'C');Digest received{};
 if(!io(c,sent.data(),sent.size(),true,error)||!io(c,received.data(),received.size(),false,error))return false;
 auto expected=proof(key,session,p,consumer,producer?'C':'P');
 if(!equal(received.data(),expected.data(),32))return fail(error,"memory agreement authentication/session binding rejected");
 flMemoryRequirements requirements[]={local.requirements,candidate.requirements};flMemoryPath paths[]={local.path,candidate.path};flMemoryDecision decisions[2]{};
 bool accepted=equal(local.resourceBinding.data(),candidate.resourceBinding.data(),32)&&flMemoryEvaluateBoundary(requirements,paths,2,decisions)==FL_MEMORY_OK;
 // Authenticate acceptance too; no unauthenticated byte can approve a rejected path.
 auto approval=proof(key,session,p,consumer,producer?(accepted?'p':'x'):(accepted?'c':'y'));
 if(!io(c,approval.data(),approval.size(),true,error)||!io(c,received.data(),received.size(),false,error))return false;
 auto want=proof(key,session,p,consumer,producer?'c':'p');
 if(!accepted||!equal(received.data(),want.data(),32))return fail(error,"memory requirements or exact resource binding incompatible");
 remote=candidate;return true;
}
}

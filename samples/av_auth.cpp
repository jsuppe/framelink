#include "av_auth.h"
#include <array>
#include <cstring>
namespace av {
namespace {
using Nonce=std::array<unsigned char,32>;
using Digest=std::array<unsigned char,32>;
constexpr unsigned char magic[8]={'F','L','A','V','A','U','T',1};
bool fail(std::string& error,const char* text){error=text;return false;}
bool write(GSocketConnection* c,const void* data,size_t n,std::string& error){
 gsize done=0;return (g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(c)),data,n,&done,nullptr,nullptr)&&done==n)||fail(error,"authentication write failed");
}
bool read(GSocketConnection* c,void* data,size_t n,std::string& error){
 gsize done=0;return (g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(c)),data,n,&done,nullptr,nullptr)&&done==n)||fail(error,"authentication read failed");
}
Digest proof(const Endpoint& e,unsigned char direction,char role,const Nonce& client,const Nonce& server){
 auto* h=g_hmac_new(G_CHECKSUM_SHA256,e.token.data(),e.token.size());
 g_hmac_update(h,magic,sizeof(magic));g_hmac_update(h,&direction,1);
 auto r=static_cast<unsigned char>(role);g_hmac_update(h,&r,1);
 g_hmac_update(h,client.data(),client.size());g_hmac_update(h,server.data(),server.size());
 Digest d{};gsize n=d.size();g_hmac_get_digest(h,d.data(),&n);g_hmac_unref(h);return d;
}
bool equal(const Digest&a,const Digest&b){unsigned diff=0;for(size_t i=0;i<a.size();++i)diff|=a[i]^b[i];return diff==0;}
bool roleValid(char role){return role=='C'||role=='A';}
}
bool authenticateClient(GSocketConnection*c,const Endpoint&e,char role,std::string&error){
 if(!c||!roleValid(role))return fail(error,"invalid authentication role");
 Nonce client{},server{};Digest received{};
 if(!secureRandom(client.data(),client.size(),error)||!write(c,magic,sizeof(magic),error)||!write(c,&role,1,error)||!write(c,client.data(),client.size(),error)
    ||!read(c,server.data(),server.size(),error)||!read(c,received.data(),received.size(),error))return false;
 if(!equal(received,proof(e,'S',role,client,server)))return fail(error,"server authentication rejected");
 auto response=proof(e,'C',role,client,server);
 unsigned char ok=0;
 return write(c,response.data(),response.size(),error)&&read(c,&ok,1,error)&&(ok==1||fail(error,"client authentication rejected"));
}
bool authenticateServer(GSocketConnection*c,const Endpoint&e,char expectedRole,std::string&error){
 if(!c||!roleValid(expectedRole))return fail(error,"invalid authentication role");
 unsigned char receivedMagic[8]{};char role=0;Nonce client{},server{};Digest received{};
 if(!read(c,receivedMagic,sizeof(receivedMagic),error)||memcmp(receivedMagic,magic,sizeof(magic))||!read(c,&role,1,error)||role!=expectedRole)
     return fail(error,"authentication protocol or role rejected");
 if(!read(c,client.data(),client.size(),error)||!secureRandom(server.data(),server.size(),error))return false;
 auto response=proof(e,'S',role,client,server);
 if(!write(c,server.data(),server.size(),error)||!write(c,response.data(),response.size(),error)||!read(c,received.data(),received.size(),error))return false;
 if(!equal(received,proof(e,'C',role,client,server)))return fail(error,"client authentication rejected");
 unsigned char ok=1;return write(c,&ok,1,error);
}
}

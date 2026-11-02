#include "av_memory_agreement.h"
#include "av_auth.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Wire=std::array<unsigned char,160>;
using Digest=std::array<unsigned char,32>;
static bool io(GSocketConnection*c,void*p,size_t n,bool write){gsize done=0;return (write?
 g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(c)),p,n,&done,nullptr,nullptr):
 g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(c)),p,n,&done,nullptr,nullptr))&&done==n;}
static flSessionConfig config(){flSessionConfig c{};c.version=1;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;c.videoTrackId=1;c.audioTrackId=2;c.video={FL_FORMAT_RGBA8,64,32,30,1};c.audio={FL_PCM_S16,48000,FL_LAYOUT_STEREO};c.epochNs=1000000000;c.videoQueueFrames=2;c.audioQueueMs=40;c.connectTimeoutMs=c.idleTimeoutMs=1000;return c;}
static av::MemoryOffer offer(){av::MemoryOffer o{};CHECK(flMemoryRequirementsInit(&o.requirements)==FL_MEMORY_OK);o.requirements.requiredUsage=FL_MEMORY_GPU_SAMPLE;o.path={1,FL_MEMORY_DMABUF,FL_MEMORY_GPU_SAMPLE,0,{}};o.resourceBinding[0]=42;return o;}
// Independent fixed wire fixture: do not call the production private encoder.
static Wire request(){Wire w{};memcpy(w.data(),"FLMEMOF\1C",9);w[16]=17;
 const unsigned char requirements[24]={'F','M','R','1',1,0,0,0,4,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0};
 memcpy(w.data()+48,requirements,24);w[112]=1;w[116]=FL_MEMORY_DMABUF;w[120]=FL_MEMORY_GPU_SAMPLE;w[128]=42;return w;}
static Digest proof(const av::Endpoint&key,const Wire&p,const Wire&c,unsigned char direction){
 unsigned char session[FL_SESSION_WIRE_SIZE]{};auto cfg=config();CHECK(flSessionEncode(&cfg,session,sizeof(session))==FL_SESSION_OK);
 auto*h=g_hmac_new(G_CHECKSUM_SHA256,key.token.data(),key.token.size());
 const unsigned char domain[]="FrameLink-memory-agreement-v1";
 g_hmac_update(h,domain,sizeof(domain));g_hmac_update(h,&direction,1);g_hmac_update(h,session,sizeof(session));
 g_hmac_update(h,p.data(),p.size());g_hmac_update(h,c.data(),c.size());Digest d{};gsize n=d.size();g_hmac_get_digest(h,d.data(),&n);g_hmac_unref(h);return d;
}
int main(){av::Endpoint key;std::string error;CHECK(av::secureRandom(key.token.data(),key.token.size(),error));
 const char* names[]={"truncated-legacy","role","reserved","zero-nonce","requirements-magic","requirements-version","path-version","multi-resource","unknown-operation","zero-binding","bad-proof","reflection","bad-approval","strict-copy-approval","valid-control"};
 for(unsigned mode=0;mode<15;mode++){
  auto begin=std::chrono::steady_clock::now();auto*l=g_socket_listener_new();auto*a=g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);auto*sa=g_inet_socket_address_new(a,0);GSocketAddress*effective=nullptr;
  CHECK(g_socket_listener_add_address(l,sa,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr));auto port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(a);g_object_unref(sa);g_object_unref(effective);
  bool accepted=false;av::MemoryOffer remote;memset(&remote,0x55,sizeof(remote));auto unchanged=remote;
  std::thread server([&]{auto*c=g_socket_listener_accept(l,nullptr,nullptr,nullptr);CHECK(c);g_socket_set_timeout(g_socket_connection_get_socket(c),1);std::string e;CHECK(av::authenticateServer(c,key,'C',e));accepted=av::agreeMemory(c,key,config(),true,offer(),remote,e);g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);});
  auto*client=g_socket_client_new();g_socket_client_set_timeout(client,1);auto*c=g_socket_client_connect_to_host(client,"127.0.0.1",port,nullptr,nullptr);g_object_unref(client);CHECK(c);g_socket_set_timeout(g_socket_connection_get_socket(c),1);CHECK(av::authenticateClient(c,key,'C',error));
  auto own=request();Wire peer{};
  switch(mode){case 1:own[8]='P';break;case 2:own[9]=1;break;case 3:own[16]=0;break;case 4:own[48]=0;break;case 5:own[52]=2;break;case 6:own[112]=2;break;case 7:own[116]|=FL_MEMORY_AHB;break;case 8:own[127]=128;break;case 9:own[128]=0;break;case 13:own[124]=FL_MEMORY_COPY;break;}
  CHECK(io(c,own.data(),mode==0?8:own.size(),true));
  if(mode!=0){CHECK(io(c,peer.data(),peer.size(),false));
   if(mode>=10){Digest received{};CHECK(io(c,received.data(),received.size(),false));auto sent=proof(key,peer,own,'C');
    if(mode==10)sent.fill(0);if(mode==11)sent=received;CHECK(io(c,sent.data(),sent.size(),true));
    if(mode>=12){CHECK(io(c,received.data(),received.size(),false));auto approval=proof(key,peer,own,'c');if(mode==12)approval.fill(0);CHECK(io(c,approval.data(),approval.size(),true));}
   }
  }
  g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);server.join();g_object_unref(l);
  CHECK(accepted==(mode==14));if(mode!=14)CHECK(!memcmp(&remote,&unchanged,sizeof(remote)));
  CHECK(std::chrono::steady_clock::now()-begin<std::chrono::seconds(3));printf("PASS %s (bounded, unchanged output on rejection)\n",names[mode]);
 }
}

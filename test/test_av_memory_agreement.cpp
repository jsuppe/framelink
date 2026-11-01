#include "av_memory_agreement.h"
#include "av_auth.h"
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static flSessionConfig config(){flSessionConfig c{};c.version=1;c.sessionId.bytes[0]=1;c.clockId.bytes[0]=2;c.generation=1;c.videoTrackId=1;c.audioTrackId=2;c.video={FL_FORMAT_RGBA8,64,32,30,1};c.audio={FL_PCM_S16,48000,FL_LAYOUT_STEREO};c.epochNs=1000000000;c.videoQueueFrames=2;c.audioQueueMs=40;c.connectTimeoutMs=c.idleTimeoutMs=1000;return c;}
static av::MemoryOffer offer(){av::MemoryOffer o{};CHECK(flMemoryRequirementsInit(&o.requirements)==FL_MEMORY_OK);o.requirements.requiredUsage=FL_MEMORY_GPU_SAMPLE;o.path.version=1;o.path.resourceType=FL_MEMORY_DMABUF;o.path.supportedUsage=FL_MEMORY_GPU_SAMPLE;o.resourceBinding[0]=42;return o;}
int main(){av::Endpoint key;std::string error;CHECK(av::secureRandom(key.token.data(),32,error));
 for(int mode=0;mode<10;++mode){
  auto*l=g_socket_listener_new();auto*a=g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);auto*sa=g_inet_socket_address_new(a,0);GSocketAddress*effective=nullptr;
  CHECK(g_socket_listener_add_address(l,sa,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr));auto port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(a);g_object_unref(sa);g_object_unref(effective);
  auto producer=offer(),consumer=offer();auto pc=config(),cc=pc;auto ck=key;
  if(mode==1)consumer.resourceBinding[1]=1;
  if(mode==2)producer.path.operations=FL_MEMORY_COPY;
  if(mode==3)producer.path.supportedUsage=0;
  if(mode==4)cc.generation++;
  if(mode==5)ck.token[0]^=1;
  if(mode==6)consumer.path.resourceType=FL_MEMORY_AHB;
  if(mode==7){producer.path.operations=FL_MEMORY_COPY;producer.requirements.allowedOperations=consumer.requirements.allowedOperations=FL_MEMORY_COPY;}
  if(mode==8)consumer.requirements.version=99;
  if(mode==9)consumer.resourceBinding.fill(0);
  bool serverOK=false;av::MemoryOffer po{},co{};memset(&po,0x55,sizeof(po));memset(&co,0x55,sizeof(co));auto ps=po,cs=co;
  std::thread server([&]{auto*c=g_socket_listener_accept(l,nullptr,nullptr,nullptr);CHECK(c);g_socket_set_timeout(g_socket_connection_get_socket(c),1);std::string e;CHECK(av::authenticateServer(c,key,'C',e));serverOK=av::agreeMemory(c,key,pc,true,producer,po,e);g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);});
  auto*client=g_socket_client_new();g_socket_client_set_timeout(client,1);auto*c=g_socket_client_connect_to_host(client,"127.0.0.1",port,nullptr,nullptr);g_object_unref(client);CHECK(c);g_socket_set_timeout(g_socket_connection_get_socket(c),1);CHECK(av::authenticateClient(c,key,'C',error));
  bool clientOK=av::agreeMemory(c,ck,cc,false,consumer,co,error);g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);server.join();g_object_unref(l);
  bool expected=mode==0||mode==7;CHECK(clientOK==expected&&serverOK==expected);
  if(!expected)CHECK(!memcmp(&po,&ps,sizeof(po))&&!memcmp(&co,&cs,sizeof(co)));
  else CHECK(co.path.operations==producer.path.operations&&po.requirements.allowedOperations==consumer.requirements.allowedOperations);
 }
 puts("authenticated memory agreement: exact binding, both-endpoint policy, explicit fallback, session/key mismatch, invalid offer and unchanged-output negatives pass");
}

#include "av_auth.h"
#include <cstdio>
#include <thread>
#include <stdexcept>
#include <cstring>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
bool transfer(GSocketConnection*c,void*p,size_t n,bool output){gsize done=0;return (output?g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(c)),p,n,&done,nullptr,nullptr):g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(c)),p,n,&done,nullptr,nullptr))&&done==n;}
int main(){try{
 av::Endpoint e;std::string error;CHECK(av::secureRandom(e.token.data(),e.token.size(),error));
 for(int mode=0;mode<5;++mode){
  auto*l=g_socket_listener_new();auto*address=g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);auto*sa=g_inet_socket_address_new(address,0);GSocketAddress*effective=nullptr;
  CHECK(g_socket_listener_add_address(l,sa,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr));
  auto port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(address);g_object_unref(sa);g_object_unref(effective);
  bool serverOK=false;bool fakeNoSecret=false;
  std::thread server([&]{auto*c=g_socket_listener_accept(l,nullptr,nullptr,nullptr);if(!c)return;g_socket_set_timeout(g_socket_connection_get_socket(c),2);std::string err;
   if(mode==3){unsigned char hello[41]{},reply[64]{},probe[1]{};bool got=transfer(c,hello,sizeof(hello),false)&&transfer(c,reply,sizeof(reply),true);fakeNoSecret=got&&!transfer(c,probe,1,false);}
   else serverOK=av::authenticateServer(c,e,'C',err);
   g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);
  });
  auto*client=g_socket_client_new();g_socket_client_set_timeout(client,2);auto*c=g_socket_client_connect_to_host(client,"127.0.0.1",port,nullptr,nullptr);g_object_unref(client);CHECK(c);
  g_socket_set_timeout(g_socket_connection_get_socket(c),2);auto key=e;if(mode==1)key.token[0]^=1;
  bool clientOK=false;
  if(mode==4){ // A server proof cannot be reflected as a client proof.
   unsigned char hello[41]={'F','L','A','V','A','U','T',1,'C'},response[64]{},ack=0;
   CHECK(av::secureRandom(hello+9,32,error));CHECK(transfer(c,hello,sizeof(hello),true));CHECK(transfer(c,response,sizeof(response),false));
   CHECK(transfer(c,response+32,32,true));clientOK=transfer(c,&ack,1,false)&&ack==1;
  }else clientOK=av::authenticateClient(c,key,mode==2?'A':'C',error);
  g_io_stream_close(G_IO_STREAM(c),nullptr,nullptr);g_object_unref(c);server.join();g_object_unref(l);
  CHECK(clientOK==(mode==0));CHECK(serverOK==(mode==0));if(mode==3)CHECK(fakeNoSecret);
 }
 puts("mutual A/V authentication: success, wrong key/role, fake server and reflection rejection passed");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}

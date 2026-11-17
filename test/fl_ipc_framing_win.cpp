#include "fl_ipc.h"
#include <windows.h>
#include <thread>
#include <future>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s error=%lu\n",__LINE__,#x,GetLastError());exit(1);}}while(0)
static void test(unsigned index,unsigned actual,unsigned declared,bool magic,bool expected){
 std::string name="framelink-framing-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(index);
 std::promise<bool> ready;auto created=ready.get_future();bool got=false,connected=false;
 std::thread server([&]{auto ch=fl::Channel::listen(name,[](void*p,bool ok){static_cast<std::promise<bool>*>(p)->set_value(ok);},&ready);
  auto message=ch.recv();got=bool(message);connected=ch.connected();
  if(message){CHECK(message->type==fl::MsgType::Hello&&message->payload.size()==declared);}
 });
 CHECK(created.wait_for(std::chrono::seconds(3))==std::future_status::ready&&created.get());
 HANDLE pipe=CreateFileA(("\\\\.\\pipe\\"+name).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);CHECK(pipe!=INVALID_HANDLE_VALUE);
 unsigned char bytes[64]{};fl::MsgHeader header{magic?fl::kMagic:0,fl::MsgType::Hello,declared};memcpy(bytes,&header,sizeof(header));
 DWORD written=0;CHECK(WriteFile(pipe,bytes,actual,&written,nullptr)&&written==actual);CloseHandle(pipe);server.join();
 CHECK(got==expected);if(!expected)CHECK(!connected);
}
int main(){
 test(0,sizeof(fl::MsgHeader),0,true,true);
 test(1,sizeof(fl::MsgHeader)+4,4,true,true);
 test(2,sizeof(fl::MsgHeader)+4,0,true,false); // trailing bytes must not be ignored
 test(3,sizeof(fl::MsgHeader),4,true,false);
 test(4,sizeof(fl::MsgHeader)-1,0,true,false);
 test(5,sizeof(fl::MsgHeader),0,false,false);
 test(6,sizeof(fl::MsgHeader),0xffffffffu,true,false);
 puts("Windows IPC exact-message framing: 2 valid and 5 malformed cases passed");
}

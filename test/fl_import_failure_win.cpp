// Include the backend to exercise its transaction boundary without adding a
// production fault-injection/export API. Public integration tests remain separate.
#include "../src/framelink_win.cpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static uint64_t duplicate(HANDLE h) {
 HANDLE out=nullptr;CHECK(DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&out,0,FALSE,DUPLICATE_SAME_ACCESS));return (uint64_t)(uintptr_t)out;
}
int main() {
 flChannel *c=nullptr,*p=nullptr;
 std::string name="import-failure-"+std::to_string(GetCurrentProcessId());
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU,&c)==FL_OK);
 for(int i=0;i<100&&!p;++i){auto r=flOpenProducer(name.c_str(),&p);CHECK(r==FL_OK||r==FL_NOT_FOUND);if(!p)Sleep(5);}CHECK(p);
 auto* original=p->images[0];auto generation=p->info.generation;
 for(unsigned mode=0;mode<3;++mode)for(unsigned repeat=0;repeat<8;++repeat) {
  fl::RingDescMsg ring{};ring.width=64;ring.height=32;ring.format=FL_FORMAT_RGBA8;ring.poolSize=2;ring.generation=generation+1;
  for(unsigned i=0;i<2;++i)ring.texture[i]=duplicate(c->images[i]->shared);
  ring.readyFence=duplicate(c->readyShared);ring.consumedFence=duplicate(c->consumedShared);
  if(mode<2) {
   uint64_t& invalid=mode==0?ring.texture[1]:ring.readyFence;
   CloseHandle((HANDLE)(uintptr_t)invalid);
   invalid=(uint64_t)(uintptr_t)CreateEvent(nullptr,FALSE,FALSE,nullptr);CHECK(invalid);
  } else ring.width=128; // Imported texture disagrees with descriptor.
  CHECK(importRing(p,&ring)!=FL_OK);
  CHECK(p->images.size()==2 && p->images[0]==original && p->info.generation==generation && p->info.width==64);
  DWORD flags=0;
  for(unsigned i=0;i<2;++i)CHECK(!GetHandleInformation((HANDLE)(uintptr_t)ring.texture[i],&flags));
  CHECK(!GetHandleInformation((HANDLE)(uintptr_t)ring.readyFence,&flags));
  CHECK(!GetHandleInformation((HANDLE)(uintptr_t)ring.consumedFence,&flags));
 }
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK && flSubmit(p,&b,123)==FL_OK);
 flFrame f{};CHECK(flAcquireFrame(c,&f,1000)==FL_OK && f.ptsNs==123 && flRelease(c,&f)==FL_OK);
 flClose(p);flClose(c);
 puts("24 failed ring imports close every received handle and preserve old ring");
}

#include "../src/fl_shared_cpu_allocation_win.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s error=%lu\n",__LINE__,#x,GetLastError());exit(1);}}while(0)
using Allocation=fl::SharedCpuAllocationWin;
static flSharedCpuDescriptor desc(bool nv12){
 flSharedCpuDescriptor d{};d.version=1;d.resourceType=FL_MEMORY_SHARED_CPU;d.format=nv12?FL_FORMAT_NV12:FL_FORMAT_BGRA8;
 d.width=130;d.height=66;d.planeCount=nv12?2:1;d.generation=7;d.allocationId=13;d.planeOffset[0]=64;d.planeStride[0]=nv12?256:768;
 d.allocationBytes=64+d.planeStride[0]*66;if(nv12){d.planeOffset[1]=d.allocationBytes+128;d.planeStride[1]=256;d.allocationBytes=d.planeOffset[1]+256*33;}
 SYSTEM_INFO s{};GetSystemInfo(&s);d.allocationBytes=(d.allocationBytes+s.dwPageSize-1)/s.dwPageSize*s.dwPageSize;return d;
}
static void pixels(const flMappedImage&v,unsigned n,bool write){
 for(unsigned p=0;p<v.planes;p++)for(unsigned y=0;y<v.plane[p].rows;y++)for(unsigned x=0;x<v.plane[p].rowBytes;x++){
  auto*at=(unsigned char*)v.plane[p].pixels+y*v.plane[p].stride+x;unsigned char expected=(n*17+p*71+y*7+x*3)&255;
  if(write)*at=expected;else CHECK(*at==expected);
 }
}
static DWORD handles(){DWORD n=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&n));return n;}
static int child(int argc,char**argv){
 CHECK(argc==5);auto d=desc(atoi(argv[4])!=0);
 HANDLE parent=OpenProcess(PROCESS_DUP_HANDLE,FALSE,DWORD(strtoul(argv[2],nullptr,10)));CHECK(parent);
 HANDLE remote=reinterpret_cast<HANDLE>(uintptr_t(_strtoui64(argv[3],nullptr,10))),local=nullptr;
 CHECK(DuplicateHandle(parent,remote,GetCurrentProcess(),&local,0,FALSE,DUPLICATE_SAME_ACCESS));CloseHandle(parent);
 Allocation a;CHECK(Allocation::importHandle(d,local,7,13,a)==FL_OK);CloseHandle(local);
 Allocation::Mapping map;CHECK(a.map(false,map)==FL_OK);a.reset();pixels(map.view(),7,false);
 DWORD oldProtection=0;CHECK(VirtualProtect(map.view().plane[0].pixels,1,PAGE_READWRITE,&oldProtection)==FALSE);
 map.reset();return 0;
}
static void crossProcess(bool nv12){
 auto d=desc(nv12);Allocation owner;CHECK(Allocation::create(d,owner)==FL_OK);
 Allocation::Mapping writer;CHECK(owner.map(true,writer)==FL_OK);pixels(writer.view(),7,true);
 HANDLE ro=nullptr;CHECK(owner.exportHandle(false,GetCurrentProcess(),ro)==FL_OK);
 char exe[32768]{};CHECK(GetModuleFileNameA(nullptr,exe,sizeof(exe))>0);
 char cmd[33000]{};snprintf(cmd,sizeof(cmd),"\"%s\" --child %lu %llu %d",exe,GetCurrentProcessId(),
  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ro)),int(nv12));
 STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 CHECK(CreateProcessA(nullptr,cmd,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process));
 CHECK(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);DWORD exitCode=99;CHECK(GetExitCodeProcess(process.hProcess,&exitCode)&&exitCode==0);
 CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(ro);
}
int main(int argc,char**argv){
 if(argc>1)return child(argc,argv);
 // Warm loader/query paths before the exact handle-baseline check.
 {Allocation a;CHECK(Allocation::create(desc(false),a)==FL_OK);}
 crossProcess(false); // Warm CreateProcess runtime before measuring retained handles.
 auto baseline=handles();
 for(bool nv12:{false,true}){
  auto d=desc(nv12);Allocation owner;CHECK(Allocation::create(d,owner)==FL_OK);
  Allocation::Mapping writer;CHECK(owner.map(true,writer)==FL_OK);pixels(writer.view(),3,true);
  HANDLE ro=nullptr,rw=nullptr;CHECK(owner.exportHandle(false,GetCurrentProcess(),ro)==FL_OK);CHECK(owner.exportHandle(true,GetCurrentProcess(),rw)==FL_OK);
  DWORD flags=99;CHECK(GetHandleInformation(ro,&flags)&&!(flags&HANDLE_FLAG_INHERIT));
  CHECK(!MapViewOfFile(ro,FILE_MAP_WRITE,0,0,0)&&GetLastError()==ERROR_ACCESS_DENIED);
  Allocation reader;CHECK(Allocation::importHandle(d,ro,7,13,reader)==FL_OK);auto*identity=reader.descriptor();
  CHECK(Allocation::importHandle(d,ro,8,13,reader)==FL_INVALID&&reader.descriptor()==identity);
  CHECK(Allocation::importHandle(d,ro,7,14,reader)==FL_INVALID&&reader.descriptor()==identity);
  auto wrong=d;wrong.allocationBytes+=4096;CHECK(Allocation::importHandle(wrong,ro,7,13,reader)==FL_INVALID);
  wrong=d;wrong.allocationBytes--;CHECK(Allocation::create(wrong,owner)==FL_INVALID);
  HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);CHECK(Allocation::importHandle(d,event,7,13,reader)==FL_INVALID);CloseHandle(event);
  HANDLE reserved=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE|SEC_RESERVE,0,DWORD(d.allocationBytes),nullptr);CHECK(reserved);
  CHECK(Allocation::importHandle(d,reserved,7,13,reader)==FL_INVALID);CloseHandle(reserved);
  HANDLE noQuery=nullptr;CHECK(DuplicateHandle(GetCurrentProcess(),ro,GetCurrentProcess(),&noQuery,SECTION_MAP_READ,FALSE,0));
  CHECK(Allocation::importHandle(d,noQuery,7,13,reader)==FL_INVALID);CloseHandle(noQuery);
  char tempDir[MAX_PATH]{},tempFile[MAX_PATH]{};CHECK(GetTempPathA(MAX_PATH,tempDir));CHECK(GetTempFileNameA(tempDir,"fsc",0,tempFile));
  HANDLE file=CreateFileA(tempFile,GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);CHECK(file!=INVALID_HANDLE_VALUE);
  HANDLE fileMap=CreateFileMappingW(file,nullptr,PAGE_READWRITE,0,DWORD(d.allocationBytes),nullptr);CHECK(fileMap);
  CHECK(Allocation::importHandle(d,fileMap,7,13,reader)==FL_INVALID);CloseHandle(fileMap);CloseHandle(file);
  CHECK(reader.descriptor()==identity);
  Allocation::Mapping held;CHECK(reader.map(false,held)==FL_OK);auto*pointer=held.view().plane[0].pixels;
  CHECK(reader.map(true,held)==FL_ACCESS_DENIED&&held.view().plane[0].pixels==pointer);
  HANDLE sentinel=INVALID_HANDLE_VALUE;CHECK(reader.exportHandle(true,GetCurrentProcess(),sentinel)==FL_ACCESS_DENIED&&sentinel==INVALID_HANDLE_VALUE);
  CloseHandle(ro);CloseHandle(rw);auto lifetime=reader.lifetime();reader.reset();owner.reset();
  CHECK(!lifetime.expired());pixels(held.view(),3,false);pixels(writer.view(),4,true);pixels(held.view(),4,false);
  void* base=nullptr;MEMORY_BASIC_INFORMATION memory{};CHECK(VirtualQuery(pointer,&memory,sizeof(memory)));base=memory.AllocationBase;
  writer.reset();held.reset();CHECK(lifetime.expired());CHECK(VirtualQuery(base,&memory,sizeof(memory))&&memory.State==MEM_FREE);
 }
 for(unsigned n=0;n<100;n++){Allocation a;CHECK(Allocation::create(desc(n&1),a)==FL_OK);Allocation::Mapping map;CHECK(a.map(true,map)==FL_OK);a.reset();pixels(map.view(),n,true);map.reset();}
 for(unsigned n=0;n<10;n++){crossProcess(false);crossProcess(true);CHECK(handles()==baseline);}
 CHECK(handles()==baseline);puts("Windows CPU section: BGRA/NV12 pixels, exact size/type/epoch, OS readonly, cross-process pixels, retained lifetime, unmapped views and 100 handle-clean cycles passed");
}

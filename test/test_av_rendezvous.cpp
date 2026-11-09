#include "av_rendezvous.h"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#endif
#endif
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#x,error.c_str());return 1;}}while(0)
namespace fs=std::filesystem;
int main(int argc,char** argv){
 std::string error;av::Endpoint e;e.port=12345;CHECK(av::secureRandom(e.token.data(),e.token.size(),error));
 if(argc==3 && std::string(argv[1])=="--crash-owner"){
   av::RendezvousOwner owner;CHECK(owner.publish(argv[2],e,error));
#ifdef _WIN32
   ExitProcess(0);
#else
   _exit(0);
#endif
 }
 std::string suffix;for(unsigned i=0;i<8;++i)suffix+=std::to_string(e.token[i]);
 auto root=fs::temp_directory_path()/("fl-rendezvous-test-"+suffix);
 auto path=(root/"private").string();fs::create_directory(root);
 av::RendezvousOwner owner;CHECK(owner.publish(path,e,error));av::Endpoint got;
 CHECK(av::readEndpoint(path,got,error));CHECK(got.port==e.port&&got.token==e.token);
 {av::RendezvousOwner duplicate;CHECK(!duplicate.publish(path,e,error));}
 CHECK(av::readEndpoint(path,got,error));owner.close();CHECK(!av::readEndpoint(path,got,error));
 CHECK(owner.publish(path,e,error));owner.close();
 // The OS releases the persistent owner lock on death; stale metadata is replaced.
#ifdef _WIN32
 std::string command="\""+std::string(argv[0])+"\" --crash-owner \""+path+"\"";
 STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 CHECK(CreateProcessA(nullptr,&command[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));
 CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);DWORD code=1;
 CHECK(GetExitCodeProcess(pi.hProcess,&code));CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CHECK(code==0);
#else
 pid_t child=fork();CHECK(child>=0);if(!child){av::RendezvousOwner p;if(!p.publish(path,e,error))_exit(2);_exit(0);}
 int status=0;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
#endif
 CHECK(av::readEndpoint(path,got,error));CHECK(owner.publish(path,e,error));
#ifdef __linux__
 // Typed Unix discovery must never be accepted by a legacy TCP consumer.
 got.port=54321;CHECK(!av::readUnixEndpoint(path,got,error));CHECK(got.port==54321);
 owner.close();
 av::UnixRendezvousOwner unixOwner;CHECK(unixOwner.publish(path,error));
 CHECK((fcntl(unixOwner.fd(),F_GETFD)&FD_CLOEXEC)!=0);
 CHECK((fcntl(unixOwner.fd(),F_GETFL)&O_NONBLOCK)!=0);
 av::Endpoint unixEndpoint;CHECK(av::readUnixEndpoint(path,unixEndpoint,error));
 CHECK(!av::readEndpoint(path,got,error));CHECK(got.port==54321);
 auto address=av::unixAddress(unixEndpoint);CHECK(!address.empty()&&address[0]=='\0');
 {av::UnixRendezvousOwner duplicate;CHECK(!duplicate.publish(path,error));CHECK(duplicate.fd()==-1);}
 {av::RendezvousOwner duplicate;CHECK(!duplicate.publish(path,e,error));}
 int socketFd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);CHECK(socketFd>=0);
 sockaddr_un addr{};addr.sun_family=AF_UNIX;memcpy(addr.sun_path,address.data(),address.size());
 CHECK(connect(socketFd,reinterpret_cast<sockaddr*>(&addr),offsetof(sockaddr_un,sun_path)+address.size())==0);
 int accepted=accept4(unixOwner.fd(),nullptr,nullptr,SOCK_CLOEXEC);CHECK(accepted>=0);
 char sent='V',received=0;CHECK(write(socketFd,&sent,1)==1);CHECK(read(accepted,&received,1)==1&&received=='V');
 close(accepted);close(socketFd);unixOwner.close();CHECK(!av::readUnixEndpoint(path,unixEndpoint,error));
 CHECK(unixOwner.publish(path,error));av::Endpoint fresh;CHECK(av::readUnixEndpoint(path,fresh,error));
 CHECK(fresh.unixName!=unixEndpoint.unixName&&fresh.token!=unixEndpoint.token);
 unixOwner.close();
 // Malformed FAR2 records leave outputs intact; no legacy reinterpretation.
 for(int bad=0;bad<4;++bad){
   CHECK(unixOwner.publish(path,error));
   auto record=fs::path(path)/"endpoint";
   std::ifstream in(record,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
   CHECK(bytes.size()==52);
   if(bad==0)bytes.pop_back();if(bad==1)bytes+='x';
   if(bad==2)std::fill(bytes.begin()+4,bytes.begin()+36,0);
   if(bad==3)std::fill(bytes.begin()+36,bytes.end(),0);
   {std::ofstream out(record,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());}
   got.port=54321;CHECK(!av::readUnixEndpoint(path,got,error));CHECK(got.port==54321);
   CHECK(!av::readEndpoint(path,got,error));unixOwner.close();fs::remove(record);
 }
 for(int cycle=0;cycle<100;++cycle){CHECK(unixOwner.publish(path,error));unixOwner.close();CHECK(unixOwner.fd()==-1);}
 // Both formats use one OS lifetime lock; crash recovery produces a fresh name.
 child=fork();CHECK(child>=0);if(!child){av::UnixRendezvousOwner p;if(!p.publish(path,error))_exit(2);_exit(0);}
 CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 CHECK(av::readUnixEndpoint(path,unixEndpoint,error));CHECK(unixOwner.publish(path,error));
 CHECK(av::readUnixEndpoint(path,fresh,error));CHECK(fresh.unixName!=unixEndpoint.unixName);
 unixOwner.close();
 // Mixed transport descriptors must not publish or disturb an existing owner.
 auto mixed=e;mixed.unixName[0]=1;CHECK(!owner.publish(path,mixed,error));
 CHECK(owner.publish(path,e,error));
#endif
 // FAR3 is explicitly CPU-only: old TCP readers never accept it, nor does
 // the CPU reader accept legacy records. All formats share the owner lock.
 got.port=54321;CHECK(!av::readCpuControlEndpoint(path,got,error));CHECK(got.port==54321);
 owner.close();auto cpu=e;cpu.cpuControl=true;
 CHECK(owner.publish(path,cpu,error));CHECK(av::readCpuControlEndpoint(path,got,error));
 CHECK(got.cpuControl&&got.port==e.port&&got.token==e.token);
 CHECK(!av::readEndpoint(path,got,error));
 {av::RendezvousOwner duplicate;CHECK(!duplicate.publish(path,e,error));}
 owner.close();
 for(int bad=0;bad<5;++bad){
  CHECK(owner.publish(path,cpu,error));auto record=fs::path(path)/"endpoint";
  std::ifstream in(record,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
  CHECK(bytes.size()==38&&bytes.substr(0,4)=="FAR3");
  if(bad==0)bytes.pop_back();if(bad==1)bytes+='x';
  if(bad==2)bytes[4]=bytes[5]=0;if(bad==3)std::fill(bytes.begin()+6,bytes.end(),0);
  if(bad==4)bytes[3]='9';
  {std::ofstream out(record,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());}
  got.port=54321;CHECK(!av::readCpuControlEndpoint(path,got,error));CHECK(got.port==54321);
  CHECK(!av::readEndpoint(path,got,error));owner.close();fs::remove(record);
 }
 auto mixedCpu=cpu;mixedCpu.unixName[0]=1;CHECK(!owner.publish(path,mixedCpu,error));
 for(int cycle=0;cycle<100;++cycle){CHECK(owner.publish(path,cpu,error));owner.close();}
 CHECK(owner.publish(path,e,error));
 // FAR4 native policy setup never aliases legacy/CPU records.
 CHECK(!av::readNativeControlEndpoint(path,got,error));owner.close();
 auto native=e;native.nativeControl=true;
 CHECK(owner.publish(path,native,error));CHECK(av::readNativeControlEndpoint(path,got,error));
 CHECK(got.nativeControl&&!got.cpuControl&&got.port==e.port&&got.token==e.token);
 CHECK(!av::readEndpoint(path,got,error));CHECK(!av::readCpuControlEndpoint(path,got,error));
 {av::RendezvousOwner duplicate;CHECK(!duplicate.publish(path,cpu,error));}
 owner.close();auto mixedNative=native;mixedNative.cpuControl=true;CHECK(!owner.publish(path,mixedNative,error));
 mixedNative=native;mixedNative.unixName[0]=1;CHECK(!owner.publish(path,mixedNative,error));
 for(int bad=0;bad<5;++bad){
  CHECK(owner.publish(path,native,error));auto record=fs::path(path)/"endpoint";
  std::ifstream in(record,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
  CHECK(bytes.size()==38&&bytes.substr(0,4)=="FAR4");
  if(bad==0)bytes.pop_back();if(bad==1)bytes+='x';
  if(bad==2)bytes[4]=bytes[5]=0;if(bad==3)std::fill(bytes.begin()+6,bytes.end(),0);if(bad==4)bytes[3]='9';
  {std::ofstream out(record,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());}
  got.port=54321;CHECK(!av::readNativeControlEndpoint(path,got,error));CHECK(got.port==54321);
  owner.close();fs::remove(record);
 }
 CHECK(owner.publish(path,e,error));
 // Oversized or truncated records fail closed and leave caller's output alone.
 {std::ofstream f(fs::path(path)/"endpoint",std::ios::binary|std::ios::trunc);f<<"FAR1";}
 got.port=54321;CHECK(!av::readEndpoint(path,got,error));CHECK(got.port==54321);owner.close();
 fs::remove(fs::path(path)/"endpoint");
#ifndef _WIN32
 CHECK(chmod(path.c_str(),0755)==0);CHECK(!av::readEndpoint(path,got,error));CHECK(!owner.publish(path,e,error));CHECK(chmod(path.c_str(),0700)==0);
 auto alias=root/"alias";fs::create_directory_symlink(path,alias);
 CHECK(!av::readEndpoint(alias.string(),got,error));CHECK(!owner.publish(alias.string()+"/",e,error));fs::remove(alias);
 CHECK(owner.publish(path,e,error));owner.close();
 auto victim=root/"victim";{std::ofstream f(victim);f<<"untouched";}
 fs::create_symlink(victim,fs::path(path)/"endpoint");CHECK(!av::readEndpoint(path,got,error));fs::remove(fs::path(path)/"endpoint");
 std::error_code linkError;fs::create_hard_link(victim,fs::path(path)/"endpoint",linkError);
#ifdef __ANDROID__
 if(linkError==std::errc::permission_denied||linkError==std::errc::operation_not_permitted)
   puts("UNAVAILABLE: hard-link negative fixture denied by Android filesystem policy");
 else
#endif
 {CHECK(!linkError);CHECK(!av::readEndpoint(path,got,error));fs::remove(fs::path(path)/"endpoint");}
 int fifoResult=mkfifo((fs::path(path)/"endpoint").c_str(),0600);
#ifdef __ANDROID__
 if(fifoResult!=0&&(errno==EACCES||errno==EPERM))
   puts("UNAVAILABLE: FIFO negative fixture denied by Android filesystem policy");
 else
#endif
 {CHECK(fifoResult==0);CHECK(!av::readEndpoint(path,got,error));fs::remove(fs::path(path)/"endpoint");}
 fs::remove(fs::path(path)/".owner.lock");fs::create_symlink(victim,fs::path(path)/".owner.lock");CHECK(!owner.publish(path,e,error));fs::remove(fs::path(path)/".owner.lock");
#else
 // A NULL DACL grants everyone access; neither reader nor publisher may accept it.
 CHECK(SetNamedSecurityInfoA(&path[0],SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS);
 CHECK(!av::readEndpoint(path,got,error));CHECK(!owner.publish(path,e,error));
 CHECK(!av::readCpuControlEndpoint(path,got,error));CHECK(!owner.publish(path,cpu,error));
#endif
 fs::remove_all(root);puts("private rendezvous: agreement, owner exclusion, crash recovery, malformed data and access checks passed");return 0;
}

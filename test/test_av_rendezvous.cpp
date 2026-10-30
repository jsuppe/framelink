#include "av_rendezvous.h"
#include <cstdio>
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
 fs::create_hard_link(victim,fs::path(path)/"endpoint");CHECK(!av::readEndpoint(path,got,error));fs::remove(fs::path(path)/"endpoint");
 CHECK(mkfifo((fs::path(path)/"endpoint").c_str(),0600)==0);CHECK(!av::readEndpoint(path,got,error));fs::remove(fs::path(path)/"endpoint");
 fs::remove(fs::path(path)/".owner.lock");fs::create_symlink(victim,fs::path(path)/".owner.lock");CHECK(!owner.publish(path,e,error));fs::remove(fs::path(path)/".owner.lock");
#else
 // A NULL DACL grants everyone access; neither reader nor publisher may accept it.
 CHECK(SetNamedSecurityInfoA(&path[0],SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS);
 CHECK(!av::readEndpoint(path,got,error));CHECK(!owner.publish(path,e,error));
#endif
 fs::remove_all(root);puts("private rendezvous: agreement, owner exclusion, crash recovery, malformed data and access checks passed");return 0;
}

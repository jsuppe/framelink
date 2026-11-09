#include "av_rendezvous.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#endif
#endif
namespace av {
namespace {
constexpr size_t wireSize = 38, unixWireSize = 52;
const char* recordName = "endpoint";
template<size_t N> bool nonzero(const std::array<unsigned char,N>& a) {
    return std::any_of(a.begin(),a.end(),[](unsigned char c){return c!=0;});
}
std::vector<unsigned char> encode(const Endpoint& e) {
    const bool unixSocket=nonzero(e.unixName);
    std::vector<unsigned char> b(unixSocket?unixWireSize:wireSize);
    if(unixSocket){
        // A mixed TCP/Unix endpoint is invalid, never silently select one.
        if(e.port||e.cpuControl||e.nativeControl)return {};
        memcpy(b.data(),"FAR2",4);
        std::copy(e.token.begin(),e.token.end(),b.begin()+4);
        std::copy(e.unixName.begin(),e.unixName.end(),b.begin()+36);
    }else{
        if(e.cpuControl&&e.nativeControl)return {};
        memcpy(b.data(),e.nativeControl?"FAR4":(e.cpuControl?"FAR3":"FAR1"),4);b[4]=e.port&255;b[5]=e.port>>8;
        std::copy(e.token.begin(),e.token.end(),b.begin()+6);
    }
    return b;
}
bool decode(const unsigned char* b,size_t n,Endpoint& e,std::string& error) {
    Endpoint tmp;
    if(n==wireSize&&(!memcmp(b,"FAR1",4)||!memcmp(b,"FAR3",4)||!memcmp(b,"FAR4",4))){
        tmp.cpuControl=!memcmp(b,"FAR3",4);
        tmp.nativeControl=!memcmp(b,"FAR4",4);
        tmp.port=uint16_t(b[4]|(unsigned(b[5])<<8));
        std::copy(b+6,b+38,tmp.token.begin());
        if(!tmp.port){error="invalid TCP rendezvous endpoint";return false;}
    }else if(n==unixWireSize&&!memcmp(b,"FAR2",4)){
        std::copy(b+4,b+36,tmp.token.begin());
        std::copy(b+36,b+52,tmp.unixName.begin());
        if(!nonzero(tmp.unixName)){error="invalid Unix rendezvous endpoint";return false;}
    }else{error="invalid rendezvous record";return false;}
    if(!nonzero(tmp.token)){error="invalid rendezvous credential";return false;}
    e=tmp;return true;
}
bool fail(std::string& error, const char* operation) { error=operation; return false; }
std::string tempName(std::string& error) {
    unsigned char random[16]; if(!secureRandom(random,sizeof(random),error)) return {};
    std::string name=".publish-";
    for(auto c:random){name+="0123456789abcdef"[c>>4];name+="0123456789abcdef"[c&15];}
    return name;
}
#ifdef _WIN32
struct Handle { HANDLE h=INVALID_HANDLE_VALUE; ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
std::wstring wide(const std::string& s) {
    if(s.empty() || s.find('\0')!=std::string::npos) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n)return {}; std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),&out[0],n); return out;
}
struct Security {
    std::vector<unsigned char> user;
    PACL acl=nullptr;
    SECURITY_DESCRIPTOR sd{};
    SECURITY_ATTRIBUTES sa{};
    ~Security(){if(acl)LocalFree(acl);}
    PSID sid(){return reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;}
    bool init(std::string& error) {
        Handle token; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h))return fail(error,"cannot query current user");
        DWORD n=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&n);user.resize(n);
        if(!n || !GetTokenInformation(token.h,TokenUser,user.data(),n,&n))return fail(error,"cannot query current user SID");
        EXPLICIT_ACCESSW ea{};ea.grfAccessPermissions=GENERIC_ALL;ea.grfAccessMode=SET_ACCESS;
        ea.grfInheritance=SUB_CONTAINERS_AND_OBJECTS_INHERIT;ea.Trustee.TrusteeForm=TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType=TRUSTEE_IS_USER;ea.Trustee.ptstrName=static_cast<LPWSTR>(sid());
        if(SetEntriesInAclW(1,&ea,nullptr,&acl)!=ERROR_SUCCESS || !InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION)
          || !SetSecurityDescriptorOwner(&sd,sid(),FALSE) || !SetSecurityDescriptorDacl(&sd,TRUE,acl,FALSE)
          || !SetSecurityDescriptorControl(&sd,SE_DACL_PROTECTED,SE_DACL_PROTECTED))return fail(error,"cannot create private ACL");
        sa.nLength=sizeof(sa);sa.lpSecurityDescriptor=&sd;return true;
    }
    bool check(HANDLE h,bool directory,std::string& error) {
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(h,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
          || bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory || (!directory&&info.nNumberOfLinks!=1))
            return fail(error,"unsafe rendezvous file type or link");
        PSID owner=nullptr;PACL dacl=nullptr;PSECURITY_DESCRIPTOR desc=nullptr;
        DWORD rc=GetSecurityInfo(h,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&dacl,nullptr,&desc);
        bool ok=rc==ERROR_SUCCESS&&owner&&EqualSid(owner,sid())&&dacl;
        if(ok)for(DWORD i=0;i<dacl->AceCount;++i){
            void* p=nullptr;if(!GetAce(dacl,i,&p)){ok=false;break;}
            auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(p);
            // Only straightforward current-user allow ACEs are accepted. Deny
            // ACEs cannot broaden access, but uncommon/object ACEs fail closed.
            if(ace->Header.AceType==ACCESS_DENIED_ACE_TYPE)continue;
            if(ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE || !EqualSid(&ace->SidStart,sid())){ok=false;break;}
        }
        if(desc)LocalFree(desc);return ok||fail(error,"rendezvous must have current-user-only owner and ACL");
    }
};
HANDLE openFile(const std::wstring& p,DWORD access,DWORD share,DWORD disposition,Security& s,bool directory=false) {
    return CreateFileW(p.c_str(),access|READ_CONTROL,share,&s.sa,disposition,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL),nullptr);
}
HANDLE openDir(const std::wstring& p,bool create,Security& s,std::string& error) {
    if(p.empty()){fail(error,"invalid directory path");return INVALID_HANDLE_VALUE;}
    if(create && !CreateDirectoryW(p.c_str(),&s.sa) && GetLastError()!=ERROR_ALREADY_EXISTS){fail(error,"cannot create private directory");return INVALID_HANDLE_VALUE;}
    HANDLE h=openFile(p,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,OPEN_EXISTING,s,true);
    if(h==INVALID_HANDLE_VALUE){fail(error,"cannot open private directory");return h;}
    if(!s.check(h,true,error)){CloseHandle(h);return INVALID_HANDLE_VALUE;}return h;
}
#else
struct Fd { int fd=-1; ~Fd(){if(fd>=0)::close(fd);} };
bool check(int fd,bool directory,std::string& error) {
    struct stat st{};
    if(fstat(fd,&st) || st.st_uid!=geteuid() || (st.st_mode&0077)
      || (directory?!S_ISDIR(st.st_mode):(!S_ISREG(st.st_mode)||st.st_nlink!=1)))
        return fail(error,"rendezvous requires private owner-only nonlinked files");
    return true;
}
int openDir(const std::string& path,bool create,std::string& error) {
    if(path.empty()||path.find('\0')!=std::string::npos){fail(error,"invalid directory path");return -1;}
    if(create&&mkdir(path.c_str(),0700)&&errno!=EEXIST){fail(error,"cannot create private directory");return -1;}
    std::string normalized=path;
    while(normalized.size()>1&&normalized.back()=='/')normalized.pop_back();
    int fd=open(normalized.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0){fail(error,"cannot open private directory");return -1;}
    if(!check(fd,true,error)){::close(fd);return -1;}return fd;
}
#endif
}
bool secureRandom(void* buffer,size_t n,std::string& error) {
#ifdef _WIN32
    if(n>ULONG_MAX)return fail(error,"random request too large");
    return BCryptGenRandom(nullptr,static_cast<PUCHAR>(buffer),ULONG(n),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0
      ||fail(error,"secure random generation failed");
#else
    Fd fd;fd.fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd.fd<0)return fail(error,"cannot open secure random source");
    auto*p=static_cast<unsigned char*>(buffer);size_t done=0;
    while(done<n){ssize_t k=::read(fd.fd,p+done,n-done);if(k<0&&errno==EINTR)continue;if(k<=0)return fail(error,"secure random read failed");done+=size_t(k);}return true;
#endif
}
struct RendezvousOwner::Impl {
    Endpoint endpoint;
#ifdef _WIN32
    Security security;Handle dir,lock;std::wstring path;
#else
    Fd dir,lock;
#endif
};
RendezvousOwner::RendezvousOwner()=default;
RendezvousOwner::~RendezvousOwner(){close();}
bool RendezvousOwner::publish(const std::string& directory,const Endpoint& endpoint,std::string& error) {
    if(impl_)return fail(error,"rendezvous already published");
    auto bytes=encode(endpoint);Endpoint validated;
    if(!decode(bytes.data(),bytes.size(),validated,error))return false;
    std::unique_ptr<Impl> p(new Impl);p->endpoint=endpoint;
    std::string temp=tempName(error);if(temp.empty())return false;
#ifdef _WIN32
    if(!p->security.init(error))return false;p->path=wide(directory);
    p->dir.h=openDir(p->path,true,p->security,error);if(p->dir.h==INVALID_HANDLE_VALUE)return false;
    p->lock.h=openFile(p->path+L"\\.owner.lock",GENERIC_READ|GENERIC_WRITE,0,OPEN_ALWAYS,p->security);
    if(p->lock.h==INVALID_HANDLE_VALUE)return fail(error,"rendezvous owner already active or lock unavailable");
    if(!p->security.check(p->lock.h,false,error))return false;
    auto temporary=p->path+L"\\"+wide(temp),target=p->path+L"\\endpoint";
    Handle f;f.h=openFile(temporary,GENERIC_WRITE,0,CREATE_NEW,p->security);
    if(f.h==INVALID_HANDLE_VALUE)return fail(error,"cannot create rendezvous record");
    DWORD written=0;bool ok=WriteFile(f.h,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(f.h);
    CloseHandle(f.h);f.h=INVALID_HANDLE_VALUE;
    if(!ok){DeleteFileW(temporary.c_str());return fail(error,"cannot write rendezvous record");}
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary.c_str());return fail(error,"cannot publish rendezvous record");}
#else
    p->dir.fd=openDir(directory,true,error);if(p->dir.fd<0)return false;
    p->lock.fd=openat(p->dir.fd,".owner.lock",O_RDWR|O_NONBLOCK|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(p->lock.fd<0||!check(p->lock.fd,false,error))return fail(error,"unsafe or unavailable owner lock");
    if(flock(p->lock.fd,LOCK_EX|LOCK_NB))return fail(error,"rendezvous owner already active");
    Fd f;f.fd=openat(p->dir.fd,temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(f.fd<0)return fail(error,"cannot create rendezvous record");
    size_t written=0;
    while(written<bytes.size()){
        ssize_t n=write(f.fd,bytes.data()+written,bytes.size()-written);if(n<0&&errno==EINTR)continue;
        if(n<=0){unlinkat(p->dir.fd,temp.c_str(),0);return fail(error,"cannot write rendezvous record");}written+=size_t(n);
    }
    if(fsync(f.fd)||renameat(p->dir.fd,temp.c_str(),p->dir.fd,recordName)){
        unlinkat(p->dir.fd,temp.c_str(),0);return fail(error,"cannot publish rendezvous record");}
#endif
    impl_=p.release();return true;
}
void RendezvousOwner::close() {
    if(!impl_)return;
    auto expected=encode(impl_->endpoint);
    unsigned char b[unixWireSize+1]{};
#ifdef _WIN32
    auto name=impl_->path+L"\\endpoint";
    // The directory is pinned against rename; only the same trusted user can
    // alter its entries. Compare our generation token before deleting.
    Handle f;f.h=openFile(name,GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,impl_->security);
    std::string error;DWORD n=0;bool own=f.h!=INVALID_HANDLE_VALUE&&impl_->security.check(f.h,false,error);
    if(own)own=ReadFile(f.h,b,sizeof(b),&n,nullptr)&&n==expected.size()&&memcmp(b,expected.data(),expected.size())==0;
    if(f.h!=INVALID_HANDLE_VALUE){CloseHandle(f.h);f.h=INVALID_HANDLE_VALUE;}
    if(own)DeleteFileW(name.c_str());
#else
    Fd f;f.fd=openat(impl_->dir.fd,recordName,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);std::string error;
    if(f.fd>=0&&check(f.fd,false,error)){
        ssize_t n;do{n=::read(f.fd,b,sizeof(b));}while(n<0&&errno==EINTR);
        if(n==ssize_t(expected.size())&&memcmp(b,expected.data(),expected.size())==0)unlinkat(impl_->dir.fd,recordName,0);
    }
#endif
    // Keep .owner.lock permanently: unlinking lock files permits split ownership.
    delete impl_;impl_=nullptr;
}
static bool readAnyEndpoint(const std::string& directory,Endpoint& endpoint,std::string& error) {
    unsigned char b[unixWireSize+1]{};size_t count=0;
#ifdef _WIN32
    Security s;if(!s.init(error))return false;auto path=wide(directory);
    Handle dir;dir.h=openDir(path,false,s,error);if(dir.h==INVALID_HANDLE_VALUE)return false;
    Handle f;f.h=openFile(path+L"\\endpoint",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,OPEN_EXISTING,s);
    if(f.h==INVALID_HANDLE_VALUE)return fail(error,"rendezvous record unavailable");
    if(!s.check(f.h,false,error))return false;DWORD n=0;
    if(!ReadFile(f.h,b,sizeof(b),&n,nullptr))return fail(error,"cannot read rendezvous record");count=n;
#else
    Fd dir;dir.fd=openDir(directory,false,error);if(dir.fd<0)return false;
    Fd f;f.fd=openat(dir.fd,recordName,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(f.fd<0)return fail(error,"rendezvous record unavailable");
    if(!check(f.fd,false,error))return false;
    ssize_t n;do{n=::read(f.fd,b,sizeof(b));}while(n<0&&errno==EINTR);
    if(n<0)return fail(error,"cannot read rendezvous record");
    count=size_t(n);
#endif
    return decode(b,count,endpoint,error);
}
bool readEndpoint(const std::string& directory,Endpoint& endpoint,std::string& error){
    Endpoint e;if(!readAnyEndpoint(directory,e,error))return false;
    if(!e.port||e.cpuControl||e.nativeControl)return fail(error,"legacy TCP rendezvous required");
    endpoint=e;return true;
}
bool readCpuControlEndpoint(const std::string& directory,Endpoint& endpoint,std::string& error){
    Endpoint e;if(!readAnyEndpoint(directory,e,error))return false;
    if(!e.port||!e.cpuControl||e.nativeControl)return fail(error,"CPU control rendezvous required");
    endpoint=e;return true;
}
bool readNativeControlEndpoint(const std::string& directory,Endpoint& endpoint,std::string& error){
    Endpoint e;if(!readAnyEndpoint(directory,e,error))return false;
    if(!e.port||!e.nativeControl||e.cpuControl)return fail(error,"native control rendezvous required");
    endpoint=e;return true;
}
#ifdef __linux__
bool readUnixEndpoint(const std::string& directory,Endpoint& endpoint,std::string& error){
    Endpoint e;if(!readAnyEndpoint(directory,e,error))return false;
    if(e.port)return fail(error,"Unix rendezvous required");
    endpoint=e;return true;
}
std::string unixAddress(const Endpoint& e) {
    if(e.port||!nonzero(e.unixName)||!nonzero(e.token))return {};
    std::string address(1,'\0');address+="framelink-cpu-";
    for(auto c:e.unixName){address+="0123456789abcdef"[c>>4];address+="0123456789abcdef"[c&15];}
    return address;
}
UnixRendezvousOwner::~UnixRendezvousOwner(){close();}
void UnixRendezvousOwner::close(){
    record_.close();
    if(fd_>=0){::close(fd_);fd_=-1;}
}
bool UnixRendezvousOwner::publish(const std::string& directory,std::string& error){
    if(fd_>=0)return fail(error,"Unix rendezvous already published");
    Endpoint e;
    if(!secureRandom(e.token.data(),e.token.size(),error)||
       !secureRandom(e.unixName.data(),e.unixName.size(),error))return false;
    auto address=unixAddress(e);if(address.empty())return fail(error,"invalid Unix random endpoint");
    Fd listener;listener.fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(listener.fd<0)return fail(error,"cannot create Unix listener");
    sockaddr_un addr{};addr.sun_family=AF_UNIX;memcpy(addr.sun_path,address.data(),address.size());
    if(bind(listener.fd,reinterpret_cast<sockaddr*>(&addr),offsetof(sockaddr_un,sun_path)+address.size())||
       listen(listener.fd,8))return fail(error,"cannot bind Unix listener");
    // Publish only after listen; failed lock acquisition closes only our socket.
    if(!record_.publish(directory,e,error))return false;
    fd_=listener.fd;listener.fd=-1;return true;
}
#endif

}

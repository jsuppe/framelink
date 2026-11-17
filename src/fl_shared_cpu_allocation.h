#pragma once
// Linux/Android internal building block, not a channel or frame-lease protocol.
// Mapping references retain storage after allocation wrappers are destroyed.
// Pixel access still requires externally synchronized, agreed frame ownership.
#include "framelink_shared_cpu.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <cstdio>
#include <cerrno>
#include <new>
namespace fl {
class SharedCpuAllocation {
    struct State {
        int fd=-1;bool writable=false;
        flSharedCpuDescriptor descriptor{};
        void* maps[2]={MAP_FAILED,MAP_FAILED};
        std::mutex mutex;
        ~State(){for(auto p:maps)if(p!=MAP_FAILED)munmap(p,descriptor.allocationBytes);if(fd>=0)close(fd);}
    };
    std::shared_ptr<State> state_;
    struct Fd {int value=-1;~Fd(){if(value>=0)close(value);}};
    static flResult checked(const flSharedCpuDescriptor& d){
        const auto r=flSharedCpuDescriptorValidate(&d);
        return r==FL_MEMORY_OK?FL_OK:r==FL_MEMORY_VERSION_MISMATCH?FL_VERSION_MISMATCH:FL_INVALID;
    }
public:
    class Mapping {
        friend class SharedCpuAllocation;
        std::shared_ptr<State> owner_;
        flMappedImage view_{};
    public:
        const flMappedImage& view()const{return view_;}
        explicit operator bool()const{return bool(owner_);}
        void reset(){owner_.reset();view_={};}
    };
    SharedCpuAllocation()=default;
    SharedCpuAllocation(const SharedCpuAllocation&)=delete;
    SharedCpuAllocation& operator=(const SharedCpuAllocation&)=delete;
    SharedCpuAllocation(SharedCpuAllocation&&)=default;
    SharedCpuAllocation& operator=(SharedCpuAllocation&&)=default;
    void reset(){state_.reset();}
    explicit operator bool()const{return bool(state_);}
    const flSharedCpuDescriptor* descriptor()const{return state_?&state_->descriptor:nullptr;}
    // Import borrows input; this object retains its own CLOEXEC duplicate. The
    // authenticated control layer supplies expected epoch/identity separately.
    static flResult importFd(const flSharedCpuDescriptor& d,int input,uint64_t generation,uint64_t allocationId,SharedCpuAllocation& out){
        auto valid=checked(d);if(valid!=FL_OK)return valid;
        if(d.generation!=generation||d.allocationId!=allocationId||input<0)return FL_INVALID;
        Fd fd{fcntl(input,F_DUPFD_CLOEXEC,0)};if(fd.value<0)return FL_INVALID;
        struct stat st{};const int seals=fcntl(fd.value,F_GET_SEALS),mode=fcntl(fd.value,F_GETFL);
        if(fstat(fd.value,&st)||!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)!=d.allocationBytes||
           seals<0||(seals&(F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL))!=(F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)||mode<0)return FL_INVALID;
        if((mode&O_ACCMODE)!=O_RDONLY&&(mode&O_ACCMODE)!=O_RDWR)return FL_ACCESS_DENIED;
        try {
            auto state=std::make_shared<State>();state->descriptor=d;state->writable=(mode&O_ACCMODE)==O_RDWR&&!(seals&F_SEAL_WRITE);
#ifdef F_SEAL_FUTURE_WRITE
            if(seals&F_SEAL_FUTURE_WRITE)state->writable=false;
#endif
            // Some kernels reject even a PROT_READ shared mapping of a
            // write-sealed file through an O_RDWR description. Preserve the
            // immutable capability by reopening it genuinely read-only.
            if(!state->writable&&(mode&O_ACCMODE)==O_RDWR){
                char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",fd.value);
                int readOnly=open(path,O_RDONLY|O_CLOEXEC);if(readOnly<0)return FL_ACCESS_DENIED;
                close(fd.value);fd.value=readOnly;
            }
            state->fd=fd.value;fd.value=-1;out.state_=std::move(state);return FL_OK;
        }catch(const std::bad_alloc&){return FL_OOM;}
    }
    static flResult create(const flSharedCpuDescriptor& d,SharedCpuAllocation& out){
        auto valid=checked(d);if(valid!=FL_OK)return valid;
        Fd fd{int(syscall(SYS_memfd_create,"framelink-shared-cpu",MFD_CLOEXEC|MFD_ALLOW_SEALING))};
        if(fd.value<0)return errno==ENOSYS?FL_FORMAT_UNSUPPORTED:FL_INTERNAL;
        if(ftruncate(fd.value,d.allocationBytes)||fcntl(fd.value,F_ADD_SEALS,F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)<0)return FL_INTERNAL;
        return importFd(d,fd.value,d.generation,d.allocationId,out);
    }
    // A read-only export is a genuinely O_RDONLY file description, not a flag
    // on a writable duplicate. No read/write upgrade or silent fallback.
    flResult exportFd(bool writable,int& out)const{
        auto state=state_;if(!state)return FL_INVALID;
        if(writable&&!state->writable)return FL_ACCESS_DENIED;
        int fd=-1;
        if(writable)fd=fcntl(state->fd,F_DUPFD_CLOEXEC,0);
        else {char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",state->fd);fd=open(path,O_RDONLY|O_CLOEXEC);}
        if(fd<0)return FL_INTERNAL;
        out=fd;return FL_OK;
    }
    // Query the retained descriptor, never a borrowed FD that may be recycled.
    std::weak_ptr<void> lifetime()const{return state_;}
    flResult identity(dev_t& device,ino_t& inode)const{
        auto state=state_;if(!state)return FL_INVALID;
        struct stat st{};if(fstat(state->fd,&st))return FL_INTERNAL;
        device=st.st_dev;inode=st.st_ino;return FL_OK;
    }
    flResult map(bool writable,Mapping& out)const{
        auto state=state_;if(!state)return FL_INVALID;
        if(writable&&!state->writable)return FL_ACCESS_DENIED;
        std::lock_guard<std::mutex> lock(state->mutex);
        const unsigned index=writable?1:0;auto&base=state->maps[index];
        if(base==MAP_FAILED){base=mmap(nullptr,state->descriptor.allocationBytes,PROT_READ|(writable?PROT_WRITE:0),MAP_SHARED,state->fd,0);if(base==MAP_FAILED)return errno==ENOMEM?FL_OOM:FL_ACCESS_DENIED;}
        Mapping result;result.owner_=state;auto&view=result.view_;const auto&d=state->descriptor;view.planes=d.planeCount;
        for(unsigned p=0;p<view.planes;p++)view.plane[p]={static_cast<char*>(base)+d.planeOffset[p],d.planeStride[p],d.width*(d.format==FL_FORMAT_NV12?1u:4u),p?d.height/2:d.height};
        out=std::move(result);return FL_OK;
    }
};
}

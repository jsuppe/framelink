#pragma once
// Internal Windows shared-CPU storage, not a transport or frame-lease API.
// Only unnamed committed pagefile sections are accepted. allocationBytes must
// include page rounding explicitly; descriptors are never silently rewritten.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "framelink_shared_cpu.h"
#include <memory>
#include <mutex>
#include <new>
namespace fl {
class SharedCpuAllocationWin {
    struct Handle {
        HANDLE value=nullptr;
        ~Handle(){if(value)CloseHandle(value);}
    };
    struct State {
        Handle handle;
        bool writable=false;
        flSharedCpuDescriptor descriptor{};
        void* maps[2]={nullptr,nullptr};
        std::mutex mutex;
        ~State(){for(auto p:maps)if(p)UnmapViewOfFile(p);}
    };
    std::shared_ptr<State> state_;
    struct SectionBasic {void* base;ULONG attributes;LARGE_INTEGER bytes;};
    using QuerySection=LONG(NTAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*);
    static flResult checked(const flSharedCpuDescriptor& d){
        auto result=flSharedCpuDescriptorValidate(&d);
        if(result!=FL_MEMORY_OK)return result==FL_MEMORY_VERSION_MISMATCH?FL_VERSION_MISMATCH:FL_INVALID;
        SYSTEM_INFO info{};GetSystemInfo(&info);
        return d.allocationBytes%info.dwPageSize?FL_INVALID:FL_OK;
    }
public:
    class Mapping {
        friend class SharedCpuAllocationWin;
        std::shared_ptr<State> owner_;
        flMappedImage view_{};
    public:
        const flMappedImage& view()const{return view_;}
        explicit operator bool()const{return bool(owner_);}
        void reset(){owner_.reset();view_={};}
    };
    SharedCpuAllocationWin()=default;
    SharedCpuAllocationWin(const SharedCpuAllocationWin&)=delete;
    SharedCpuAllocationWin& operator=(const SharedCpuAllocationWin&)=delete;
    SharedCpuAllocationWin(SharedCpuAllocationWin&&)=default;
    SharedCpuAllocationWin& operator=(SharedCpuAllocationWin&&)=default;
    void reset(){state_.reset();}
    explicit operator bool()const{return bool(state_);}
    const flSharedCpuDescriptor* descriptor()const{return state_?&state_->descriptor:nullptr;}
    std::weak_ptr<void> lifetime()const{return state_;}
    // Borrows a handle already transferred into THIS process by an authenticated
    // broker. A numeric handle from another process is never directly usable.
    // Require SECTION_QUERY as well as SECTION_MAP_READ; preserve OS write rights.
    static flResult importHandle(const flSharedCpuDescriptor& d,HANDLE input,
                                 uint64_t generation,uint64_t allocationId,
                                 SharedCpuAllocationWin& out){
        auto valid=checked(d);if(valid!=FL_OK)return valid;
        if(d.generation!=generation||d.allocationId!=allocationId||!input||input==INVALID_HANDLE_VALUE)return FL_INVALID;
        Handle handle;
        if(!DuplicateHandle(GetCurrentProcess(),input,GetCurrentProcess(),&handle.value,0,FALSE,DUPLICATE_SAME_ACCESS))return FL_INVALID;
        static auto query=reinterpret_cast<QuerySection>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySection"));
        if(!query)return FL_FORMAT_UNSUPPORTED;
        SectionBasic info{};
        if(query(handle.value,0,&info,sizeof(info),nullptr)<0||
           info.bytes.QuadPart<=0||uint64_t(info.bytes.QuadPart)!=d.allocationBytes||
           info.attributes!=SEC_COMMIT)return FL_INVALID;
        void* read=MapViewOfFile(handle.value,FILE_MAP_READ,0,0,SIZE_T(d.allocationBytes));
        if(!read)return FL_ACCESS_DENIED;
        UnmapViewOfFile(read);
        // Probe the kernel's granted access, not an untrusted descriptor flag.
        void* write=MapViewOfFile(handle.value,FILE_MAP_WRITE,0,0,SIZE_T(d.allocationBytes));
        const DWORD error=write?ERROR_SUCCESS:GetLastError();
        if(!write&&error!=ERROR_ACCESS_DENIED)return error==ERROR_NOT_ENOUGH_MEMORY?FL_OOM:FL_INTERNAL;
        const bool writable=write!=nullptr;if(write)UnmapViewOfFile(write);
        try {
            auto state=std::make_shared<State>();state->descriptor=d;state->writable=writable;
            state->handle.value=handle.value;handle.value=nullptr;
            out.state_=std::move(state);return FL_OK;
        }catch(const std::bad_alloc&){return FL_OOM;}
    }
    static flResult create(const flSharedCpuDescriptor& d,SharedCpuAllocationWin& out){
        auto valid=checked(d);if(valid!=FL_OK)return valid;
        Handle handle;handle.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE|SEC_COMMIT,
             DWORD(d.allocationBytes>>32),DWORD(d.allocationBytes),nullptr);
        if(!handle.value)return GetLastError()==ERROR_NOT_ENOUGH_MEMORY?FL_OOM:FL_INTERNAL;
        return importHandle(d,handle.value,d.generation,d.allocationId,out);
    }
    // Duplicate without inheritance, with precisely query+read[+write] access.
    // Destination must be authenticated separately; caller owns returned handle
    // in that process. Local exports use GetCurrentProcess().
    flResult exportHandle(bool writable,HANDLE destination,HANDLE& out)const{
        auto state=state_;if(!state||!destination)return FL_INVALID;
        if(writable&&!state->writable)return FL_ACCESS_DENIED;
        HANDLE handle=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),state->handle.value,destination,&handle,
             SECTION_QUERY|SECTION_MAP_READ|(writable?SECTION_MAP_WRITE:0),FALSE,0))return FL_ACCESS_DENIED;
        out=handle;return FL_OK;
    }
    flResult map(bool writable,Mapping& out)const{
        auto state=state_;if(!state)return FL_INVALID;
        if(writable&&!state->writable)return FL_ACCESS_DENIED;
        std::lock_guard<std::mutex> lock(state->mutex);
        auto& base=state->maps[writable?1:0];
        if(!base){base=MapViewOfFile(state->handle.value,writable?FILE_MAP_WRITE:FILE_MAP_READ,0,0,SIZE_T(state->descriptor.allocationBytes));
            if(!base)return GetLastError()==ERROR_NOT_ENOUGH_MEMORY?FL_OOM:FL_ACCESS_DENIED;}
        Mapping result;result.owner_=state;auto& v=result.view_;const auto&d=state->descriptor;v.planes=d.planeCount;
        for(unsigned p=0;p<v.planes;p++)v.plane[p]={static_cast<char*>(base)+d.planeOffset[p],d.planeStride[p],d.width*(d.format==FL_FORMAT_NV12?1u:4u),p?d.height/2:d.height};
        out=std::move(result);return FL_OK;
    }
};
}

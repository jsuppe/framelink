#pragma once
// Windows atomic pool adoption building block. The control transport must
// authenticate the peer and explicitly negotiate shared CPU before calling this.
// This does NOT implement live slot recycling or native GPU interoperability.
#include "fl_shared_cpu_allocation_win.h"
#include "fl_shared_cpu_pool.h"
#include <array>
namespace fl {
class SharedCpuImportedPoolWin {
    std::array<SharedCpuAllocationWin,FL_MAX_POOL_SIZE> images_;
    flSessionConfig session_{};
    size_t count_=0;
    bool producer_=false;
    using Identity=SharedCpuAllocationWin::Identity;
    // Bounded retirement accounting; mappings, not this list, retain storage.
    static constexpr size_t MaxLiveAllocations=256;
    std::array<Identity,MaxLiveAllocations> history_;
    size_t historyCount_=0;
public:
    SharedCpuImportedPoolWin()=default;
    SharedCpuImportedPoolWin(const SharedCpuImportedPoolWin&)=delete;
    SharedCpuImportedPoolWin& operator=(const SharedCpuImportedPoolWin&)=delete;
    SharedCpuImportedPoolWin(SharedCpuImportedPoolWin&& other)noexcept{*this=std::move(other);}
    SharedCpuImportedPoolWin& operator=(SharedCpuImportedPoolWin&& other)noexcept{
        if(this!=&other){images_=std::move(other.images_);session_=other.session_;
            count_=other.count_;producer_=other.producer_;history_=std::move(other.history_);
            historyCount_=other.historyCount_;other.count_=other.historyCount_=0;}
        return *this;
    }
    size_t size()const{return count_;}
    uint64_t generation()const{return count_?session_.generation:0;}
    // Borrow exactly count process-local section handles. Stage every checked allocation before replacing
    // out. Existing Mapping owners remain alive across a successful replacement.
    // Caller serializes adoption/map and arbitrates frame leases separately.
    static flResult adopt(const flSessionConfig& session,
        const flSharedCpuDescriptor* desc,const HANDLE* handles,size_t count,
        const flMemoryRequirements* requirements,const uint32_t* operations,
        bool producer,SharedCpuImportedPoolWin& out) {
        if(!handles)return FL_INVALID;
        flMemoryDecision decisions[2]{};
        auto policy=validateSharedCpuPool(&session,desc,count,requirements,operations,decisions);
        if(policy!=FL_MEMORY_OK)return policy==FL_MEMORY_VERSION_MISMATCH?FL_VERSION_MISMATCH:FL_INVALID;
        if(out.count_){
            if(producer!=out.producer_||flSessionValidateReplacement(&out.session_,&session)!=FL_SESSION_OK)
                return FL_INVALID;
        }
        SharedCpuImportedPoolWin next;
        for(size_t j=0;j<out.historyCount_;++j)
            if(!out.history_[j].expired())next.history_[next.historyCount_++]=out.history_[j];
        if(next.historyCount_+count>MaxLiveAllocations)return FL_BUSY;
        for(size_t i=0;i<count;++i){
            SharedCpuAllocationWin imported;
            auto r=SharedCpuAllocationWin::importHandle(desc[i],handles[i],session.generation,desc[i].allocationId,imported);
            if(r!=FL_OK)return r;
            if(!producer){
                // Receiving RW handles never grants the consumer write access.
                // Duplicate reduced rights and retain only that capability.
                HANDLE ro=nullptr;r=imported.exportHandle(false,GetCurrentProcess(),ro);if(r!=FL_OK)return r;
                r=SharedCpuAllocationWin::importHandle(desc[i],ro,session.generation,desc[i].allocationId,next.images_[i]);
                CloseHandle(ro);if(r!=FL_OK)return r;
            }else next.images_[i]=std::move(imported);
            // Includes earlier slots in this offer and all retained generations.
            // Kernel-object comparison rejects aliases even under different IDs.
            for(size_t j=0;j<next.historyCount_;++j){
                bool same=false;r=next.images_[i].sameStorage(next.history_[j],same);
                if(r!=FL_OK)return r;if(same)return FL_INVALID;
            }
            next.history_[next.historyCount_++]=next.images_[i].identity();
            SharedCpuAllocationWin::Mapping probe;
            r=next.images_[i].map(producer,probe);if(r!=FL_OK)return r;
        }
        next.session_=session;next.count_=count;next.producer_=producer;
        out=std::move(next);return FL_OK;
    }
    flResult map(size_t slot,SharedCpuAllocationWin::Mapping& out)const{
        if(slot>=count_)return FL_INVALID;
        return images_[slot].map(producer_,out);
    }
};
}

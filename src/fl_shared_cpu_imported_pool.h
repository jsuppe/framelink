#pragma once
// Linux/Android atomic pool adoption building block. The control transport must
// authenticate the peer and explicitly negotiate shared CPU before calling this.
// This does NOT implement live slot recycling or native GPU interoperability.
#include "fl_shared_cpu_allocation.h"
#include "fl_shared_cpu_pool.h"
#include <array>
namespace fl {
class SharedCpuImportedPool {
    std::array<SharedCpuAllocation,FL_MAX_POOL_SIZE> images_;
    flSessionConfig session_{};
    size_t count_=0;
    bool producer_=false;
    struct Identity {dev_t device{};ino_t inode{};std::weak_ptr<void> lifetime;};
    // Bounded retirement accounting; mappings, not this list, retain storage.
    static constexpr size_t MaxLiveAllocations=256;
    std::array<Identity,MaxLiveAllocations> history_;
    size_t historyCount_=0;
public:
    SharedCpuImportedPool()=default;
    SharedCpuImportedPool(const SharedCpuImportedPool&)=delete;
    SharedCpuImportedPool& operator=(const SharedCpuImportedPool&)=delete;
    SharedCpuImportedPool(SharedCpuImportedPool&& other)noexcept{*this=std::move(other);}
    SharedCpuImportedPool& operator=(SharedCpuImportedPool&& other)noexcept{
        if(this!=&other){images_=std::move(other.images_);session_=other.session_;
            count_=other.count_;producer_=other.producer_;history_=std::move(other.history_);
            historyCount_=other.historyCount_;other.count_=other.historyCount_=0;}
        return *this;
    }
    size_t size()const{return count_;}
    uint64_t generation()const{return count_?session_.generation:0;}
    // Borrow exactly count FDs. Stage every checked allocation before replacing
    // out. Existing Mapping owners remain alive across a successful replacement.
    // Caller serializes adoption/map and arbitrates frame leases separately.
    static flResult adopt(const flSessionConfig& session,
        const flSharedCpuDescriptor* desc,const int* fds,size_t count,
        const flMemoryRequirements* requirements,const uint32_t* operations,
        bool producer,SharedCpuImportedPool& out) {
        if(!fds)return FL_INVALID;
        flMemoryDecision decisions[2]{};
        auto policy=validateSharedCpuPool(&session,desc,count,requirements,operations,decisions);
        if(policy!=FL_MEMORY_OK)return policy==FL_MEMORY_VERSION_MISMATCH?FL_VERSION_MISMATCH:FL_INVALID;
        if(out.count_){
            if(producer!=out.producer_||flSessionValidateReplacement(&out.session_,&session)!=FL_SESSION_OK)
                return FL_INVALID;
        }
        SharedCpuImportedPool next;
        for(size_t j=0;j<out.historyCount_;++j)
            if(!out.history_[j].lifetime.expired())next.history_[next.historyCount_++]=out.history_[j];
        if(next.historyCount_+count>MaxLiveAllocations)return FL_BUSY;
        std::array<dev_t,FL_MAX_POOL_SIZE> devices{};
        std::array<ino_t,FL_MAX_POOL_SIZE> inodes{};
        for(size_t i=0;i<count;++i){
            SharedCpuAllocation imported;
            auto r=SharedCpuAllocation::importFd(desc[i],fds[i],session.generation,desc[i].allocationId,imported);
            if(r!=FL_OK)return r;
            if(!producer){
                // Receiving RW handles never grants the consumer write access.
                // Reopen genuinely RO and retain only that capability.
                int ro=-1;r=imported.exportFd(false,ro);if(r!=FL_OK)return r;
                r=SharedCpuAllocation::importFd(desc[i],ro,session.generation,desc[i].allocationId,next.images_[i]);
                close(ro);if(r!=FL_OK)return r;
            }else next.images_[i]=std::move(imported);
            r=next.images_[i].identity(devices[i],inodes[i]);if(r!=FL_OK)return r;
            for(size_t j=0;j<i;++j)
                if(devices[i]==devices[j]&&inodes[i]==inodes[j])return FL_INVALID;
            // Reject reuse of ANY storage still in the current or retained retired generations. Held
            // old mappings must not become writable slots of the new generation.
            for(size_t j=0;j<next.historyCount_;++j)
                if(next.history_[j].device==devices[i]&&next.history_[j].inode==inodes[i])return FL_INVALID;
            next.history_[next.historyCount_++]={devices[i],inodes[i],next.images_[i].lifetime()};
            SharedCpuAllocation::Mapping probe;
            r=next.images_[i].map(producer,probe);if(r!=FL_OK)return r;
        }
        next.session_=session;next.count_=count;next.producer_=producer;
        out=std::move(next);return FL_OK;
    }
    flResult map(size_t slot,SharedCpuAllocation::Mapping& out)const{
        if(slot>=count_)return FL_INVALID;
        return images_[slot].map(producer_,out);
    }
};
}

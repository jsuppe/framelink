#pragma once
// Explicit Linux/Android setup transport. Not selected by legacy factories.
// The broker authenticates/delegates the socket before construction. A successful
// setup is not a live frame lease. Acceptance is a peer protocol assertion.
#include "fl_ipc.h"
#include "fl_metadata.hpp"
#include "fl_shared_cpu_imported_pool.h"
#include <array>
#include <cstring>
#include <sys/socket.h>
namespace fl {
class SharedCpuControl {
    Channel link_;
    flSessionConfig session_{};
    flMemoryRequirements requirements_[2]{};
    uint32_t operations_[2]{};
    bool producer_=false,agreed_=false,poolExchanged_=false,accepted_=false;
    bool metadata_=false,eosEnabled_=false,ended_=false;
    size_t poolCount_=0;
    uint64_t lastReady_=0;
    std::array<uint64_t,FL_MAX_POOL_SIZE> slots_{};
    static constexpr size_t HelloSize=256,PoolHeader=16;
    static void put(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=uint8_t(x>>(i*8));}
    static uint32_t get(const uint8_t*p){uint32_t x=0;for(unsigned i=0;i<4;i++)x|=uint32_t(p[i])<<(i*8);return x;}
    static void put64(uint8_t*p,uint64_t x){put(p,uint32_t(x));put(p+4,uint32_t(x>>32));}
    static uint64_t get64(const uint8_t*p){return uint64_t(get(p))|(uint64_t(get(p+4))<<32);}
    void disconnect(){if(link_.pollFd()>=0)shutdown(link_.pollFd(),SHUT_RDWR);}
    flResult fail(flResult r=FL_INVALID){disconnect();link_=Channel{};agreed_=false;accepted_=false;return r;}
    struct Received {
        std::optional<Message> message;
        ~Received(){if(message)for(int fd:message->fds)close(fd);}
    };
public:
    explicit SharedCpuControl(Channel&& link):link_(std::move(link)){}
    // A coordinator may retain a duplicate solely for cancellation. Closing or
    // failing the owner must still disconnect immediately, not wait for that FD.
    ~SharedCpuControl(){disconnect();}
    int pollFd()const{return link_.pollFd();}
    bool agreed()const{return agreed_;}
    bool poolAccepted()const{return accepted_;}
    bool writableSlot(uint32_t slot)const{return accepted_&&producer_&&slot<poolCount_&&!slots_[slot]&&!ended_;}
    // Both ends send then receive a bounded offer. No handles may accompany it.
    // Session bytes bind all identities, timestamps, queues and negotiated formats.
    // Caller has already validated selection against both real session offers.
    flResult negotiate(bool producer,const flSessionConfig& session,
        const flMemoryRequirements& local,uint32_t operations,bool metadata=false,bool eos=false){
        if(agreed_||poolExchanged_||!link_.connected())return fail();
        std::array<uint8_t,HelloSize> offer{};
        if(!session.videoTrackId||flSessionEncode(&session,offer.data()+32,160)!=FL_SESSION_OK||
           flMemoryRequirementsEncode(&local,offer.data()+192,64)!=FL_MEMORY_OK||
           (operations&~FL_MEMORY_COPY))return fail();
        put(offer.data(),0x31484346); // FCH1
        put(offer.data()+4,1);put(offer.data()+8,producer?1:2);put(offer.data()+12,operations);put(offer.data()+16,(metadata?2u:0u)|(eos?4u:0u));
        if(!link_.send(MsgType::SharedCpuHello,offer.data(),offer.size()))return fail(FL_DISCONNECTED);
        Received received{link_.recv()};if(!received.message)return fail(FL_DISCONNECTED);
        auto&m=*received.message;
        if(m.type!=MsgType::SharedCpuHello||m.payload.size()!=HelloSize||!m.fds.empty())return fail();
        const auto*p=m.payload.data();
        if(get(p)!=0x31484346||get(p+4)!=1||get(p+8)!=(producer?2u:1u)||get(p+12)&~FL_MEMORY_COPY)return fail();
        if(get(p+16)!=((metadata?2u:0u)|(eos?4u:0u)))return fail();
        for(unsigned i=20;i<32;i++)if(p[i])return fail();
        if(std::memcmp(p+32,offer.data()+32,160))return fail();
        flMemoryRequirements peer{};
        if(flMemoryRequirementsDecode(p+192,64,&peer)!=FL_MEMORY_OK)return fail();
        flMemoryRequirements req[2]={producer?local:peer,producer?peer:local};
        uint32_t ops[2]={producer?operations:get(p+12),producer?get(p+12):operations};
        flMemoryPath paths[2]={{FL_MEMORY_VERSION,FL_MEMORY_SHARED_CPU,FL_MEMORY_CPU_WRITE,ops[0],{}},
                               {FL_MEMORY_VERSION,FL_MEMORY_SHARED_CPU,FL_MEMORY_CPU_READ,ops[1],{}}};
        flMemoryDecision decisions[2]{};
        if(flMemoryEvaluateBoundary(req,paths,2,decisions)!=FL_MEMORY_OK)return fail(FL_ACCESS_DENIED);
        session_=session;producer_=producer;metadata_=metadata;eosEnabled_=eos;
        for(unsigned i=0;i<2;i++){requirements_[i]=req[i];operations_[i]=ops[i];}
        agreed_=true;return FL_OK;
    }
    // Consumer owns allocations; producer receives writable FDs. Only setup is
    // implemented here. Do not recycle storage without the frame-lease protocol.
    flResult sendPool(const flSharedCpuDescriptor* desc,const int* fds,size_t count){
        if(!agreed_||producer_||poolExchanged_||!fds)return fail();
        flMemoryDecision decisions[2]{};
        if(validateSharedCpuPool(&session_,desc,count,requirements_,operations_,decisions)!=FL_MEMORY_OK)return fail();
        std::array<uint8_t,PoolHeader+FL_MAX_POOL_SIZE*FL_SHARED_CPU_WIRE_SIZE> wire{};
        put(wire.data(),0x31504346);put(wire.data()+4,1);put(wire.data()+8,uint32_t(count));
        for(size_t i=0;i<count;i++){
            if(fds[i]<0||flSharedCpuDescriptorEncode(&desc[i],wire.data()+PoolHeader+i*128,128)!=FL_MEMORY_OK)return fail();
        }
        if(!link_.sendWithFds(MsgType::SharedCpuPool,wire.data(),PoolHeader+count*128,fds,count))return fail(FL_DISCONNECTED);
        poolExchanged_=true;
        Received received{link_.recv()};if(!received.message)return fail(FL_DISCONNECTED);
        const auto&m=*received.message;
        if(m.type!=MsgType::SharedCpuAccepted||m.payload.size()!=24||!m.fds.empty())return fail();
        const auto*p=m.payload.data();
        if(get(p)!=0x31414346||get(p+4)!=1||get(p+8)!=uint32_t(session_.generation)||
           get(p+12)!=uint32_t(session_.generation>>32)||get(p+16)!=count||get(p+20))return fail();
        poolCount_=count;accepted_=true;return FL_OK;
    }
    // Import publishes out before the ACK send. If that send fails, out retains
    // the newly imported storage but this connection is failed/not accepted.
    // Caller must not publish any frames on failure; mappings remain safe owners.
    flResult receivePool(SharedCpuImportedPool& out){
        if(!agreed_||!producer_||poolExchanged_)return fail();
        Received received{link_.recv()};if(!received.message)return fail(FL_DISCONNECTED);
        auto&m=*received.message;
        if(m.type!=MsgType::SharedCpuPool||m.payload.size()<PoolHeader)return fail();
        auto*p=m.payload.data();const size_t count=get(p+8);
        if(get(p)!=0x31504346||get(p+4)!=1||get(p+12)||count<2||count>FL_MAX_POOL_SIZE||
           m.payload.size()!=PoolHeader+count*128||m.fds.size()!=count)return fail();
        std::array<flSharedCpuDescriptor,FL_MAX_POOL_SIZE> desc{};
        for(size_t i=0;i<count;i++)
            if(flSharedCpuDescriptorDecode(p+PoolHeader+i*128,128,&desc[i])!=FL_MEMORY_OK)return fail();
        auto r=SharedCpuImportedPool::adopt(session_,desc.data(),m.fds.data(),count,requirements_,operations_,true,out);
        if(r!=FL_OK)return fail(r);
        poolExchanged_=true;
        std::array<uint8_t,24> ack{};put(ack.data(),0x31414346);put(ack.data()+4,1);
        put(ack.data()+8,uint32_t(session_.generation));put(ack.data()+12,uint32_t(session_.generation>>32));
        put(ack.data()+16,uint32_t(count));
        if(!link_.send(MsgType::SharedCpuAccepted,ack.data(),ack.size()))return fail(FL_DISCONNECTED);
        poolCount_=count;accepted_=true;return FL_OK;
    }
    struct Frame {uint64_t sequence=0;int64_t ptsNs=0;uint32_t slot=0;flFrameMetadata metadata{};};
private:
    flResult sendFrameMessage(MsgType type,uint32_t magic,const Frame& frame){
        const bool extended=metadata_&&type==MsgType::SharedCpuReady;
        std::array<uint8_t,80> wire{};
        if(extended){type=MsgType::SharedCpuReadyMetadata;magic=0x314d4346;}
        put(wire.data(),magic);put(wire.data()+4,1);
        put64(wire.data()+8,session_.generation);put64(wire.data()+16,frame.sequence);
        put64(wire.data()+24,uint64_t(frame.ptsNs));put(wire.data()+32,frame.slot);
        if(extended){const auto&m=frame.metadata;put(wire.data()+40,m.version);put(wire.data()+44,m.flags);
            put64(wire.data()+48,uint64_t(m.dtsNs));put64(wire.data()+56,m.durationNs);put64(wire.data()+64,m.streamGeneration);}
        return link_.send(type,wire.data(),extended?80:40)?FL_OK:fail(FL_DISCONNECTED);
    }
    flResult receiveFrameMessage(MsgType type,uint32_t magic,Frame& out,bool* eos=nullptr){
        Received received{link_.recv()};if(!received.message)return fail(FL_DISCONNECTED);
        const auto&m=*received.message;
        if(m.type==MsgType::SharedCpuEos&&eos&&eosEnabled_){
            if(m.payload.size()!=32||!m.fds.empty())return fail();
            const auto*p=m.payload.data();
            if(get(p)!=0x31454346||get(p+4)!=1||get64(p+8)!=session_.generation||
               get64(p+16)!=lastReady_||get64(p+24))return fail();
            ended_=true;*eos=true;return FL_OK;
        }
        const bool extended=metadata_&&type==MsgType::SharedCpuReady;
        if(extended){type=MsgType::SharedCpuReadyMetadata;magic=0x314d4346;}
        if(m.type!=type||m.payload.size()!=(extended?80u:40u)||!m.fds.empty())return fail();
        const auto*p=m.payload.data();
        if(get(p)!=magic||get(p+4)!=1||get64(p+8)!=session_.generation||get(p+36)||
           !get64(p+16)||get64(p+24)>uint64_t(INT64_MAX)||get(p+32)>=poolCount_)return fail();
        Frame frame{get64(p+16),int64_t(get64(p+24)),get(p+32)};
        if(extended){auto&meta=frame.metadata;meta.version=get(p+40);meta.flags=get(p+44);
            if(get64(p+48)>uint64_t(INT64_MAX))return fail();
            meta.dtsNs=int64_t(get64(p+48));meta.durationNs=get64(p+56);meta.streamGeneration=get64(p+64);
            meta.reserved[0]=get(p+72);meta.reserved[1]=get(p+76);
            if(validateMetadata(meta)!=FL_OK)return fail();}
        out=frame;return FL_OK;
    }
public:
    // Serialized CPU-only protocol. Before submit, caller finishes all writes;
    // before release, caller finishes ALL reads and retires any borrowed views.
    // This does not make raw mapped pointers enforceable access leases.
    flResult submit(const Frame& frame){
        if(ended_)return FL_INVALID;
        if(!accepted_||!producer_||frame.slot>=poolCount_||!frame.sequence||
           frame.sequence<=lastReady_||frame.ptsNs<0)return fail();
        if(metadata_?validateMetadata(frame.metadata)!=FL_OK:
           (frame.metadata.version||frame.metadata.flags||frame.metadata.dtsNs||frame.metadata.durationNs||frame.metadata.streamGeneration||frame.metadata.reserved[0]||frame.metadata.reserved[1]))return fail();
        if(slots_[frame.slot])return FL_BUSY; // no implicit overwrite/drop
        auto r=sendFrameMessage(MsgType::SharedCpuReady,0x31464346,frame);
        if(r!=FL_OK)return r;
        slots_[frame.slot]=frame.sequence;lastReady_=frame.sequence;return FL_OK;
    }
    flResult sendEos(){
        if(!accepted_||!producer_||!eosEnabled_||ended_)return FL_INVALID;
        std::array<uint8_t,32> wire{};put(wire.data(),0x31454346);put(wire.data()+4,1);
        put64(wire.data()+8,session_.generation);put64(wire.data()+16,lastReady_);
        if(!link_.send(MsgType::SharedCpuEos,wire.data(),wire.size()))return fail(FL_DISCONNECTED);
        ended_=true;return FL_OK;
    }
    flResult receiveFrame(Frame& out,bool* eos=nullptr){
        if(!accepted_||producer_)return fail();
        if(ended_){if(eos){*eos=true;return FL_OK;}return FL_INVALID;}
        bool end=false;
        Frame frame;auto r=receiveFrameMessage(MsgType::SharedCpuReady,0x31464346,frame,eos?&end:nullptr);if(r!=FL_OK)return r;
        if(end){*eos=true;return FL_OK;}
        if(frame.sequence<=lastReady_||slots_[frame.slot])return fail();
        slots_[frame.slot]=frame.sequence;lastReady_=frame.sequence;out=frame;if(eos)*eos=false;return FL_OK;
    }
    flResult release(uint32_t slot,uint64_t sequence){
        if(!accepted_||producer_||slot>=poolCount_||!sequence||slots_[slot]!=sequence)return fail();
        auto r=sendFrameMessage(MsgType::SharedCpuReleased,0x31524346,{sequence,0,slot});
        if(r!=FL_OK)return r;
        slots_[slot]=0;return FL_OK;
    }
    flResult receiveRelease(Frame& out){
        if(!accepted_||!producer_)return fail();
        Frame frame;auto r=receiveFrameMessage(MsgType::SharedCpuReleased,0x31524346,frame);if(r!=FL_OK)return r;
        if(frame.ptsNs||slots_[frame.slot]!=frame.sequence)return fail();
        slots_[frame.slot]=0;out=frame;return FL_OK;
    }
};
}

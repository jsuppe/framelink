#pragma once
// Explicit Linux/Android setup transport. Not selected by legacy factories.
// The broker authenticates/delegates the socket before construction. A successful
// setup is not a live frame lease. Acceptance is a peer protocol assertion.
#include "fl_ipc.h"
#include "fl_shared_cpu_imported_pool.h"
#include <array>
#include <cstring>
namespace fl {
class SharedCpuControl {
    Channel link_;
    flSessionConfig session_{};
    flMemoryRequirements requirements_[2]{};
    uint32_t operations_[2]{};
    bool producer_=false,agreed_=false,poolExchanged_=false,accepted_=false;
    static constexpr size_t HelloSize=256,PoolHeader=16;
    static void put(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=uint8_t(x>>(i*8));}
    static uint32_t get(const uint8_t*p){uint32_t x=0;for(unsigned i=0;i<4;i++)x|=uint32_t(p[i])<<(i*8);return x;}
    flResult fail(flResult r=FL_INVALID){link_=Channel{};agreed_=false;accepted_=false;return r;}
    struct Received {
        std::optional<Message> message;
        ~Received(){if(message)for(int fd:message->fds)close(fd);}
    };
public:
    explicit SharedCpuControl(Channel&& link):link_(std::move(link)){}
    bool agreed()const{return agreed_;}
    bool poolAccepted()const{return accepted_;}
    // Both ends send then receive a bounded offer. No handles may accompany it.
    // Session bytes bind all identities, timestamps, queues and negotiated formats.
    // Caller has already validated selection against both real session offers.
    flResult negotiate(bool producer,const flSessionConfig& session,
        const flMemoryRequirements& local,uint32_t operations){
        if(agreed_||poolExchanged_||!link_.connected())return fail();
        std::array<uint8_t,HelloSize> offer{};
        if(!session.videoTrackId||flSessionEncode(&session,offer.data()+32,160)!=FL_SESSION_OK||
           flMemoryRequirementsEncode(&local,offer.data()+192,64)!=FL_MEMORY_OK||
           (operations&~FL_MEMORY_COPY))return fail();
        put(offer.data(),0x31484346); // FCH1
        put(offer.data()+4,1);put(offer.data()+8,producer?1:2);put(offer.data()+12,operations);
        if(!link_.send(MsgType::SharedCpuHello,offer.data(),offer.size()))return fail(FL_DISCONNECTED);
        Received received{link_.recv()};if(!received.message)return fail(FL_DISCONNECTED);
        auto&m=*received.message;
        if(m.type!=MsgType::SharedCpuHello||m.payload.size()!=HelloSize||!m.fds.empty())return fail();
        const auto*p=m.payload.data();
        if(get(p)!=0x31484346||get(p+4)!=1||get(p+8)!=(producer?2u:1u)||get(p+12)&~FL_MEMORY_COPY)return fail();
        for(unsigned i=16;i<32;i++)if(p[i])return fail();
        if(std::memcmp(p+32,offer.data()+32,160))return fail();
        flMemoryRequirements peer{};
        if(flMemoryRequirementsDecode(p+192,64,&peer)!=FL_MEMORY_OK)return fail();
        flMemoryRequirements req[2]={producer?local:peer,producer?peer:local};
        uint32_t ops[2]={producer?operations:get(p+12),producer?get(p+12):operations};
        flMemoryPath paths[2]={{FL_MEMORY_VERSION,FL_MEMORY_SHARED_CPU,FL_MEMORY_CPU_WRITE,ops[0],{}},
                               {FL_MEMORY_VERSION,FL_MEMORY_SHARED_CPU,FL_MEMORY_CPU_READ,ops[1],{}}};
        flMemoryDecision decisions[2]{};
        if(flMemoryEvaluateBoundary(req,paths,2,decisions)!=FL_MEMORY_OK)return fail(FL_ACCESS_DENIED);
        session_=session;producer_=producer;
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
        accepted_=true;return FL_OK;
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
        accepted_=true;return FL_OK;
    }
};
}

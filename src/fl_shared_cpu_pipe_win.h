#pragma once
// Internal CPU transport prerequisite. The broker authenticates the connected
// message pipe and supplies a retained peer process handle. No discovery, handle
// transfer, pool negotiation or public CPU channel is implied by adoption.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "fl_ipc.h"
#include <array>
#include <memory>
#include <cstring>
namespace fl {
class SharedCpuPipeWin {
    struct Handle {
        HANDLE value=nullptr;
        ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    };
    struct State {
        Handle pipe,peer,cancel,event;
        DWORD timeout=0;
    };
    std::unique_ptr<State> state_;
    // FileModeInformation exposes whether the supplied handle was opened for
    // synchronous I/O. Passing OVERLAPPED to such a handle could block forever.
    struct IoStatus {union {LONG status;void* pointer;};ULONG_PTR information;};
    using QueryFile=LONG(NTAPI*)(HANDLE,IoStatus*,void*,ULONG,ULONG);
    flResult io(bool write,void* bytes,DWORD size,DWORD& transferred){
        if(!state_)return FL_DISCONNECTED;
        auto& s=*state_;
        if(WaitForSingleObject(s.peer.value,0)!=WAIT_TIMEOUT||
           (s.cancel.value&&WaitForSingleObject(s.cancel.value,0)!=WAIT_TIMEOUT)){
            state_.reset();return FL_DISCONNECTED;
        }
        if(!ResetEvent(s.event.value)){state_.reset();return FL_INTERNAL;}
        OVERLAPPED overlapped{};overlapped.hEvent=s.event.value;
        BOOL ok=write?WriteFile(s.pipe.value,bytes,size,&transferred,&overlapped):
                      ReadFile(s.pipe.value,bytes,size,&transferred,&overlapped);
        if(ok)return FL_OK;
        if(GetLastError()!=ERROR_IO_PENDING){state_.reset();return FL_DISCONNECTED;}
        HANDLE events[3]={s.event.value,s.peer.value,s.cancel.value};
        DWORD result=WaitForMultipleObjects(s.cancel.value?3:2,events,FALSE,s.timeout);
        if(result!=WAIT_OBJECT_0){
            // Drain cancellation BEFORE destroying the stack OVERLAPPED/buffer.
            // No new operation is issued once the deadline/cancel has fired.
            CancelIoEx(s.pipe.value,&overlapped);
            GetOverlappedResult(s.pipe.value,&overlapped,&transferred,TRUE);
            state_.reset();return result==WAIT_TIMEOUT?FL_TIMEOUT:FL_DISCONNECTED;
        }
        if(!GetOverlappedResult(s.pipe.value,&overlapped,&transferred,FALSE)){
            state_.reset();return FL_DISCONNECTED;
        }
        return FL_OK;
    }
public:
    SharedCpuPipeWin()=default;
    SharedCpuPipeWin(SharedCpuPipeWin&&)=default;
    SharedCpuPipeWin& operator=(SharedCpuPipeWin&&)=default;
    SharedCpuPipeWin(const SharedCpuPipeWin&)=delete;
    SharedCpuPipeWin& operator=(const SharedCpuPipeWin&)=delete;
    bool connected()const{return bool(state_);}
    void close(){state_.reset();}
    // Consumes input on EVERY path. Borrows then duplicates peer/cancel handles.
    // Broker guarantees the pipe is local and authenticated; PID equality alone
    // is not authorization. Stable process handle prevents PID-reuse confusion.
    // All operations are externally serialized; signal cancellation from another
    // thread, never concurrently destroy this object while it owns pending I/O.
    static flResult adoptConnected(HANDLE input,HANDLE peer,HANDLE cancellation,
                                   uint32_t timeoutMs,SharedCpuPipeWin& out){
        Handle pipe;pipe.value=input;
        if(!input||input==INVALID_HANDLE_VALUE||!peer||!timeoutMs||timeoutMs>60000)return FL_INVALID;
        if(GetFileType(input)!=FILE_TYPE_PIPE)return FL_INVALID;
        DWORD flags=0,mode=0;
        if(!GetNamedPipeInfo(input,&flags,nullptr,nullptr,nullptr)||!(flags&PIPE_TYPE_MESSAGE)||
           !GetNamedPipeHandleStateW(input,&mode,nullptr,nullptr,nullptr,nullptr,0)||!(mode&PIPE_READMODE_MESSAGE)||(mode&PIPE_NOWAIT))return FL_INVALID;
        static auto query=reinterpret_cast<QueryFile>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationFile"));
        if(!query)return FL_FORMAT_UNSUPPORTED;
        IoStatus status{};ULONG fileMode=0;
        if(query(input,&status,&fileMode,sizeof(fileMode),16)<0||(fileMode&0x30u))return FL_INVALID;
        ULONG actualPid=0;
        if(!(flags&PIPE_SERVER_END?GetNamedPipeClientProcessId(input,&actualPid):GetNamedPipeServerProcessId(input,&actualPid))||
           !actualPid||actualPid!=GetProcessId(peer)||WaitForSingleObject(peer,0)!=WAIT_TIMEOUT)return FL_INVALID;
        if(!SetHandleInformation(input,HANDLE_FLAG_INHERIT,0))return FL_INVALID;
        try{
            auto s=std::make_unique<State>();s->timeout=timeoutMs;
            if(!DuplicateHandle(GetCurrentProcess(),peer,GetCurrentProcess(),&s->peer.value,0,FALSE,DUPLICATE_SAME_ACCESS))return FL_ACCESS_DENIED;
            if(cancellation&&!DuplicateHandle(GetCurrentProcess(),cancellation,GetCurrentProcess(),&s->cancel.value,0,FALSE,DUPLICATE_SAME_ACCESS))return FL_ACCESS_DENIED;
            s->event.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!s->event.value)return FL_OOM;
            s->pipe.value=pipe.value;pipe.value=nullptr;out.state_=std::move(s);return FL_OK;
        }catch(const std::bad_alloc&){return FL_OOM;}
    }
    flResult send(MsgType type,const void* payload,size_t size){
        if(!state_)return FL_DISCONNECTED;
        if(size>65536-sizeof(MsgHeader)||(size&&!payload))return FL_INVALID;
        std::array<uint8_t,65536> wire{};MsgHeader h{kMagic,type,uint32_t(size)};
        memcpy(wire.data(),&h,sizeof(h));if(size)memcpy(wire.data()+sizeof(h),payload,size);
        DWORD transferred=0;auto r=io(true,wire.data(),DWORD(sizeof(h)+size),transferred);
        if(r!=FL_OK)return r;
        if(transferred!=sizeof(h)+size){close();return FL_DISCONNECTED;}return FL_OK;
    }
    flResult receive(Message& out){
        std::array<uint8_t,65536> wire{};DWORD transferred=0;
        auto r=io(false,wire.data(),DWORD(wire.size()),transferred);if(r!=FL_OK)return r;
        if(transferred<sizeof(MsgHeader)){close();return FL_INVALID;}
        MsgHeader h{};memcpy(&h,wire.data(),sizeof(h));
        if(h.magic!=kMagic||h.payloadSize!=transferred-sizeof(h)){close();return FL_INVALID;}
        try{
            Message next;next.type=h.type;next.payload.assign(wire.data()+sizeof(h),wire.data()+transferred);
            out=std::move(next);return FL_OK;
        }catch(const std::bad_alloc&){close();return FL_OOM;}
    }
};
}

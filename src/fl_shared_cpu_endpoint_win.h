#pragma once
// Internal local rendezvous primitive. A name/PID/DACL is NOT authentication:
// the caller must authenticate this exact connected pipe before exchanging
// handles (fl_gst_session_bind_authenticated_cpu_pipe). No discovery record or
// public factory/default changes. All returned handles are non-inheritable.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include "framelink.h"

namespace fl {
class SharedCpuEndpointWin {
    static bool validName(const std::wstring& name) {
        const std::wstring prefix=L"\\\\.\\pipe\\framelink-cpu-";
        if(name.compare(0,prefix.size(),prefix)||name.size()<=prefix.size()||name.size()>240)return false;
        for(size_t i=prefix.size();i<name.size();++i)
            if(!((name[i]>=L'a'&&name[i]<=L'z')||(name[i]>=L'A'&&name[i]<=L'Z')||
                 (name[i]>=L'0'&&name[i]<=L'9')||name[i]==L'-'))return false;
        return true;
    }
    static bool cancelled(HANDLE cancel) {
        return cancel&&WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT;
    }
    static flResult pin(HANDLE pipe,bool server,HANDLE& peer) {
        ULONG pid=0;
        if(!(server?GetNamedPipeClientProcessId(pipe,&pid):GetNamedPipeServerProcessId(pipe,&pid))||!pid)return FL_INVALID;
        peer=OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
        if(!peer)return FL_ACCESS_DENIED;
        ULONG again=0;
        if(!(server?GetNamedPipeClientProcessId(pipe,&again):GetNamedPipeServerProcessId(pipe,&again))||
           again!=pid||GetProcessId(peer)!=pid||WaitForSingleObject(peer,0)!=WAIT_TIMEOUT){
            CloseHandle(peer);peer=nullptr;return FL_DISCONNECTED;
        }
        return FL_OK;
    }
public:
    // Create first-instance, current-user-only server. Never removes/reuses an
    // existing name, including a hostile or stale endpoint. Caller owns result.
    static flResult listen(const std::wstring& name,HANDLE& server) {
        server=nullptr;if(!validName(name))return FL_INVALID;
        HANDLE token=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return FL_ACCESS_DENIED;
        DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
        std::vector<unsigned char> info(bytes);
        bool ok=bytes&&GetTokenInformation(token,TokenUser,info.data(),bytes,&bytes);
        CloseHandle(token);if(!ok)return FL_ACCESS_DENIED;
        LPWSTR sid=nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid,&sid))return FL_ACCESS_DENIED;
        std::wstring sddl=L"D:P(A;;GA;;;";sddl+=sid;sddl+=L")";LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return FL_ACCESS_DENIED;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        HANDLE h=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&attributes);
        LocalFree(descriptor);
        if(h==INVALID_HANDLE_VALUE)return FL_ACCESS_DENIED;
        server=h;return FL_OK;
    }
    // Borrows a server returned by listen() and cancel (never a synchronous
    // externally supplied pipe). Failure leaves caller responsible for closing the
    // server, with no outstanding OVERLAPPED operation. Peer output owned.
    static flResult accept(HANDLE server,HANDLE cancel,uint32_t timeout,HANDLE& peer) {
        peer=nullptr;if(!server||server==INVALID_HANDLE_VALUE||!timeout||timeout>60000)return FL_INVALID;
        if(cancelled(cancel))return FL_DISCONNECTED;
        HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event)return FL_OOM;
        OVERLAPPED ov{};ov.hEvent=event;
        BOOL connected=ConnectNamedPipe(server,&ov);DWORD error=connected?ERROR_SUCCESS:GetLastError();
        flResult result=FL_OK;
        if(!connected&&error==ERROR_IO_PENDING){
            HANDLE waits[2]={event,cancel};
            DWORD wait=WaitForMultipleObjects(cancel?2:1,waits,FALSE,timeout),bytes=0;
            if(wait!=WAIT_OBJECT_0){
                CancelIoEx(server,&ov);GetOverlappedResult(server,&ov,&bytes,TRUE);
                result=wait==WAIT_TIMEOUT?FL_TIMEOUT:FL_DISCONNECTED;
            }else if(!GetOverlappedResult(server,&ov,&bytes,FALSE))result=FL_DISCONNECTED;
        }else if(!connected&&error!=ERROR_PIPE_CONNECTED)result=FL_DISCONNECTED;
        CloseHandle(event);
        if(result==FL_OK&&cancelled(cancel))result=FL_DISCONNECTED;
        return result==FL_OK?pin(server,true,peer):result;
    }
    // Allows client-first start. Open is always OVERLAPPED and local. A busy or
    // absent server is retried only within this one deadline, cancel aware.
    static flResult connect(const std::wstring& name,HANDLE cancel,uint32_t timeout,HANDLE& client,HANDLE& peer) {
        client=peer=nullptr;if(!validName(name)||!timeout||timeout>60000)return FL_INVALID;
        const auto end=GetTickCount64()+timeout;
        for(;;){
            if(cancelled(cancel))return FL_DISCONNECTED;
            if(GetTickCount64()>=end)return FL_TIMEOUT;
            HANDLE h=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
            if(h!=INVALID_HANDLE_VALUE){
                DWORD mode=PIPE_READMODE_MESSAGE,flags=0;
                if(!GetNamedPipeInfo(h,&flags,nullptr,nullptr,nullptr)||!(flags&PIPE_TYPE_MESSAGE)||
                   !SetNamedPipeHandleState(h,&mode,nullptr,nullptr)){CloseHandle(h);return FL_INVALID;}
                auto r=pin(h,false,peer);
                if(r==FL_OK&&(cancelled(cancel)||GetTickCount64()>=end)){
                    CloseHandle(peer);peer=nullptr;r=cancelled(cancel)?FL_DISCONNECTED:FL_TIMEOUT;
                }
                if(r!=FL_OK){CloseHandle(h);return r;}
                client=h;return FL_OK;
            }
            auto error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PIPE_BUSY)return FL_ACCESS_DENIED;
            auto now=GetTickCount64();if(now>=end)return FL_TIMEOUT;
            DWORD delay=DWORD((end-now)<5?(end-now):5);
            if(cancel)WaitForSingleObject(cancel,delay);else Sleep(delay);
        }
    }
};
}

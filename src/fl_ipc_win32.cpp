// framelink control channel, Windows named pipes.
//
// Message-mode pipes, so one WriteFile is one message and a partial read is an
// error rather than a silent truncation.

#include "fl_ipc.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>

namespace fl {
namespace {

constexpr DWORD kBufferSize = 64 * 1024;

std::wstring pipePath(const std::string& name) {
    std::wstring w(name.begin(), name.end());
    return L"\\\\.\\pipe\\" + w;
}

} // namespace

Channel::~Channel() { close(); }

Channel::Channel(Channel&& o) noexcept
    : handle_(o.handle_), connected_(o.connected_), peerPid_(o.peerPid_) {
    o.handle_ = nullptr;
    o.connected_ = false;
}

Channel& Channel::operator=(Channel&& o) noexcept {
    if (this != &o) {
        close();
        handle_ = o.handle_;
        connected_ = o.connected_;
        peerPid_ = o.peerPid_;
        o.handle_ = nullptr;
        o.connected_ = false;
    }
    return *this;
}

void Channel::close() {
    if (handle_) CloseHandle((HANDLE)handle_);
    handle_ = nullptr;
    connected_ = false;
}

Channel Channel::listen(const std::string& name) {
    Channel ch;
    // Unlimited instances: a consumer re-listens for the next producer while
    // the current one is still attached.
    HANDLE pipe = CreateNamedPipeW(pipePath(name).c_str(), PIPE_ACCESS_DUPLEX,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES, kBufferSize, kBufferSize, 0,
                                   nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return ch;
    BOOL ok = ConnectNamedPipe(pipe, nullptr);
    if (!ok && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(pipe);
        return ch;
    }
    ULONG pid = 0;
    if (GetNamedPipeClientProcessId(pipe, &pid)) ch.peerPid_ = (uint32_t)pid;
    ch.handle_ = pipe;
    ch.connected_ = true;
    return ch;
}

Channel Channel::connect(const std::string& name, uint32_t timeoutMs) {
    Channel ch;
    const std::wstring path = pipePath(name);
    const DWORD deadline = GetTickCount() + timeoutMs;
    for (;;) {
        HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
            ch.handle_ = pipe;
            ch.connected_ = true;
            return ch;
        }
        // ERROR_PIPE_BUSY means the channel exists but every instance is taken;
        // anything else (typically FILE_NOT_FOUND) means no such channel, and
        // the caller turns that into FL_NOT_FOUND without retrying forever.
        if (GetLastError() != ERROR_PIPE_BUSY || GetTickCount() >= deadline) return ch;
        WaitNamedPipeW(path.c_str(), 50);
    }
}

bool Channel::sendWithFds(MsgType type, const void* payload, size_t size, const int*,
                          size_t fdCount) {
    // Windows has no fd passing; handles are duplicated into the peer and sent
    // as values inside the payload instead.
    return fdCount == 0 && send(type, payload, size);
}

bool Channel::send(MsgType type, const void* payload, size_t size) {
    if (!connected_) return false;
    if (sizeof(MsgHeader) + size > kBufferSize) return false;
    uint8_t buf[kBufferSize];
    auto* hdr = (MsgHeader*)buf;
    hdr->magic = kMagic;
    hdr->type = type;
    hdr->payloadSize = (uint32_t)size;
    if (size && payload) memcpy(buf + sizeof(MsgHeader), payload, size);
    DWORD written = 0;
    if (!WriteFile((HANDLE)handle_, buf, (DWORD)(sizeof(MsgHeader) + size), &written,
                   nullptr)) {
        connected_ = false;
        return false;
    }
    return written == sizeof(MsgHeader) + size;
}

std::optional<Message> Channel::recv() {
    if (!connected_) return std::nullopt;
    uint8_t buf[kBufferSize];
    DWORD read = 0;
    if (!ReadFile((HANDLE)handle_, buf, kBufferSize, &read, nullptr) ||
        read < sizeof(MsgHeader)) {
        connected_ = false;
        return std::nullopt;
    }
    const auto* hdr = (const MsgHeader*)buf;
    if (hdr->magic != kMagic || sizeof(MsgHeader) + hdr->payloadSize > read) {
        connected_ = false;
        return std::nullopt;
    }
    Message msg;
    msg.type = hdr->type;
    msg.payload.assign(buf + sizeof(MsgHeader), buf + sizeof(MsgHeader) + hdr->payloadSize);
    return msg;
}

} // namespace fl

#endif // _WIN32

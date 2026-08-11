// framelink control channel, POSIX (Linux, Android).
//
// Unix domain socket, and unlike the Windows side the descriptors matter: a
// dma-buf IS an fd, so the ring is delivered as SCM_RIGHTS ancillary data
// rather than as integers in the payload. That is the whole reason Message
// carries fds.

#include "fl_ipc.h"

#ifndef _WIN32

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include "fl_log.h"

namespace fl {
namespace {

constexpr size_t kBufferSize = 64 * 1024;
constexpr size_t kMaxFds = 16;

// Abstract-namespace sockets (a leading NUL) so there is no filesystem litter
// and no stale socket file to clean up after a crash - the name disappears
// with the last reference, which is exactly the lifetime a channel wants.
size_t fillAddr(sockaddr_un& addr, const std::string& name) {
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const std::string path = "framelink." + name;
    const size_t n = path.size() < sizeof(addr.sun_path) - 1 ? path.size()
                                                             : sizeof(addr.sun_path) - 2;
    memcpy(addr.sun_path + 1, path.data(), n); // sun_path[0] stays NUL
    return offsetof(sockaddr_un, sun_path) + 1 + n;
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
    if (handle_) ::close((int)(intptr_t)handle_ - 1);
    handle_ = nullptr;
    connected_ = false;
}

Channel Channel::listen(const std::string& name) {
    Channel ch;
    int srv = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (srv < 0) return ch;
    sockaddr_un addr;
    const size_t len = fillAddr(addr, name);
    if (bind(srv, (sockaddr*)&addr, (socklen_t)len) < 0 || ::listen(srv, 4) < 0) {
        ::close(srv);
        return ch;
    }
    const int fd = accept4(srv, nullptr, nullptr, SOCK_CLOEXEC);
    ::close(srv); // one producer per channel, as on Windows
    if (fd < 0) return ch;

    ucred cred{};
    socklen_t clen = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &clen) == 0) ch.peerPid_ = cred.pid;
    ch.handle_ = (void*)(intptr_t)(fd + 1); // +1 so fd 0 is not "null"
    ch.connected_ = true;
    return ch;
}

Channel Channel::connect(const std::string& name, uint32_t timeoutMs) {
    Channel ch;
    sockaddr_un addr;
    const size_t len = fillAddr(addr, name);
    const uint32_t step = 25;
    for (uint32_t waited = 0;; waited += step) {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (fd < 0) return ch;
        if (::connect(fd, (sockaddr*)&addr, (socklen_t)len) == 0) {
            ch.handle_ = (void*)(intptr_t)(fd + 1);
            ch.connected_ = true;
            return ch;
        }
        ::close(fd);
        // ECONNREFUSED/ENOENT both mean "no channel here"; the caller turns
        // that into FL_NOT_FOUND rather than waiting forever.
        if (waited >= timeoutMs) return ch;
        usleep(step * 1000);
    }
}

bool Channel::send(MsgType type, const void* payload, size_t size) {
    return sendWithFds(type, payload, size, nullptr, 0);
}

bool Channel::sendWithFds(MsgType type, const void* payload, size_t size, const int* fds,
                          size_t fdCount) {
    if (!connected_ || fdCount > kMaxFds) return false;
    if (sizeof(MsgHeader) + size > kBufferSize) return false;

    uint8_t buf[kBufferSize];
    auto* hdr = (MsgHeader*)buf;
    hdr->magic = kMagic;
    hdr->type = type;
    hdr->payloadSize = (uint32_t)size;
    if (size && payload) memcpy(buf + sizeof(MsgHeader), payload, size);

    iovec iov{buf, sizeof(MsgHeader) + size};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int) * kMaxFds)];
    if (fdCount) {
        memset(control, 0, sizeof(control));
        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * fdCount);
        cmsghdr* cm = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int) * fdCount);
        memcpy(CMSG_DATA(cm), fds, sizeof(int) * fdCount);
    }
    const ssize_t n = sendmsg((int)(intptr_t)handle_ - 1, &msg, MSG_NOSIGNAL);
    if (n < 0) {
        connected_ = false;
        return false;
    }
    return (size_t)n == sizeof(MsgHeader) + size;
}

std::optional<Message> Channel::recv() {
    if (!connected_) return std::nullopt;
    uint8_t buf[kBufferSize];
    iovec iov{buf, sizeof(buf)};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    char control[CMSG_SPACE(sizeof(int) * kMaxFds)];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    const ssize_t n = recvmsg((int)(intptr_t)handle_ - 1, &msg, MSG_CMSG_CLOEXEC);
    if (n <= 0 || (size_t)n < sizeof(MsgHeader)) {
        connected_ = false;
        return std::nullopt;
    }
    const auto* hdr = (const MsgHeader*)buf;
    if (hdr->magic != kMagic || sizeof(MsgHeader) + hdr->payloadSize > (size_t)n) {
        connected_ = false;
        return std::nullopt;
    }

    Message out;
    out.type = hdr->type;
    out.payload.assign(buf + sizeof(MsgHeader), buf + sizeof(MsgHeader) + hdr->payloadSize);
    for (cmsghdr* cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) continue;
        const size_t count = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const int* got = (const int*)CMSG_DATA(cm);
        out.fds.assign(got, got + count);
    }
    return out;
}

} // namespace fl

#endif // !_WIN32

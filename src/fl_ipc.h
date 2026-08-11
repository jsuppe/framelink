// Minimal message channel for framelink's control plane.
//
// Frames never travel through here - only the handles that name them and a
// few bytes per frame to say which slot is ready. Windows uses a named pipe;
// the POSIX implementation will need fd passing (SCM_RIGHTS) for dma-buf, which
// is why recv() carries a place for descriptors even though Windows never
// fills it.
#pragma once
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "fl_protocol.h"

namespace fl {

struct Message {
    MsgType type{};
    std::vector<uint8_t> payload;
    std::vector<int> fds; // POSIX only; empty on Windows

    template <typename T>
    const T* as() const {
        return payload.size() >= sizeof(T) ? reinterpret_cast<const T*>(payload.data())
                                           : nullptr;
    }
};

class Channel {
  public:
    Channel() = default;
    ~Channel();
    Channel(Channel&&) noexcept;
    Channel& operator=(Channel&&) noexcept;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // Blocks until a peer connects. The consumer (ring owner) listens.
    static Channel listen(const std::string& name);
    // Fails fast when the name does not exist - a producer attaches to a
    // channel or it does not; it never creates one.
    // `busy` (optional) distinguishes "the channel exists but is taken" from
    // "no such channel" - the caller turns that into FL_BUSY vs FL_NOT_FOUND.
    static Channel connect(const std::string& name, uint32_t timeoutMs, bool* busy = nullptr);

    bool send(MsgType type, const void* payload, size_t size);
    // POSIX only: the ring's dma-buf fds travel as SCM_RIGHTS ancillary data,
    // because an fd cannot be sent as a number. On Windows this is send().
    bool sendWithFds(MsgType type, const void* payload, size_t size, const int* fds,
                     size_t fdCount);
    bool send(MsgType type) { return send(type, nullptr, 0); }
    template <typename T>
    bool send(MsgType type, const T& value) {
        return send(type, &value, sizeof(T));
    }

    std::optional<Message> recv();

    bool connected() const { return connected_; }
    uint32_t peerPid() const { return peerPid_; } // listen() side only

#ifndef _WIN32
    // For poll()ing alongside other fds - the consumer's pump loop waits on
    // the producer AND the listener at once. POSIX only; Windows waits on the
    // pipe through its own machinery.
    int pollFd() const { return (int)(intptr_t)handle_ - 1; }
#endif

  private:
    void close();
    void* handle_ = nullptr; // HANDLE on Windows, int fd on POSIX
    bool connected_ = false;
    uint32_t peerPid_ = 0;

#ifndef _WIN32
    friend class Listener;
#endif
};

#ifndef _WIN32
// A listening socket that OUTLIVES individual producer connections. POSIX
// only, and it exists for one reason: Channel::listen() binds afresh per
// producer and closes the socket after one accept, so while a producer was
// attached the name did not exist - connect() got ECONNREFUSED and "taken"
// was indistinguishable from "no such channel". Slot walking needs exactly
// that distinction (FL_BUSY vs FL_NOT_FOUND), so the listener stays bound for
// the channel's whole life and extras are refused explicitly with a Busy
// message.
//
// Windows does not need this: a named pipe with one instance reports
// ERROR_PIPE_BUSY on its own.
class Listener {
  public:
    Listener() = default;
    ~Listener();
    Listener(Listener&&) noexcept;
    Listener& operator=(Listener&&) noexcept;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    static Listener create(const std::string& name);
    bool valid() const { return fd_ >= 0; }
    int pollFd() const { return fd_; }
    // Accept one connection. Blocking unless the caller poll()ed first.
    Channel accept();
    void close();

  private:
    int fd_ = -1;
};
#endif

} // namespace fl

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace av {
struct Endpoint { uint16_t port = 0; std::array<unsigned char, 32> token{};
    // Explicit FAR2 Unix record: port must be zero; name is independent of token.
    std::array<unsigned char, 16> unixName{}; };
// Local current-user trust boundary. Keep owner alive while listening. An OS
// lifetime lock allows recovery after process death without trusting PID files.
class RendezvousOwner {
public:
    RendezvousOwner();
    ~RendezvousOwner();
    RendezvousOwner(const RendezvousOwner&) = delete;
    RendezvousOwner& operator=(const RendezvousOwner&) = delete;
    bool publish(const std::string& directory, const Endpoint&, std::string& error);
    void close();
private:
    struct Impl;
    Impl* impl_ = nullptr;
};
#ifdef __linux__
// Linux/Android abstract Unix STREAM listener. Discovery remains protected by
// the same current-user record/lock as TCP. The public socket name is NOT the
// authentication credential. Accepted streams still require authentication.
// fd() is borrowed, CLOEXEC and nonblocking. No media or session setup here.
class UnixRendezvousOwner {
public:
    ~UnixRendezvousOwner();
    UnixRendezvousOwner() = default;
    UnixRendezvousOwner(const UnixRendezvousOwner&) = delete;
    UnixRendezvousOwner& operator=(const UnixRendezvousOwner&) = delete;
    bool publish(const std::string&, std::string& error);
    void close();
    int fd() const { return fd_; }
private:
    int fd_ = -1;
    RendezvousOwner record_;
};
// Exact abstract address bytes, including leading NUL; empty for TCP/invalid.
std::string unixAddress(const Endpoint&);
bool readUnixEndpoint(const std::string&, Endpoint&, std::string& error);
#endif
bool readEndpoint(const std::string& directory, Endpoint&, std::string& error);
bool secureRandom(void*, size_t, std::string& error);
}

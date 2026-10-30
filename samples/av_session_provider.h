#pragma once
#include "av_rendezvous.h"
#include <gio/gio.h>
#include <functional>

namespace av {
// Adapter for an OS-authorized broker, not an alternate authentication policy.
// Called on the playback thread. Provider outlives runPlayback; callbacks must
// obey timeoutMs. resolve obtains a fresh lease/key after every reconnect; do not
// reuse a revoked lease/generation. connect transfers one owning reference to a
// fresh connected STREAM connection, or returns null. C and A are distinct
// connections. Existing HMAC (including session/generation-derived audio key)
// is still applied by playback. A broker death callback must shutdown retained
// duplicates to wake readers; close alone does not revoke transferred handles.
// No filesystem access or loopback listener is required by this adapter.
struct SessionProvider {
  std::function<bool(Endpoint&, unsigned timeoutMs, std::string&)> resolve;
  std::function<GSocketConnection*(const Endpoint&, char role,
                                  unsigned timeoutMs, std::string&)> connect;
};
}

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
namespace av {
// Consumes fd on EVERY path, including rejection. Pass detachFd(), not getFd(),
// from ParcelFileDescriptor. Returns a full GObject reference. Only stream
// sockets can carry the byte-oriented control and GDP protocols; AHB's separate
// seqpacket socket is not accepted. Broker retains a dup only for revocation.
inline GSocketConnection* adoptConnectedStreamFd(int fd, unsigned timeoutMs,
                                                std::string& error) {
  error.clear();
  if(fd < 0) {error="invalid delegated descriptor"; return nullptr;}
  struct Owned {int fd; ~Owned(){if(fd>=0)::close(fd);}} owned{fd};
  int type=0; socklen_t length=sizeof(type);
  sockaddr_storage peer{}; socklen_t peerLength=sizeof(peer);
  if(!timeoutMs || getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&length) ||
     type!=SOCK_STREAM || getpeername(fd,reinterpret_cast<sockaddr*>(&peer),&peerLength)) {
    error="delegated descriptor must be a connected stream with a finite timeout";
    return nullptr;
  }
  int flags=fcntl(fd,F_GETFD);
  if(flags<0 || fcntl(fd,F_SETFD,flags|FD_CLOEXEC)<0) {
    error="cannot protect delegated descriptor across exec"; return nullptr;
  }
  GError* e=nullptr;
  auto* socket=g_socket_new_from_fd(fd,&e);
  if(!socket) {error=e?e->message:"cannot adopt delegated descriptor";if(e)g_error_free(e);return nullptr;}
  owned.fd=-1;
  // Same <=2 second bounded I/O policy as desktop control connections.
  g_socket_set_timeout(socket, timeoutMs>1000 ? 2 : 1);
  auto* result=g_socket_connection_factory_create_connection(socket);
  g_object_unref(socket);
  return result;
}
}
#endif

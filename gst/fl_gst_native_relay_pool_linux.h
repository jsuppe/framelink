#pragma once
// Internal explicit native relay bootstrap for desktop Linux; the counterpart
// of fl_gst_native_relay_pool_win.h. No discovery/default/API changes.
//
// Transport: an abstract AF_UNIX SOCK_SEQPACKET socket whose name is derived
// from the session secret. Abstract names carry no file permissions, so the
// name is not the credential: each side pins the kernel peer (SO_PEERCRED:
// same effective user, and the peer's PID) and both PIDs are bound into a
// mutual HMAC under a distinct domain before any fd is accepted.
//
// Protocol (one message each): receiver nonce; sender nonce + MAC; receiver
// MAC; sender OFFER (layout payload, every plane fd as SCM_RIGHTS); receiver
// imports the SAME allocations (no allocation or copy), validates the ring and
// echoes READY; sender attaches as producer, validates and echoes ATTACHED.
#include "framelink_dmabuf.h"
#include "framelink_session.h"
#include "../src/fl_render_node.h"
#include <gio/gio.h>
#include <gst/gst.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <set>
#include <string>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace flnative {
struct Fd {
 int value=-1;
 ~Fd(){if(value>=0)::close(value);}
};

// Bounded, cancellable wait for one socket condition.
class RelayIo {
public:
 RelayIo(int fd,GCancellable*cancel,gint64 end):fd_(fd),cancel_(cancel),end_(end){}
 flResult wait(short events){
  for(;;){
   if(cancel_&&g_cancellable_is_cancelled(cancel_))return FL_DISCONNECTED;
   const gint64 left=end_-g_get_monotonic_time();if(left<=0)return FL_TIMEOUT;
   pollfd p[2]{{fd_,events,0},{-1,POLLIN,0}};nfds_t n=1;
   GPollFD cancelFd{};const bool watch=cancel_&&g_cancellable_make_pollfd(cancel_,&cancelFd);
   if(watch){p[1].fd=cancelFd.fd;n=2;}
   const int ready=poll(p,n,int(std::min<gint64>((left+999)/1000,1000)));
   if(watch)g_cancellable_release_fd(cancel_);
   if(ready<0&&errno!=EINTR)return FL_DISCONNECTED;
   if(ready>0&&(p[0].revents&(events|POLLHUP|POLLERR)))return (p[0].revents&events)?FL_OK:FL_DISCONNECTED;
  }
 }
 flResult send(const void*data,size_t size,const int*fds=nullptr,size_t count=0){
  iovec io{const_cast<void*>(data),size};
  std::array<char,CMSG_SPACE(sizeof(int)*4*FL_MAX_POOL_SIZE)> ancillary{};
  msghdr msg{};msg.msg_iov=&io;msg.msg_iovlen=1;
  if(count){
   msg.msg_control=ancillary.data();msg.msg_controllen=CMSG_SPACE(sizeof(int)*count);
   auto*c=CMSG_FIRSTHDR(&msg);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;
   c->cmsg_len=CMSG_LEN(sizeof(int)*count);memcpy(CMSG_DATA(c),fds,sizeof(int)*count);
  }
  for(;;){
   auto r=wait(POLLOUT);if(r!=FL_OK)return r;
   const ssize_t n=sendmsg(fd_,&msg,MSG_DONTWAIT|MSG_NOSIGNAL);
   if(n==ssize_t(size))return FL_OK;
   if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
   return FL_DISCONNECTED;
  }
 }
 // Exactly one message of exactly size bytes; received fds go to fds (owned).
 flResult receive(void*data,size_t size,std::vector<int>*fds=nullptr){
  std::array<char,CMSG_SPACE(sizeof(int)*4*FL_MAX_POOL_SIZE)> ancillary{};
  for(;;){
   auto r=wait(POLLIN);if(r!=FL_OK)return r;
   iovec io{data,size};msghdr msg{};msg.msg_iov=&io;msg.msg_iovlen=1;
   msg.msg_control=ancillary.data();msg.msg_controllen=ancillary.size();
   const ssize_t n=recvmsg(fd_,&msg,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
   if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
   std::vector<int> received;bool valid=n==ssize_t(size)&&!(msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC));
   for(auto*c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)){
    if(c->cmsg_level!=SOL_SOCKET||c->cmsg_type!=SCM_RIGHTS||c->cmsg_len<CMSG_LEN(0)){valid=false;continue;}
    const size_t bytes=c->cmsg_len-CMSG_LEN(0);if(bytes%sizeof(int))valid=false;
    for(size_t i=0;i+sizeof(int)<=bytes;i+=sizeof(int)){int f=-1;memcpy(&f,CMSG_DATA(c)+i,sizeof f);received.push_back(f);}
   }
   if(!valid||(!fds&&!received.empty())){for(int f:received)::close(f);return n==0?FL_DISCONNECTED:FL_INVALID;}
   if(fds)*fds=std::move(received);
   return FL_OK;
  }
 }
private:
 int fd_;GCancellable*cancel_;gint64 end_;
};

// Abstract socket name for a session: secret-derived, never the credential.
inline std::string relayPoolAddress(GBytes*key,const flSessionConfig&config){
 gsize n=0;const auto*bytes=static_cast<const guint8*>(g_bytes_get_data(key,&n));
 auto*h=g_hmac_new(G_CHECKSUM_SHA256,bytes,n);
 static const char label[]="FrameLink Linux native relay pool socket v1";
 g_hmac_update(h,reinterpret_cast<const guint8*>(label),sizeof label);
 g_hmac_update(h,config.sessionId.bytes,sizeof config.sessionId.bytes);
 guint8 digest[32];gsize size=sizeof digest;g_hmac_get_digest(h,digest,&size);g_hmac_unref(h);
 std::string address(1,'\0');address+="framelink-native-relay-";
 for(unsigned i=0;i<16;i++){address+="0123456789abcdef"[digest[i]>>4];address+="0123456789abcdef"[digest[i]&15];}
 return address;
}

// Receiver: listen, then accept exactly one peer within the deadline.
inline flResult acceptRelayPool(const std::string&address,GCancellable*cancel,gint64 end,int*out){
 *out=-1;Fd listener;listener.value=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
 if(listener.value<0)return FL_INTERNAL;
 sockaddr_un addr{};addr.sun_family=AF_UNIX;
 if(address.size()>sizeof addr.sun_path)return FL_INVALID;
 memcpy(addr.sun_path,address.data(),address.size());
 if(bind(listener.value,reinterpret_cast<sockaddr*>(&addr),socklen_t(offsetof(sockaddr_un,sun_path)+address.size()))||
    listen(listener.value,1))return FL_BUSY;
 RelayIo io(listener.value,cancel,end);
 for(;;){
  auto r=io.wait(POLLIN);if(r!=FL_OK)return r;
  const int peer=accept4(listener.value,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
  if(peer>=0){*out=peer;return FL_OK;}
  if(errno!=EINTR&&errno!=EAGAIN&&errno!=ECONNABORTED)return FL_DISCONNECTED;
 }
}
// Sender: connect, retrying until the receiver listens or the deadline passes.
inline flResult connectRelayPool(const std::string&address,GCancellable*cancel,gint64 end,int*out){
 *out=-1;sockaddr_un addr{};addr.sun_family=AF_UNIX;
 if(address.size()>sizeof addr.sun_path)return FL_INVALID;
 memcpy(addr.sun_path,address.data(),address.size());
 for(;;){
  if(cancel&&g_cancellable_is_cancelled(cancel))return FL_DISCONNECTED;
  if(g_get_monotonic_time()>=end)return FL_TIMEOUT;
  Fd s;s.value=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(s.value<0)return FL_INTERNAL;
  if(!connect(s.value,reinterpret_cast<sockaddr*>(&addr),socklen_t(offsetof(sockaddr_un,sun_path)+address.size()))){
   *out=s.value;s.value=-1;return FL_OK;}
  if(errno!=ECONNREFUSED&&errno!=ENOENT&&errno!=EAGAIN&&errno!=EINTR)return FL_DISCONNECTED;
  g_usleep(5000);
 }
}

// Consumes fd on every path. Producer images are BORROWED until return (the
// caller keeps the source pool pinned, e.g. with a consumer-pool snapshot).
// Both sides must agree on config, capacity, CPU access, device and the
// secret-derived channel name. Output owned on success. This only creates the
// ring: the caller MUST install the read-lease relay and cancellation
// supervision before any source writes. No independent writers.
inline flResult transferRelayPool(int input,GCancellable*cancel,guint timeout,
 const flSessionConfig&config,bool producer,GBytes*key,const char*name,unsigned count,
 bool cpuAccess,const flDmabuf*images,flChannel**out){
 Fd socketFd;socketFd.value=input;if(out)*out=nullptr;
 gsize keySize=0;const auto*keyBytes=key?static_cast<const guint8*>(g_bytes_get_data(key,&keySize)):nullptr;
 if(!out||!name||!*name||strlen(name)>240||count<2||count>FL_MAX_POOL_SIZE||
    !timeout||timeout>60000||!keyBytes||keySize!=32||!config.videoTrackId||
    flSessionValidate(&config)!=FL_SESSION_OK||(producer!=(images!=nullptr)))return FL_INVALID;
 std::array<guint8,FL_SESSION_WIRE_SIZE> wire{};
 if(flSessionEncode(&config,wire.data(),wire.size())!=FL_SESSION_OK)return FL_INVALID;
 const gint64 end=g_get_monotonic_time()+gint64(timeout)*1000;
 RelayIo io(input,cancel,end);

 // Kernel peer pinning: same effective user; its PID is bound into the MAC.
 ucred peer{};socklen_t credSize=sizeof peer;
 if(getsockopt(input,SOL_SOCKET,SO_PEERCRED,&peer,&credSize)||credSize!=sizeof peer||peer.uid!=geteuid()||peer.pid<=0)
  return FL_ACCESS_DENIED;
 const guint32 senderPid=producer?guint32(getpid()):guint32(peer.pid);
 const guint32 receiverPid=producer?guint32(peer.pid):guint32(getpid());
 auto mac=[&](char role,const guint8*receiverNonce,const guint8*senderNonce){
  auto*h=g_hmac_new(G_CHECKSUM_SHA256,keyBytes,keySize);
  static const char label[]="FrameLink authenticated Linux native relay pool v1";
  g_hmac_update(h,reinterpret_cast<const guint8*>(label),sizeof label);
  g_hmac_update(h,wire.data(),wire.size());g_hmac_update(h,reinterpret_cast<const guint8*>(&role),1);
  guint8 pids[8];for(unsigned b=0;b<4;b++){pids[b]=guint8(senderPid>>(8*b));pids[4+b]=guint8(receiverPid>>(8*b));}
  g_hmac_update(h,pids,sizeof pids);g_hmac_update(h,receiverNonce,32);g_hmac_update(h,senderNonce,32);
  std::array<guint8,32> d{};gsize n=d.size();g_hmac_get_digest(h,d.data(),&n);g_hmac_unref(h);return d;
 };
 auto equal=[](const guint8*a,const guint8*b,size_t n){guint8 x=0;for(size_t i=0;i<n;i++)x|=a[i]^b[i];return !x;};
 std::array<guint8,32> receiverNonce{};std::array<guint8,64> senderHello{};std::array<guint8,32> receiverProof{};
 flResult r;
 if(producer){
  if((r=io.receive(receiverNonce.data(),receiverNonce.size()))!=FL_OK)return r;
  // Fresh nonce from the OS CSPRNG.
  {Fd random;random.value=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
   if(random.value<0||read(random.value,senderHello.data(),32)!=32)return FL_INTERNAL;}
  const auto proof=mac('S',receiverNonce.data(),senderHello.data());memcpy(senderHello.data()+32,proof.data(),32);
  if((r=io.send(senderHello.data(),senderHello.size()))!=FL_OK)return r;
  if((r=io.receive(receiverProof.data(),receiverProof.size()))!=FL_OK)return r;
  if(!equal(mac('R',receiverNonce.data(),senderHello.data()).data(),receiverProof.data(),32))return FL_ACCESS_DENIED;
 }else{
  {Fd random;random.value=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
   if(random.value<0||read(random.value,receiverNonce.data(),32)!=32)return FL_INTERNAL;}
  if((r=io.send(receiverNonce.data(),receiverNonce.size()))!=FL_OK)return r;
  if((r=io.receive(senderHello.data(),senderHello.size()))!=FL_OK)return r;
  if(!equal(mac('S',receiverNonce.data(),senderHello.data()).data(),senderHello.data()+32,32))return FL_ACCESS_DENIED;
  receiverProof=mac('R',receiverNonce.data(),senderHello.data());
  if((r=io.send(receiverProof.data(),receiverProof.size()))!=FL_OK)return r;
 }

 // Offer layout: magic, count, cpu access, render node, name hash, then per
 // slot planes/fourcc/modifier/offsets/strides. Fds travel as SCM_RIGHTS.
 constexpr size_t Header=56,Slot=80;
 std::array<guint8,Header+Slot*FL_MAX_POOL_SIZE> packet{};
 memcpy(packet.data(),"FLNRL\0\0\1",8);
 GST_WRITE_UINT32_LE(packet.data()+8,count);GST_WRITE_UINT32_LE(packet.data()+12,cpuAccess?1:0);
 auto*hash=g_checksum_new(G_CHECKSUM_SHA256);
 g_checksum_update(hash,reinterpret_cast<const guchar*>(name),strlen(name)+1);
 gsize hashSize=32;g_checksum_get_digest(hash,packet.data()+24,&hashSize);g_checksum_free(hash);
 guint8 device[8]{};if(!fl::currentRenderNodeId(device))return FL_INTERNAL;
 auto planesFor=[&](uint32_t format){return format==FL_FORMAT_NV12?2u:1u;};
 const uint32_t planes=planesFor(config.video.format);
 struct Channel {flChannel*value=nullptr;~Channel(){if(value)flClose(value);}} channel;
 auto validateRing=[&]()->flResult{
  flChannelInfo info{};auto q=flQuery(channel.value,&info);if(q!=FL_OK)return q;
  // The receiver's ring must live on the device named in the offer.
  return info.width==config.video.width&&info.height==config.video.height&&
   info.format==flFormat(config.video.format)&&info.poolSize==count&&
   !memcmp(info.adapterLUID,packet.data()+16,8)?FL_OK:FL_FORMAT_UNSUPPORTED;
 };
 if(producer){
  memcpy(packet.data()+16,device,8);
  std::vector<int> fds;std::set<std::pair<dev_t,ino_t>> allocations;
  for(unsigned n=0;n<count;++n){
   const auto&d=images[n];guint8*slot=packet.data()+Header+Slot*n;
   if(d.planes!=planes)return FL_INVALID;
   GST_WRITE_UINT32_LE(slot,d.planes);GST_WRITE_UINT32_LE(slot+4,d.fourcc);GST_WRITE_UINT64_LE(slot+8,d.modifier);
   for(uint32_t p=0;p<d.planes;++p){
    struct stat st{};if(d.fd[p]<0||fstat(d.fd[p],&st))return FL_INVALID;
    if(!p&&!allocations.insert({st.st_dev,st.st_ino}).second)return FL_INVALID; // distinct allocations
    GST_WRITE_UINT64_LE(slot+16+8*p,d.offset[p]);GST_WRITE_UINT64_LE(slot+48+8*p,d.stride[p]);
    fds.push_back(d.fd[p]);
   }
  }
  if((r=io.send(packet.data(),packet.size(),fds.data(),fds.size()))!=FL_OK)return r;
  std::array<guint8,Header+Slot*FL_MAX_POOL_SIZE> reply{};
  if((r=io.receive(reply.data(),reply.size()))!=FL_OK)return r;
  if(reply!=packet)return FL_INVALID;
  // Attach as producer to the receiver's ring within the remaining budget.
  r=FL_NOT_FOUND;
  while((r=flOpenProducer(name,&channel.value))==FL_NOT_FOUND){
   if(cancel&&g_cancellable_is_cancelled(cancel))return FL_DISCONNECTED;
   if(g_get_monotonic_time()>=end)return FL_TIMEOUT;
   g_usleep(5000);
  }
  if(r!=FL_OK)return r;
  if((r=validateRing())!=FL_OK)return r;
  if((r=io.send(packet.data(),packet.size()))!=FL_OK)return r;
 }else{
  std::vector<int> received;
  if((r=io.receive(packet.data(),packet.size(),&received))!=FL_OK)return r;
  struct Owned {std::vector<int>&fds;~Owned(){for(int f:fds)::close(f);}} owned{received};
  guint8 expected[24];memcpy(expected,"FLNRL\0\0\1",8);
  GST_WRITE_UINT32_LE(expected+8,count);GST_WRITE_UINT32_LE(expected+12,cpuAccess?1:0);
  guint8 nameHash[32];hash=g_checksum_new(G_CHECKSUM_SHA256);
  g_checksum_update(hash,reinterpret_cast<const guchar*>(name),strlen(name)+1);
  hashSize=32;g_checksum_get_digest(hash,nameHash,&hashSize);g_checksum_free(hash);
  if(memcmp(packet.data(),expected,16)||memcmp(packet.data()+24,nameHash,32))return FL_INVALID;
  for(unsigned n=count;n<FL_MAX_POOL_SIZE;++n)
   for(size_t i=0;i<Slot;++i)if(packet[Header+Slot*n+i])return FL_INVALID;
  if(received.size()!=size_t(count)*planes)return FL_INVALID;
  // Same device the sender's ring lives on; never a silent substitution.
  if(memcmp(packet.data()+16,device,8))return FL_ADAPTER_MISMATCH;
  std::array<flDmabuf,FL_MAX_POOL_SIZE> imports{};std::set<std::pair<dev_t,ino_t>> allocations;size_t next=0;
  for(unsigned n=0;n<count;++n){
   const guint8*slot=packet.data()+Header+Slot*n;auto&d=imports[n];
   for(int&f:d.fd)f=-1;
   d.planes=GST_READ_UINT32_LE(slot);d.fourcc=GST_READ_UINT32_LE(slot+4);d.modifier=GST_READ_UINT64_LE(slot+8);
   if(d.planes!=planes)return FL_INVALID;
   for(uint32_t p=0;p<d.planes;++p){
    d.fd[p]=received[next++];d.offset[p]=GST_READ_UINT64_LE(slot+16+8*p);d.stride[p]=GST_READ_UINT64_LE(slot+48+8*p);
    struct stat st{};if(fstat(d.fd[p],&st))return FL_INVALID;
    if(!p&&!allocations.insert({st.st_dev,st.st_ino}).second)return FL_INVALID;
   }
  }
  if(cancel&&g_cancellable_is_cancelled(cancel))return FL_DISCONNECTED;
  if(g_get_monotonic_time()>=end)return FL_TIMEOUT;
  r=flCreateChannelFromDmabufs(name,config.video.width,config.video.height,flFormat(config.video.format),count,
   FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA|(cpuAccess?FL_MAP_CPU:0),imports.data(),&channel.value);
  if(r!=FL_OK)return r;
  if((r=validateRing())!=FL_OK)return r;
  if((r=io.send(packet.data(),packet.size()))!=FL_OK)return r;
  std::array<guint8,Header+Slot*FL_MAX_POOL_SIZE> attached{};
  if((r=io.receive(attached.data(),attached.size()))!=FL_OK)return r;
  if(attached!=packet)return FL_INVALID;
 }
 if(cancel&&g_cancellable_is_cancelled(cancel))return FL_DISCONNECTED;
 if(g_get_monotonic_time()>=end)return FL_TIMEOUT;
 *out=channel.value;channel.value=nullptr;return FL_OK;
}
}

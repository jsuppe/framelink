#define FL_GST_SESSION_BUILD
#include "framelink_session_transport.h"
#include "../runtime/av_auth.h"
#include "fl_gst_session_internal.h"
#include <algorithm>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif
#include <array>
#include <cstring>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
namespace {
using Offer=std::array<guint8,268>;
using Nonce=std::array<guint8,32>;
using Wire=std::array<guint8,FL_SESSION_WIRE_SIZE>;
using Digest=std::array<guint8,32>;
constexpr guint8 magic[8]={'F','L','G','S','T','A','V',2};
struct Deadline {
 GCancellable* cancel=g_cancellable_new();GCancellable* outer;
 gulong handler=0;std::mutex mutex;std::condition_variable condition;bool done=false;std::thread timer;
 Deadline(guint ms,GCancellable* c):outer(c){
  if(outer)handler=g_cancellable_connect(outer,G_CALLBACK(+[](GCancellable*,gpointer p){g_cancellable_cancel(static_cast<GCancellable*>(p));}),cancel,nullptr);
  timer=std::thread([this,ms]{std::unique_lock<std::mutex> lock(mutex);
   if(!condition.wait_for(lock,std::chrono::milliseconds(ms),[this]{return done;})){lock.unlock();g_cancellable_cancel(cancel);}
  });
 }
 ~Deadline(){if(handler)g_cancellable_disconnect(outer,handler);
  {std::lock_guard<std::mutex> lock(mutex);done=true;}condition.notify_all();timer.join();g_object_unref(cancel);
 }
};
bool encode(const flSessionCapabilities& c,Offer& b){
 flVideoFormat v{};flAudioFormat a{};
 if(c.version!=FL_SESSION_OPTIONAL_TRACKS_VERSION||flSessionNegotiate(&c,&c,&v,&a)!=FL_SESSION_OK)return false;
 b.fill(0);GST_WRITE_UINT32_LE(b.data(),c.version);GST_WRITE_UINT32_LE(b.data()+4,c.videoCount);GST_WRITE_UINT32_LE(b.data()+8,c.audioCount);
 for(guint i=0;i<c.videoCount;i++) {auto*p=b.data()+12+i*20;auto&v=c.video[i];
  GST_WRITE_UINT32_LE(p,v.format);GST_WRITE_UINT32_LE(p+4,v.width);GST_WRITE_UINT32_LE(p+8,v.height);GST_WRITE_UINT32_LE(p+12,v.fpsNum);GST_WRITE_UINT32_LE(p+16,v.fpsDen);
 }
 for(guint i=0;i<c.audioCount;i++){auto*p=b.data()+172+i*12;auto&a=c.audio[i];
  GST_WRITE_UINT32_LE(p,a.pcm);GST_WRITE_UINT32_LE(p+4,a.rate);GST_WRITE_UINT32_LE(p+8,a.layout);
 }return true;
}
bool decode(const Offer&b,flSessionCapabilities&c){
 c={};c.version=GST_READ_UINT32_LE(b.data());c.videoCount=GST_READ_UINT32_LE(b.data()+4);c.audioCount=GST_READ_UINT32_LE(b.data()+8);
 if(c.videoCount>FL_SESSION_MAX_FORMATS||c.audioCount>FL_SESSION_MAX_FORMATS)return false;
 for(guint i=0;i<c.videoCount;i++){auto*p=b.data()+12+i*20;c.video[i]={GST_READ_UINT32_LE(p),GST_READ_UINT32_LE(p+4),GST_READ_UINT32_LE(p+8),GST_READ_UINT32_LE(p+12),GST_READ_UINT32_LE(p+16)};}
 for(guint i=0;i<c.audioCount;i++){auto*p=b.data()+172+i*12;c.audio[i]={GST_READ_UINT32_LE(p),GST_READ_UINT32_LE(p+4),GST_READ_UINT32_LE(p+8)};}
 Offer canonical{};return encode(c,canonical)&&canonical==b;
}
Digest proof(const av::Endpoint&ep,guint8 direction,const Offer&client,const Offer&server,const Nonce&cn,const Nonce&sn,const Wire&wire){
 auto*h=g_hmac_new(G_CHECKSUM_SHA256,ep.token.data(),ep.token.size());
 g_hmac_update(h,magic,sizeof magic);g_hmac_update(h,&direction,1);
 g_hmac_update(h,cn.data(),cn.size());g_hmac_update(h,sn.data(),sn.size());
 g_hmac_update(h,client.data(),client.size());g_hmac_update(h,server.data(),server.size());g_hmac_update(h,wire.data(),wire.size());
 Digest d{};gsize n=d.size();g_hmac_get_digest(h,d.data(),&n);g_hmac_unref(h);return d;
}
bool equal(const Digest&a,const Digest&b){guint delta=0;for(gsize i=0;i<a.size();i++)delta|=a[i]^b[i];return !delta;}
}
gboolean fl_gst_session_negotiate_stream(FlGstSession*s,GSocketConnection*stream,gboolean producer,GBytes*secret,
 const flSessionCapabilities*local,const flSessionConfig*proposal,guint timeout,GCancellable*outer,GError**error){
 auto fail=[&](const char*text){if(s&&G_TYPE_CHECK_INSTANCE_TYPE(s,fl_gst_session_get_type()))fl_gst_session_cancel(s);if(stream&&G_IS_SOCKET_CONNECTION(stream))g_io_stream_close(G_IO_STREAM(stream),nullptr,nullptr);
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,text);return FALSE;};
 Offer own{},remote{};gsize keySize=0;const auto*key=secret?static_cast<const guint8*>(g_bytes_get_data(secret,&keySize)):nullptr;
 if(!s||!G_TYPE_CHECK_INSTANCE_TYPE(s,fl_gst_session_get_type())||!stream||!G_IS_SOCKET_CONNECTION(stream)||!local||!encode(*local,own)||!key||keySize!=32||!timeout||timeout>60000||(producer&&!proposal))return fail("invalid session negotiation arguments");
 auto*socket=g_socket_connection_get_socket(stream);
 if(g_socket_get_socket_type(socket)!=G_SOCKET_TYPE_STREAM||!g_socket_is_connected(socket))return fail("connected STREAM socket required");
 av::Endpoint ep{};memcpy(ep.token.data(),key,keySize);guint nonzero=0;for(auto c:ep.token)nonzero|=c;
 if(!nonzero)return fail("invalid session credential");
 Deadline deadline(timeout,outer);std::string reason;
 bool auth=producer?av::authenticateServerCancellable(stream,ep,'C',reason,deadline.cancel):av::authenticateClientCancellable(stream,ep,'C',reason,deadline.cancel);
 if(!auth)return fail(reason.c_str());
 auto read=[&](void*p,gsize n){gsize done=0;return g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(stream)),p,n,&done,deadline.cancel,nullptr)&&done==n;};
 auto write=[&](const void*p,gsize n){gsize done=0;return g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(stream)),p,n,&done,deadline.cancel,nullptr)&&done==n;};
 Nonce ownNonce{},peerNonce{};if(!av::secureRandom(ownNonce.data(),ownNonce.size(),reason))return fail(reason.c_str());
 guint8 header[sizeof magic]{};flSessionConfig chosen{};flSessionCapabilities peer{};Wire wire{};Digest sent{},received{};
 if(producer){
  if(!read(header,sizeof header)||memcmp(header,magic,sizeof magic)||!read(peerNonce.data(),peerNonce.size())||!read(remote.data(),remote.size())||!decode(remote,peer))return fail("invalid consumer session offer");
  chosen=*proposal;
  if(chosen.version!=FL_SESSION_OPTIONAL_TRACKS_VERSION||flSessionNegotiate(local,&peer,&chosen.video,&chosen.audio)!=FL_SESSION_OK||flSessionValidateSelection(&chosen,local,&peer)!=FL_SESSION_OK||flSessionEncode(&chosen,wire.data(),wire.size())!=FL_SESSION_OK)return fail("incompatible session selection");
  sent=proof(ep,'P',remote,own,peerNonce,ownNonce,wire);
  if(!write(magic,sizeof magic)||!write(ownNonce.data(),ownNonce.size())||!write(own.data(),own.size())||!write(wire.data(),wire.size())||!write(sent.data(),sent.size())||!read(received.data(),received.size())||!equal(received,proof(ep,'C',remote,own,peerNonce,ownNonce,wire)))return fail("session confirmation rejected");
 }else{
  if(!write(magic,sizeof magic)||!write(ownNonce.data(),ownNonce.size())||!write(own.data(),own.size())||!read(header,sizeof header)||memcmp(header,magic,sizeof magic)||!read(peerNonce.data(),peerNonce.size())||!read(remote.data(),remote.size())||!read(wire.data(),wire.size())||!read(received.data(),received.size())||!decode(remote,peer)||!equal(received,proof(ep,'P',own,remote,ownNonce,peerNonce,wire))||flSessionDecode(wire.data(),wire.size(),&chosen)!=FL_SESSION_OK||chosen.version!=FL_SESSION_OPTIONAL_TRACKS_VERSION||flSessionValidateSelection(&chosen,&peer,local)!=FL_SESSION_OK)return fail("producer selection or authentication rejected");
  sent=proof(ep,'C',own,remote,ownNonce,peerNonce,wire);if(!write(sent.data(),sent.size()))return fail("session confirmation write failed");
 }
 if(g_cancellable_is_cancelled(deadline.cancel))return fail("session negotiation cancelled or deadline exceeded");
 if(fl_gst_session_configure(s,&chosen,producer?local:&peer,producer?&peer:local)!=FL_GST_SESSION_READY)return fail("session generation or local track policy rejected");
 return TRUE;
}

gboolean fl_gst_session_authenticate_audio_stream(FlGstSession*s,GSocketConnection*stream,gboolean producer,
 GBytes*secret,guint timeout,GCancellable*outer,GError**error){
 auto fail=[&](const char*message){if(s&&G_TYPE_CHECK_INSTANCE_TYPE(s,fl_gst_session_get_type()))fl_gst_session_cancel(s);
  if(stream&&G_IS_SOCKET_CONNECTION(stream))g_io_stream_close(G_IO_STREAM(stream),nullptr,nullptr);
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,message);return FALSE;};
 flSessionConfig cfg{};Wire wire{};gsize n=0;
 const auto*key=secret?static_cast<const guint8*>(g_bytes_get_data(secret,&n)):nullptr;
 if(!stream||!G_IS_SOCKET_CONNECTION(stream)||!key||n!=32||!timeout||timeout>60000||fl_gst_session_get_config(s,&cfg)!=FL_GST_SESSION_READY||!cfg.audioTrackId||cfg.version!=FL_SESSION_OPTIONAL_TRACKS_VERSION||flSessionEncode(&cfg,wire.data(),wire.size())!=FL_SESSION_OK)return fail("invalid audio session binding");
 auto*socket=g_socket_connection_get_socket(stream);
 if(g_socket_get_socket_type(socket)!=G_SOCKET_TYPE_STREAM||!g_socket_is_connected(socket))return fail("connected audio STREAM socket required");
 guint nonzero=0;for(gsize i=0;i<n;i++)nonzero|=key[i];if(!nonzero)return fail("invalid audio session credential");
 av::Endpoint ep{};auto*h=g_hmac_new(G_CHECKSUM_SHA256,key,n);
 constexpr char label[]="FrameLink public audio session v2";
 g_hmac_update(h,reinterpret_cast<const guint8*>(label),sizeof label);g_hmac_update(h,wire.data(),wire.size());
 n=ep.token.size();g_hmac_get_digest(h,ep.token.data(),&n);g_hmac_unref(h);
 Deadline deadline(timeout,outer);std::string reason;
 const bool ok=producer?av::authenticateServerCancellable(stream,ep,'A',reason,deadline.cancel):av::authenticateClientCancellable(stream,ep,'A',reason,deadline.cancel);
 if(!ok)return fail(reason.c_str());
 flSessionConfig current{};Wire currentWire{};
 if(g_cancellable_is_cancelled(deadline.cancel)||fl_gst_session_get_config(s,&current)!=FL_GST_SESSION_READY||flSessionEncode(&current,currentWire.data(),currentWire.size())!=FL_SESSION_OK||currentWire!=wire)return fail("audio session changed during authentication");
 if(!fl_gst_session_publish_audio(s,&cfg,stream,producer))return fail("audio stream publication rejected");
 return TRUE;
}

gboolean fl_gst_session_authenticate_event_stream(FlGstSession*s,GSocketConnection*stream,gboolean producer,
 GBytes*secret,guint timeout,GCancellable*outer,GError**error){
 auto fail=[&](const char*message){if(s&&G_TYPE_CHECK_INSTANCE_TYPE(s,fl_gst_session_get_type()))fl_gst_session_cancel(s);
  if(stream&&G_IS_SOCKET_CONNECTION(stream))g_io_stream_close(G_IO_STREAM(stream),nullptr,nullptr);
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,message);return FALSE;};
 flSessionConfig cfg{};Wire wire{};gsize n=0;
 const auto*key=secret?static_cast<const guint8*>(g_bytes_get_data(secret,&n)):nullptr;
 if(!stream||!G_IS_SOCKET_CONNECTION(stream)||!key||n!=32||!timeout||timeout>60000||fl_gst_session_get_config(s,&cfg)!=FL_GST_SESSION_READY||!cfg.videoTrackId||cfg.version!=FL_SESSION_OPTIONAL_TRACKS_VERSION||flSessionEncode(&cfg,wire.data(),wire.size())!=FL_SESSION_OK)return fail("invalid event session binding");
 auto*socket=g_socket_connection_get_socket(stream);
 if(g_socket_get_socket_type(socket)!=G_SOCKET_TYPE_STREAM||!g_socket_is_connected(socket))return fail("connected event STREAM socket required");
 guint nonzero=0;for(gsize i=0;i<n;i++)nonzero|=key[i];if(!nonzero)return fail("invalid event session credential");
 av::Endpoint ep{};auto*h=g_hmac_new(G_CHECKSUM_SHA256,key,n);
 constexpr char label[]="FrameLink public event session v2";
 g_hmac_update(h,reinterpret_cast<const guint8*>(label),sizeof label);g_hmac_update(h,wire.data(),wire.size());
 n=ep.token.size();g_hmac_get_digest(h,ep.token.data(),&n);g_hmac_unref(h);
 Deadline deadline(timeout,outer);std::string reason;
 const bool ok=producer?av::authenticateServerCancellable(stream,ep,'E',reason,deadline.cancel):av::authenticateClientCancellable(stream,ep,'E',reason,deadline.cancel);
 if(!ok)return fail(reason.c_str());
 flSessionConfig current{};Wire currentWire{};
 if(g_cancellable_is_cancelled(deadline.cancel)||fl_gst_session_get_config(s,&current)!=FL_GST_SESSION_READY||flSessionEncode(&current,currentWire.data(),currentWire.size())!=FL_SESSION_OK||currentWire!=wire)return fail("event session changed during authentication");
 if(!fl_gst_session_publish_events(s,&cfg,stream,producer))return fail("event stream publication rejected");
 return TRUE;
}

namespace {
struct StreamOwner {GSocketConnection* p=nullptr;~StreamOwner(){if(p){g_io_stream_close(G_IO_STREAM(p),nullptr,nullptr);g_object_unref(p);}}};
struct ObjectOwner {GObject* p=nullptr;~ObjectOwner(){if(p)g_object_unref(p);}};
}
gboolean fl_gst_session_open_local_audio(FlGstSession*session,gboolean producer,const gchar*path,
 const flAudioFormat*format,guint queueMs,guint timeout,guint idle,GCancellable*outer,GError**error){
 auto fail=[&](const char*message){fl_gst_session_cancel(session);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,message);return FALSE;};
 if(!session||!path||!*path||!format||!queueMs||queueMs>2000||!timeout||timeout>60000||!idle||idle>60000)return fail("invalid local audio session configuration");
 flSessionCapabilities caps{};caps.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;caps.audioCount=1;caps.audio[0]=*format;
 Offer encoded{};if(!encode(caps,encoded))return fail("unsupported local PCM format");
 Deadline deadline(timeout,outer);av::Endpoint endpoint{};av::RendezvousOwner owner;std::string reason;
 ObjectOwner listener,client;StreamOwner control,audio;
 // The outer Deadline bounds all nested operations together.
 if(producer){
  if(!av::secureRandom(endpoint.token.data(),endpoint.token.size(),reason))return fail(reason.c_str());
  auto*l=g_socket_listener_new();listener.p=G_OBJECT(l);
  auto*address=g_inet_address_new_from_string("127.0.0.1");auto*bind=g_inet_socket_address_new(address,0);g_object_unref(address);
  GSocketAddress*effective=nullptr;const bool bound=g_socket_listener_add_address(l,bind,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr);g_object_unref(bind);
  if(!bound)return fail("cannot bind private loopback session listener");
  endpoint.port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(effective);
  if(!owner.publish(path,endpoint,reason))return fail(reason.c_str());
  control.p=g_socket_listener_accept(l,nullptr,deadline.cancel,nullptr);
 }else{
  while(!g_cancellable_is_cancelled(deadline.cancel)){
   if(av::readEndpoint(path,endpoint,reason))break;
   // Only discovery polling, never a per-frame poll or added media latency.
   g_usleep(5000);
  }
  if(g_cancellable_is_cancelled(deadline.cancel))return fail("local session discovery deadline or cancellation");
  auto*c=g_socket_client_new();client.p=G_OBJECT(c);
  // Private loopback: never consult the system proxy resolver (see open_local_av).
  g_socket_client_set_enable_proxy(c,FALSE);
  control.p=g_socket_client_connect_to_host(c,"127.0.0.1",endpoint.port,deadline.cancel,nullptr);
 }
 if(!control.p)return fail("local control connection deadline or failure");
 auto*secret=g_bytes_new(endpoint.token.data(),endpoint.token.size());
 struct BytesOwner{GBytes*p;~BytesOwner(){g_bytes_unref(p);}} secretOwner{secret};
 flSessionConfig proposal{};proposal.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;proposal.generation=1;proposal.audioTrackId=2;
 proposal.audio=*format;proposal.audioQueueMs=queueMs;proposal.connectTimeoutMs=timeout;proposal.idleTimeoutMs=idle;
 if(producer&&(!av::secureRandom(proposal.sessionId.bytes,16,reason)||!av::secureRandom(proposal.clockId.bytes,16,reason)))return fail(reason.c_str());
 if(!fl_gst_session_negotiate_stream(session,control.p,producer,secret,&caps,producer?&proposal:nullptr,timeout,deadline.cancel,error))return FALSE;
 flSessionConfig selected{};
 if(fl_gst_session_get_config(session,&selected)!=FL_GST_SESSION_READY||selected.audioQueueMs>queueMs)return fail("peer audio queue budget exceeds local limit");
 if(producer)audio.p=g_socket_listener_accept(G_SOCKET_LISTENER(listener.p),nullptr,deadline.cancel,nullptr);
 else audio.p=g_socket_client_connect_to_host(G_SOCKET_CLIENT(client.p),"127.0.0.1",endpoint.port,deadline.cancel,nullptr);
 if(!audio.p)return fail("local audio connection deadline or failure");
 if(!fl_gst_session_authenticate_audio_stream(session,audio.p,producer,secret,timeout,deadline.cancel,error))return FALSE;
 // Coordinator owns a strong reference after authentication. Do not close it on
 // this successful scope exit; control channel is intentionally setup-only here.
 g_object_unref(audio.p);audio.p=nullptr;
 return TRUE;
}

namespace {
bool exact(GSocketConnection*c,bool write,void*data,gsize n,GCancellable*cancel){
 gsize done=0;
 if(write)return g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(c)),data,n,&done,cancel,nullptr)&&done==n;
 return g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(c)),data,n,&done,cancel,nullptr)&&done==n;
}
Digest labelled(const av::Endpoint&ep,const char*label,const guint8*a,gsize an,const guint8*b,gsize bn){
 auto*h=g_hmac_new(G_CHECKSUM_SHA256,ep.token.data(),ep.token.size());
 g_hmac_update(h,reinterpret_cast<const guint8*>(label),strlen(label)+1);
 if(an)g_hmac_update(h,a,an);if(bn)g_hmac_update(h,b,bn);
 Digest d{};gsize n=d.size();g_hmac_get_digest(h,d.data(),&n);g_hmac_unref(h);return d;
}
bool constantEqual(const Digest&a,const guint8*b){guint8 x=0;for(gsize i=0;i<a.size();i++)x|=a[i]^b[i];return !x;}
}
gboolean fl_gst_session_open_local_av(FlGstSession*session,gboolean producer,const gchar*path,
 const flSessionCapabilities*local,guint videoQueue,guint pool,guint queueMs,guint timeout,guint idle,guint flags,
 flChannel**channel,GCancellable*outer,GError**error){
 if(channel)*channel=nullptr;
 auto fail=[&](const char*message){fl_gst_session_cancel(session);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,message);return FALSE;};
 if(!session||!path||!*path||!local||!channel||local->version!=FL_SESSION_OPTIONAL_TRACKS_VERSION||(!local->videoCount&&!local->audioCount)||
    (local->videoCount&&(!videoQueue||videoQueue>16||pool==1||pool>FL_MAX_POOL_SIZE))||(local->audioCount&&(!queueMs||queueMs>2000))||!timeout||timeout>60000||!idle||idle>60000)
  return fail("invalid local A/V session configuration");
 Offer encoded{};if(!encode(*local,encoded))return fail("unsupported local A/V capabilities");
 Deadline deadline(timeout,outer);av::Endpoint endpoint{};av::RendezvousOwner owner;std::string reason;
 ObjectOwner listener,client;StreamOwner control,audio,events;
 std::string ioReason;
 auto next=[&](StreamOwner&out){GError*e=nullptr;
  if(producer)out.p=g_socket_listener_accept(G_SOCKET_LISTENER(listener.p),nullptr,deadline.cancel,&e);
  else out.p=g_socket_client_connect_to_host(G_SOCKET_CLIENT(client.p),"127.0.0.1",endpoint.port,deadline.cancel,&e);
  if(e){ioReason=e->message;g_error_free(e);}
  return out.p!=nullptr;};
 auto ioFail=[&](const char*what){std::string m=std::string(what)+(ioReason.empty()?"":": "+ioReason);return fail(m.c_str());};
 if(producer){
  if(!av::secureRandom(endpoint.token.data(),endpoint.token.size(),reason))return fail(reason.c_str());
  auto*l=g_socket_listener_new();listener.p=G_OBJECT(l);
  auto*address=g_inet_address_new_from_string("127.0.0.1");auto*bind=g_inet_socket_address_new(address,0);g_object_unref(address);
  GSocketAddress*effective=nullptr;const bool bound=g_socket_listener_add_address(l,bind,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_TCP,nullptr,&effective,nullptr);g_object_unref(bind);
  if(!bound)return fail("cannot bind private loopback session listener");
  endpoint.port=g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));g_object_unref(effective);
  if(!owner.publish(path,endpoint,reason))return fail(reason.c_str());
 }else{
  while(!g_cancellable_is_cancelled(deadline.cancel)){if(av::readEndpoint(path,endpoint,reason))break;g_usleep(5000);}
  if(g_cancellable_is_cancelled(deadline.cancel))return fail("local session discovery deadline or cancellation");
  client.p=G_OBJECT(g_socket_client_new());
  // Private loopback: never consult the system proxy resolver. Its first use
  // in a process loads libproxy and GNetworkMonitor, which can block
  // uncancellably for seconds (observed in a netlink send) past the deadline;
  // a 127.0.0.1 rendezvous must not depend on proxy configuration anyway.
  g_socket_client_set_enable_proxy(G_SOCKET_CLIENT(client.p),FALSE);
 }
 if(!next(control))return ioFail("local control connection deadline or failure");
 // Small control records (clock request, ready/drain markers) must not wait
 // behind Nagle/delayed-ACK: that skews the round-trip midpoint by tens of ms.
 g_socket_set_option(g_socket_connection_get_socket(control.p),IPPROTO_TCP,TCP_NODELAY,1,nullptr);
 auto*secret=g_bytes_new(endpoint.token.data(),endpoint.token.size());
 struct BytesOwner{GBytes*p;~BytesOwner(){g_bytes_unref(p);}} secretOwner{secret};
 auto*clock=gst_system_clock_obtain();struct ClockOwner{GstClock*p;~ClockOwner(){gst_object_unref(p);}} clockOwner{clock};
 flSessionConfig proposal{};proposal.version=FL_SESSION_OPTIONAL_TRACKS_VERSION;proposal.generation=1;
 proposal.videoTrackId=local->videoCount?1:0;proposal.audioTrackId=local->audioCount?2:0;
 proposal.videoQueueFrames=local->videoCount?videoQueue:0;proposal.audioQueueMs=local->audioCount?queueMs:0;
 proposal.connectTimeoutMs=timeout;proposal.idleTimeoutMs=idle;
 // Epoch is on the producer's monotonic system clock; media starts at or after it.
 proposal.epochNs=gint64(gst_clock_get_time(clock)+200*GST_MSECOND);
 if(producer&&(!av::secureRandom(proposal.sessionId.bytes,16,reason)||!av::secureRandom(proposal.clockId.bytes,16,reason)))return fail(reason.c_str());
 if(!fl_gst_session_negotiate_stream(session,control.p,producer,secret,local,producer?&proposal:nullptr,timeout,deadline.cancel,error))return FALSE;
 flSessionConfig selected{};Wire wire{};
 if(fl_gst_session_get_config(session,&selected)!=FL_GST_SESSION_READY||flSessionEncode(&selected,wire.data(),wire.size())!=FL_SESSION_OK)return fail("negotiated session unavailable");
 if(bool(selected.videoTrackId)!=bool(local->videoCount)||bool(selected.audioTrackId)!=bool(local->audioCount))return fail("peer track set differs from local tracks");
 if((selected.audioTrackId&&selected.audioQueueMs>queueMs)||(selected.videoTrackId&&selected.videoQueueFrames>videoQueue))return fail("peer queue budget exceeds local limit");
 // Authenticated clock offset: consumer nonce, producer time + HMAC over both.
 constexpr char clockLabel[]="FrameLink public clock v1";
 guint8 nonce[16];guint8 reply[8+32];
 if(producer){
  if(!exact(control.p,false,nonce,sizeof nonce,deadline.cancel))return fail("clock request deadline or failure");
  guint64 now=gst_clock_get_time(clock);for(int i=0;i<8;i++)reply[i]=guint8(now>>(8*i));
  auto mac=labelled(endpoint,clockLabel,nonce,sizeof nonce,reply,8);memcpy(reply+8,mac.data(),32);
  if(!exact(control.p,true,reply,sizeof reply,deadline.cancel))return fail("clock reply failed");
 }else{
  if(!av::secureRandom(nonce,sizeof nonce,reason))return fail(reason.c_str());
  auto t0=gst_clock_get_time(clock);
  if(!exact(control.p,true,nonce,sizeof nonce,deadline.cancel)||!exact(control.p,false,reply,sizeof reply,deadline.cancel))return fail("clock exchange deadline or failure");
  auto t1=gst_clock_get_time(clock);
  if(!constantEqual(labelled(endpoint,clockLabel,nonce,sizeof nonce,reply,8),reply+8))return fail("clock reply authentication failed");
  guint64 source=0;for(int i=0;i<8;i++)source|=guint64(reply[i])<<(8*i);
  flClockMapping mapping{};mapping.sourceClockId=selected.clockId;
  if(!av::secureRandom(mapping.targetClockId.bytes,16,reason))return fail(reason.c_str());
  if(t1<t0||source>guint64(G_MAXINT64)||t1>guint64(G_MAXINT64))return fail("invalid clock exchange");
  mapping.sourceAnchorNs=gint64(source);mapping.targetAnchorNs=gint64(t0+(t1-t0)/2);mapping.uncertaintyNs=(t1-t0+1)/2;
  if(fl_gst_session_set_mapping(session,selected.generation,&mapping,50*GST_MSECOND)!=FL_GST_SESSION_READY)return fail("clock uncertainty exceeds 50ms or mapping rejected");
 }
 if(selected.audioTrackId){
  if(!next(audio))return ioFail("local audio connection deadline or failure");
  if(!fl_gst_session_authenticate_audio_stream(session,audio.p,producer,secret,timeout,deadline.cancel,error))return FALSE;
  g_object_unref(audio.p);audio.p=nullptr; // coordinator owns it now
 }
 if(selected.videoTrackId){
  if(!next(events))return ioFail("local event connection deadline or failure");
  if(!fl_gst_session_authenticate_event_stream(session,events.p,producer,secret,timeout,deadline.cancel,error))return FALSE;
  g_object_unref(events.p);events.p=nullptr;
  auto id=labelled(endpoint,"FrameLink public video channel v1",wire.data(),wire.size(),nullptr,0);
  gchar name[5+24+1]="flav-";for(int i=0;i<12;i++)g_snprintf(name+5+2*i,3,"%02x",id[i]);
  guint8 ready=0;flChannel*ch=nullptr;
  if(!producer){
   const auto format=flFormat(selected.video.format);
   if(flCreateChannelEx(name,selected.video.width,selected.video.height,format,pool?pool:4u,flags?flags:(FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA),&ch)!=FL_OK||!ch)return fail("cannot create consumer video channel");
   ready='V';if(!exact(control.p,true,&ready,1,deadline.cancel)){flClose(ch);return fail("video channel announcement failed");}
   // Do not return before the producer attached: a later attach changes the ring
   // generation under a started consumer element.
   if(!exact(control.p,false,&ready,1,deadline.cancel)||ready!='P'){flClose(ch);return fail("producer did not attach to the video channel");}
  }else{
   if(!exact(control.p,false,&ready,1,deadline.cancel)||ready!='V')return fail("consumer video channel announcement missing");
   flResult r=FL_NOT_FOUND;
   while(!g_cancellable_is_cancelled(deadline.cancel)&&(r=flOpenProducer(name,&ch))==FL_NOT_FOUND)g_usleep(5000);
   if(r!=FL_OK||!ch)return fail("cannot attach producer video channel");
   ready='P';if(!exact(control.p,true,&ready,1,deadline.cancel)){flClose(ch);return fail("video attach confirmation failed");}
  }
  *channel=ch;
 }
 // Retain the authenticated control connection for fl_gst_session_finish_local.
 g_object_set_data_full(G_OBJECT(session),"framelink-local-control",control.p,g_object_unref);control.p=nullptr;
 return TRUE;
}
gboolean fl_gst_session_local_ready(FlGstSession*session,gboolean producer,guint timeout,GError**error){
 if(!session||!timeout||timeout>60000){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"invalid ready request");return FALSE;}
 auto*control=static_cast<GSocketConnection*>(g_object_get_data(G_OBJECT(session),"framelink-local-control"));
 if(!control){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"no local control connection");return FALSE;}
 Deadline deadline(timeout,nullptr);guint8 marker='R';
 const bool ok=producer?(exact(control,false,&marker,1,deadline.cancel)&&marker=='R'):exact(control,true,&marker,1,deadline.cancel);
 if(!ok){fl_gst_session_cancel(session);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT,producer?"consumer did not report ready before the deadline":"ready report failed");}
 return ok;
}
gboolean fl_gst_session_finish_local(FlGstSession*session,gboolean producer,guint timeout,GError**error){
 if(!session||!timeout||timeout>60000){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"invalid finish request");return FALSE;}
 auto*control=static_cast<GSocketConnection*>(g_object_steal_data(G_OBJECT(session),"framelink-local-control"));
 if(!control){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"no local control connection (already finished or not a local session)");return FALSE;}
 Deadline deadline(timeout,nullptr);guint8 marker='D';bool ok;
 if(producer)ok=exact(control,false,&marker,1,deadline.cancel)&&marker=='D';
 else ok=exact(control,true,&marker,1,deadline.cancel);
 g_io_stream_close(G_IO_STREAM(control),nullptr,nullptr);g_object_unref(control);
 if(!ok)g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT,producer?"consumer did not report drain before the deadline":"drain report failed");
 return ok;
}
gboolean fl_gst_session_apply_pipeline_clock(FlGstSession*session,GstPipeline*pipeline,gboolean producer,GstClockTime latency,GError**error){
 flSessionConfig c{};
 if(!session||!GST_IS_PIPELINE(pipeline)||fl_gst_session_get_config(session,&c)!=FL_GST_SESSION_READY||GST_STATE(pipeline)>GST_STATE_READY||(!producer&&!GST_CLOCK_TIME_IS_VALID(latency))){
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"configured session and NULL/READY pipeline required");return FALSE;}
 auto*clock=gst_system_clock_obtain();gst_pipeline_use_clock(pipeline,clock);
 // Live producer sources must not start before the epoch (negative running time).
 if(producer){auto now=gst_clock_get_time(clock);if(now<GstClockTime(c.epochNs)){auto wait=GstClockTime(c.epochNs)-now;
  if(wait>GST_SECOND){gst_object_unref(clock);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"session epoch too far in the future");return FALSE;}
  g_usleep(gulong(wait/1000)+1);}}
 gst_object_unref(clock);
 gst_element_set_start_time(GST_ELEMENT(pipeline),GST_CLOCK_TIME_NONE);
 gst_element_set_base_time(GST_ELEMENT(pipeline),producer?GstClockTime(c.epochNs):0);
 if(!producer)gst_pipeline_set_latency(pipeline,latency);
 return TRUE;
}

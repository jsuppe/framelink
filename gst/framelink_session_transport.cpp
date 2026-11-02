#define FL_GST_SESSION_BUILD
#include "framelink_session_transport.h"
#include "../runtime/av_auth.h"
#include "fl_gst_session_internal.h"
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

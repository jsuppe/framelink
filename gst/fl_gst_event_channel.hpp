#pragma once
#include "framelink_session_transport.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <cstring>

// One authenticated, ordered, bounded control stream. No media payload, periodic
// per-frame handshake or presentation queue. Reader owns no session/element ref.
class FlGstEventChannel {
 GSocketConnection* stream_;GCancellable* cancel_=g_cancellable_new();
 const bool producer_;const guint64 generation_;std::thread reader_;
 std::mutex mutex_,writeMutex_;std::condition_variable changed_;
 std::deque<FlGstVideoEvent> events_;std::atomic<bool> failed_{false};guint64 sent_=0;
 // Consumer: the producer ended the stream (ABORT, or EOF/error not caused locally).
 std::atomic<bool> remoteEnded_{false};std::atomic<guint> abortReason_{0};
 // Producer: the consumer closed the stream (failure not caused by a local cancel).
 std::atomic<bool> localCancel_{false},peerGone_{false};
 void fail(){if(!localCancel_)peerGone_=true;failed_=true;g_cancellable_cancel(cancel_);changed_.notify_all();}
 // One record reader at a time: the reader thread, or a consumer element that
 // collects what is already pending before it acts on a failure (decision 10).
 std::timed_mutex readMutex_;guint64 serial_=0;
 // False once the stream has ended (the record was the last one, or invalid).
 bool handle(const std::array<guint8,64>&wire){
  FlGstVideoEvent event{};event.kind=GST_READ_UINT32_BE(wire.data()+16);event.afterFrames=GST_READ_UINT64_BE(wire.data()+24);event.ptsNs=GST_READ_UINT64_BE(wire.data()+32);event.durationNs=GST_READ_UINT64_BE(wire.data()+40);
  if(memcmp(wire.data(),"FLEVT001",8)||GST_READ_UINT64_BE(wire.data()+8)!=generation_||GST_READ_UINT32_BE(wire.data()+20)||GST_READ_UINT64_BE(wire.data()+56)||serial_==G_MAXUINT64||GST_READ_UINT64_BE(wire.data()+48)!=++serial_||!valid(event)){fail();return false;}
  if(event.kind==FL_GST_VIDEO_FLUSH){fail();return false;}
  if(event.kind==FL_GST_VIDEO_ABORT){abortReason_=guint(event.ptsNs);remoteEnded_=true;fail();return false;}
  {std::lock_guard<std::mutex> lock(mutex_);if(events_.size()>=32){fail();return false;}events_.push_back(event);}changed_.notify_all();
  return true;
 }
 void readLoop(){
  auto*socket=g_socket_connection_get_socket(stream_);
  while(!g_cancellable_is_cancelled(cancel_)){
   GError*error=nullptr;
   if(!g_socket_condition_timed_wait(socket,G_IO_IN,100000,cancel_,&error)){
    bool timeout=g_error_matches(error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT);g_clear_error(&error);if(timeout)continue;if(!g_cancellable_is_cancelled(cancel_)){remoteEnded_=true;fail();}return;
   }
   std::lock_guard<std::timed_mutex> reading(readMutex_);
   if(g_cancellable_is_cancelled(cancel_))return;
   // A pending-record collection may have taken what woke this thread.
   if(!g_socket_condition_check(socket,GIOCondition(G_IO_IN|G_IO_HUP|G_IO_ERR)))continue;
   std::array<guint8,64> wire{};gsize size=0;
   if(!g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(stream_)),wire.data(),wire.size(),&size,cancel_,&error)||size!=wire.size()){g_clear_error(&error);if(!g_cancellable_is_cancelled(cancel_)){remoteEnded_=true;fail();}return;}
   if(!handle(wire))return;
  }
 }
 // Consumer: take every complete record and any end of stream already pending
 // on the socket, without waiting for more. A failing producer sends its reason
 // before it closes, and a dead one has closed, so a consumer failure caused by
 // the producer finds the end here even before the reader thread has run.
 void collectPending(){
  if(producer_)return;
  std::unique_lock<std::timed_mutex> reading(readMutex_,std::defer_lock);
  // The reader holds the lock only while taking a record already arriving.
  if(!reading.try_lock_for(std::chrono::milliseconds(5)))return;
  auto*socket=g_socket_connection_get_socket(stream_);
  while(!failed_&&!g_cancellable_is_cancelled(cancel_)&&
        g_socket_condition_check(socket,GIOCondition(G_IO_IN|G_IO_HUP|G_IO_ERR))){
   const gssize available=g_socket_get_available_bytes(socket);
   if(available<=0){remoteEnded_=true;fail();return;}          // closed (or failed) by the producer
   if(available<64)return;                                      // a record still arriving: the reader takes it
   std::array<guint8,64> wire{};gsize size=0;GError*error=nullptr;
   if(!g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(stream_)),wire.data(),wire.size(),&size,cancel_,&error)||size!=wire.size()){g_clear_error(&error);remoteEnded_=true;fail();return;}
   if(!handle(wire))return;
  }
 }
 static bool valid(const FlGstVideoEvent&e){if(e.kind==FL_GST_VIDEO_ABORT)return e.ptsNs>=FL_GST_PEER_INVALID_INPUT&&e.ptsNs<=FL_GST_PEER_OVERLOAD&&!e.durationNs;
  return e.kind>=FL_GST_VIDEO_GAP&&e.kind<=FL_GST_VIDEO_END&&e.ptsNs<=G_MAXINT64&&e.durationNs<=G_MAXINT64-e.ptsNs&&(e.kind==FL_GST_VIDEO_GAP?e.durationNs>0:e.durationNs==0);}
public:
 FlGstEventChannel(GSocketConnection*s,bool producer,guint64 generation,guint idle):stream_(G_SOCKET_CONNECTION(g_object_ref(s))),producer_(producer),generation_(generation){
  g_socket_set_timeout(g_socket_connection_get_socket(stream_),(idle+999)/1000);
  if(!producer_)reader_=std::thread([this]{readLoop();});
 }
 ~FlGstEventChannel(){cancel();if(reader_.joinable())reader_.join();g_object_unref(cancel_);g_object_unref(stream_);}
 void cancel(){
  // Consumer: record a producer end that is already pending before the local
  // cancel stops the reader, so the failure is still attributed to the peer.
  if(!localCancel_)collectPending();
  localCancel_=true;g_cancellable_cancel(cancel_);
  // Interrupt the peer as well as local waiters. Cancellation does not transfer
  // ownership of any native allocation or manufacture a successful terminal.
  g_socket_shutdown(g_socket_connection_get_socket(stream_),TRUE,TRUE,nullptr);
  changed_.notify_all();
 }
 bool failed(){
  // Event records are strictly producer -> consumer. The producer has no
  // reader thread, but must observe peer shutdown even while publishing no
  // events. Readability here is EOF or forbidden reverse traffic, both fatal.
  if(producer_&&!failed_&&!g_cancellable_is_cancelled(cancel_)&&
     g_socket_condition_check(g_socket_connection_get_socket(stream_),GIOCondition(G_IO_IN|G_IO_HUP|G_IO_ERR)))fail();
  return failed_.load();
 }
 bool producer()const{return producer_;}
 // Producer: whether the consumer closed the stream, waiting up to timeout for
 // its shutdown to arrive. False after a local cancel that came first.
 bool peerClosed(guint timeout){
  if(!producer_)return false;
  if(!failed_&&!g_cancellable_is_cancelled(cancel_)){
   auto*socket=g_socket_connection_get_socket(stream_);const auto condition=GIOCondition(G_IO_IN|G_IO_HUP|G_IO_ERR);GError*error=nullptr;
   const bool ready=timeout?bool(g_socket_condition_timed_wait(socket,condition,gint64(timeout)*1000,cancel_,&error)):g_socket_condition_check(socket,condition)!=0;
   g_clear_error(&error);if(ready&&!g_cancellable_is_cancelled(cancel_))fail();
  }
  return peerGone_.load();
 }
 // Consumer: wait up to timeout for the producer's end of stream. True with the
 // ABORT reason (0: closed without one); false if none was seen.
 bool peerEnded(guint timeout,guint*reason){
  if(producer_||!reason)return false;collectPending();std::unique_lock<std::mutex> lock(mutex_);
  changed_.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return remoteEnded_.load();});
  if(!remoteEnded_)return false;*reason=abortReason_;return true;
 }
 FlGstSessionResult send(const FlGstVideoEvent&e){
  if(!producer_||!valid(e))return FL_GST_SESSION_INVALID;
  std::lock_guard<std::mutex> lock(writeMutex_);
  if(failed()||g_cancellable_is_cancelled(cancel_))return FL_GST_SESSION_CANCELLED;
  if(sent_==G_MAXUINT64)return FL_GST_SESSION_INVALID;
  std::array<guint8,64> wire{};memcpy(wire.data(),"FLEVT001",8);GST_WRITE_UINT64_BE(wire.data()+8,generation_);GST_WRITE_UINT32_BE(wire.data()+16,e.kind);GST_WRITE_UINT64_BE(wire.data()+24,e.afterFrames);GST_WRITE_UINT64_BE(wire.data()+32,e.ptsNs);GST_WRITE_UINT64_BE(wire.data()+40,e.durationNs);GST_WRITE_UINT64_BE(wire.data()+48,++sent_);
  GError*error=nullptr;bool ok=g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(stream_)),wire.data(),wire.size(),nullptr,cancel_,&error);g_clear_error(&error);if(!ok){fail();return FL_GST_SESSION_CANCELLED;}return FL_GST_SESSION_READY;
 }
 // Non-consuming lookup of a queued END record (video-only sessions learn the
 // producer's terminal accounting from it without disturbing event order).
 bool peekEnd(FlGstVideoEvent*out){
  if(producer_||!out)return false;std::lock_guard<std::mutex> lock(mutex_);
  for(const auto&e:events_)if(e.kind==FL_GST_VIDEO_END){*out=e;return true;}
  return false;
 }
 FlGstSessionResult receive(guint timeout,FlGstVideoEvent*out){
  if(producer_||!out||timeout>60000)return FL_GST_SESSION_INVALID;
  std::unique_lock<std::mutex> lock(mutex_);
  changed_.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return !events_.empty()||failed_||g_cancellable_is_cancelled(cancel_);});
  if(!events_.empty()){*out=events_.front();events_.pop_front();return FL_GST_SESSION_READY;}
  return failed_||g_cancellable_is_cancelled(cancel_)?FL_GST_SESSION_CANCELLED:FL_GST_SESSION_TIMEOUT;
 }
};

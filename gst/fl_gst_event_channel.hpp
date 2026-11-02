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
 void fail(){failed_=true;g_cancellable_cancel(cancel_);changed_.notify_all();}
 void readLoop(){
  guint64 serial=0;auto*socket=g_socket_connection_get_socket(stream_);
  while(!g_cancellable_is_cancelled(cancel_)){
   GError*error=nullptr;
   if(!g_socket_condition_timed_wait(socket,G_IO_IN,100000,cancel_,&error)){
    bool timeout=g_error_matches(error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT);g_clear_error(&error);if(timeout)continue;if(!g_cancellable_is_cancelled(cancel_))fail();return;
   }
   std::array<guint8,64> wire{};gsize size=0;
   if(!g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(stream_)),wire.data(),wire.size(),&size,cancel_,&error)||size!=wire.size()){g_clear_error(&error);if(!g_cancellable_is_cancelled(cancel_))fail();return;}
   FlGstVideoEvent event{};event.kind=GST_READ_UINT32_BE(wire.data()+16);event.afterFrames=GST_READ_UINT64_BE(wire.data()+24);event.ptsNs=GST_READ_UINT64_BE(wire.data()+32);event.durationNs=GST_READ_UINT64_BE(wire.data()+40);
   if(memcmp(wire.data(),"FLEVT001",8)||GST_READ_UINT64_BE(wire.data()+8)!=generation_||GST_READ_UINT32_BE(wire.data()+20)||GST_READ_UINT64_BE(wire.data()+56)||serial==G_MAXUINT64||GST_READ_UINT64_BE(wire.data()+48)!=++serial||!valid(event)){fail();return;}
   if(event.kind==FL_GST_VIDEO_FLUSH){fail();return;}
   {std::lock_guard<std::mutex> lock(mutex_);if(events_.size()>=32){fail();return;}events_.push_back(event);}changed_.notify_all();
  }
 }
 static bool valid(const FlGstVideoEvent&e){return e.kind>=FL_GST_VIDEO_GAP&&e.kind<=FL_GST_VIDEO_END&&e.ptsNs<=G_MAXINT64&&e.durationNs<=G_MAXINT64-e.ptsNs&&(e.kind==FL_GST_VIDEO_GAP?e.durationNs>0:e.durationNs==0);}
public:
 FlGstEventChannel(GSocketConnection*s,bool producer,guint64 generation,guint idle):stream_(G_SOCKET_CONNECTION(g_object_ref(s))),producer_(producer),generation_(generation){
  g_socket_set_timeout(g_socket_connection_get_socket(stream_),(idle+999)/1000);
  if(!producer_)reader_=std::thread([this]{readLoop();});
 }
 ~FlGstEventChannel(){cancel();if(reader_.joinable())reader_.join();g_object_unref(cancel_);g_object_unref(stream_);}
 void cancel(){g_cancellable_cancel(cancel_);changed_.notify_all();}
 bool failed()const{return failed_.load();}
 bool producer()const{return producer_;}
 FlGstSessionResult send(const FlGstVideoEvent&e){
  if(!producer_||!valid(e))return FL_GST_SESSION_INVALID;
  std::lock_guard<std::mutex> lock(writeMutex_);
  if(failed_||g_cancellable_is_cancelled(cancel_))return FL_GST_SESSION_CANCELLED;
  if(sent_==G_MAXUINT64)return FL_GST_SESSION_INVALID;
  std::array<guint8,64> wire{};memcpy(wire.data(),"FLEVT001",8);GST_WRITE_UINT64_BE(wire.data()+8,generation_);GST_WRITE_UINT32_BE(wire.data()+16,e.kind);GST_WRITE_UINT64_BE(wire.data()+24,e.afterFrames);GST_WRITE_UINT64_BE(wire.data()+32,e.ptsNs);GST_WRITE_UINT64_BE(wire.data()+40,e.durationNs);GST_WRITE_UINT64_BE(wire.data()+48,++sent_);
  GError*error=nullptr;bool ok=g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(stream_)),wire.data(),wire.size(),nullptr,cancel_,&error);g_clear_error(&error);if(!ok){fail();return FL_GST_SESSION_CANCELLED;}return FL_GST_SESSION_READY;
 }
 FlGstSessionResult receive(guint timeout,FlGstVideoEvent*out){
  if(producer_||!out||timeout>60000)return FL_GST_SESSION_INVALID;
  std::unique_lock<std::mutex> lock(mutex_);
  changed_.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return !events_.empty()||failed_||g_cancellable_is_cancelled(cancel_);});
  if(!events_.empty()){*out=events_.front();events_.pop_front();return FL_GST_SESSION_READY;}
  return failed_||g_cancellable_is_cancelled(cancel_)?FL_GST_SESSION_CANCELLED:FL_GST_SESSION_TIMEOUT;
 }
};

#pragma once
#include "framelink_session_transport.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <chrono>

// Bounded startup worker shared by an element and its deferred GIO adapter.
// No GStreamer state callback blocks on the peer. No media queue lives here.
class FlAudioStartup {
 FlGstSession* session_;
 GWeakRef owner_;
 GCancellable* cancel_=g_cancellable_new();
 std::mutex mutex_;std::condition_variable changed_;std::thread worker_;
 bool done_=false,ok_=false;std::string error_;
 GSocketConnection* connection_=nullptr;
public:
 FlAudioStartup(FlGstSession*session,GstElement*owner,const char*path,flAudioFormat format,guint queue,guint timeout,guint idle)
 :session_(static_cast<FlGstSession*>(g_object_ref(session))){
  g_weak_ref_init(&owner_,G_OBJECT(owner));
  const std::string directory(path);
  worker_=std::thread([this,directory,format,queue,timeout,idle]{GError*error=nullptr;
   bool ok=fl_gst_session_open_local_audio(session_,TRUE,directory.c_str(),&format,queue,timeout,idle,cancel_,&error);
   flSessionConfig config{};
   if(ok)ok=fl_gst_session_get_config(session_,&config)==FL_GST_SESSION_READY&&fl_gst_session_register_track(session_,FL_GST_TRACK_AUDIO)==FL_GST_SESSION_READY&&fl_gst_session_track_ready(session_,FL_GST_TRACK_AUDIO,config.generation)==FL_GST_SESSION_READY;
   {std::lock_guard<std::mutex> lock(mutex_);ok_=ok;done_=true;if(!ok)error_=error?error->message:"audio startup failed";}
   const std::string message=error?error->message:"audio startup failed";g_clear_error(&error);changed_.notify_all();
   // Taking the weak owner can temporarily be its last reference. Do not touch
   // this state after releasing that owner; finalization may occur on this thread.
   if(!ok&&!g_cancellable_is_cancelled(cancel_)){auto*object=g_weak_ref_get(&owner_);if(object){GST_ELEMENT_ERROR(GST_ELEMENT(object),RESOURCE,OPEN_WRITE,("%s",message.c_str()),("bounded standalone session setup"));g_object_unref(object);}}
  });
 }
 ~FlAudioStartup(){cancel();if(worker_.get_id()==std::this_thread::get_id())worker_.detach();else worker_.join();g_weak_ref_clear(&owner_);if(connection_)g_object_unref(connection_);g_object_unref(cancel_);g_object_unref(session_);}
 void cancel(){g_cancellable_cancel(cancel_);fl_gst_session_cancel(session_);changed_.notify_all();}
 bool wait(GCancellable*outer,GError**error){
  std::unique_lock<std::mutex> lock(mutex_);
  while(!done_&&!g_cancellable_is_cancelled(cancel_)&&!(outer&&g_cancellable_is_cancelled(outer)))changed_.wait_for(lock,std::chrono::milliseconds(5));
  if(g_cancellable_is_cancelled(cancel_)||(outer&&g_cancellable_is_cancelled(outer))){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_CANCELLED,"audio startup cancelled");return false;}
  if(!ok_){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,error_.c_str());return false;}return true;
 }
 GOutputStream* output(GCancellable*outer,GError**error){
  if(!wait(outer,error))return nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  if(!connection_){flSessionConfig config{};
   if(fl_gst_session_get_config(session_,&config)==FL_GST_SESSION_READY)connection_=fl_gst_session_claim_audio_stream(session_,TRUE,config.generation);
   if(!connection_){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"cannot claim authenticated audio output");return nullptr;}
   g_socket_set_timeout(g_socket_connection_get_socket(connection_),(config.idleTimeoutMs+999)/1000);
  }
  return G_OUTPUT_STREAM(g_object_ref(g_io_stream_get_output_stream(G_IO_STREAM(connection_))));
 }
};
GOutputStream* fl_deferred_audio_output(const std::shared_ptr<FlAudioStartup>&);

#pragma once
#include "session_relay_contract.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace flsample {
// Explicit opt-in supervision for an already authenticated, broker-composed
// graph. Does not discover peers, compose pools, forward media, reconnect or
// retire native leases. A new object is required for every graph generation.
// start/stop/destruction are owner-serialized. The failure callback runs once on
// the monitor thread: it must not throw, block, destroy this object or call stop.
// Stop after verified aggregate drain and before normal endpoint teardown.
class SessionRelaySupervisor {
 public:
  SessionRelaySupervisor(FlGstSession* upstream,FlGstSession* downstream,
                         std::function<void()> onFailure={})
      :up(upstream?static_cast<FlGstSession*>(g_object_ref(upstream)):nullptr),
       down(downstream?static_cast<FlGstSession*>(g_object_ref(downstream)):nullptr),
       contract(up,down),failure(std::move(onFailure)){}
  ~SessionRelaySupervisor(){stop();if(up)g_object_unref(up);if(down)g_object_unref(down);}
  SessionRelaySupervisor(const SessionRelaySupervisor&)=delete;
  SessionRelaySupervisor& operator=(const SessionRelaySupervisor&)=delete;
  FlGstSessionResult start(){
    if(attempted||stopping)return FL_GST_SESSION_INVALID;
    attempted=true;
    auto r=contract.initialize();result.store(r);
    // Invalid/unrelated sessions are rejected without cancelling either graph.
    if(r!=FL_GST_SESSION_READY)return r;
    worker=std::thread([this]{
      std::unique_lock<std::mutex> lock(mutex);
      while(!stopping){
        lock.unlock();
        auto r=contract.check();
        if(r!=FL_GST_SESSION_READY){
          // Never touch channels/leases or invent END/EOS. Local cancellation
          // wakes media waiters and lets peer transports observe real failure.
          fl_gst_session_cancel(up);fl_gst_session_cancel(down);
          if(failure)failure();
          result.store(r);return;
        }
        lock.lock();
        changed.wait_for(lock,std::chrono::milliseconds(10),[this]{return stopping;});
      }
    });
    return FL_GST_SESSION_READY;
  }
  // Joins any in-progress observation/cancellation; after return the callback
  // can no longer run. A race with failure may complete cancellation first.
  // Stopping is not evidence of successful terminal accounting.
  void stop(){
    {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
    changed.notify_all();if(worker.joinable())worker.join();
  }
  FlGstSessionResult status()const{return result.load();}
 private:
  FlGstSession *up,*down;SessionRelayContract contract;
  std::function<void()> failure;
  std::atomic<FlGstSessionResult> result{FL_GST_SESSION_INVALID};
  std::thread worker;std::mutex mutex;std::condition_variable changed;
  bool attempted=false,stopping=false;
};
}

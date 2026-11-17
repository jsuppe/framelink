// flSetReceiveQueue: bounds, oldest-first delivery, overflow and shrinking.
#include "framelink.h"
#include <chrono>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static int64_t nextPts=0;
// Publish `count` frames, then give the consumer's receive thread time to take
// all of them: delivery order is what is under test, not arrival speed.
static void publish(flChannel* producer,int count){
 for(int i=0;i<count;++i){flBuffer b{};CHECK(flAcquireBuffer(producer,&b,1000)==FL_OK);CHECK(flSubmit(producer,&b,++nextPts)==FL_OK);}
 std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
// Acquire the next frame, check it, and release it.
static void expect(flChannel* consumer,int64_t pts,uint64_t sequence){
 flFrame f{};CHECK(flAcquireFrame(consumer,&f,1000)==FL_OK);
 if(f.ptsNs!=pts||f.sequence!=sequence)fprintf(stderr,"got pts %lld seq %llu, want %lld seq %llu\n",(long long)f.ptsNs,(unsigned long long)f.sequence,(long long)pts,(unsigned long long)sequence);
 CHECK(f.ptsNs==pts && f.sequence==sequence);CHECK(flRelease(consumer,&f)==FL_OK);
}
static void empty(flChannel* consumer){flFrame f{};CHECK(flAcquireFrame(consumer,&f,50)==FL_TIMEOUT);}
int main(){
 const std::string name="rxq-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 flChannel *consumer=nullptr,*producer=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,64,FL_FORMAT_BGRA8,6,FL_MAP_CPU,&consumer)==FL_OK);
 CHECK(flSetReceiveQueue(nullptr,1)==FL_INVALID);
 CHECK(flSetReceiveQueue(consumer,0)==FL_INVALID);
 CHECK(flSetReceiveQueue(consumer,FL_MAX_RECEIVE_QUEUE+1)==FL_INVALID);
 CHECK(flSetReceiveQueue(consumer,5)==FL_INVALID); // 5 + reading + drawing > pool of 6
 for(int i=0;i<100&&!producer;++i){auto r=flOpenProducer(name.c_str(),&producer);if(!producer)std::this_thread::sleep_for(std::chrono::milliseconds(10));(void)r;}
 CHECK(producer);
 CHECK(flSetReceiveQueue(producer,2)==FL_INVALID);
 // Default depth 1: latest-frame-wins, the skip shows in the sequence.
 publish(producer,3);expect(consumer,3,3);empty(consumer);
 // Depth 4: every frame, oldest first, no gap.
 CHECK(flSetReceiveQueue(consumer,4)==FL_OK);
 publish(producer,3);expect(consumer,4,4);expect(consumer,5,5);expect(consumer,6,6);empty(consumer);
 // Depth 2 overflow: the oldest unread frame is replaced.
 CHECK(flSetReceiveQueue(consumer,2)==FL_OK);
 publish(producer,3);expect(consumer,8,8);expect(consumer,9,9);empty(consumer);
 // Shrinking keeps the newest.
 CHECK(flSetReceiveQueue(consumer,4)==FL_OK);
 publish(producer,3);CHECK(flSetReceiveQueue(consumer,1)==FL_OK);expect(consumer,12,12);empty(consumer);
 // A held frame plus a full queue still leaves the producer a buffer.
 CHECK(flSetReceiveQueue(consumer,4)==FL_OK);
 publish(producer,1);flFrame held{};CHECK(flAcquireFrame(consumer,&held,1000)==FL_OK && held.ptsNs==13);
 publish(producer,4);flBuffer spare{};CHECK(flAcquireBuffer(producer,&spare,1000)==FL_OK);CHECK(flCancelBuffer(producer,&spare)==FL_OK);
 CHECK(flRelease(consumer,&held)==FL_OK);
 for(int64_t pts=14;pts<=17;++pts)expect(consumer,pts,uint64_t(pts));empty(consumer);
 flClose(producer);flClose(consumer);
 puts("receive queue: bounds, latest-wins default, oldest-first, overflow, shrink, producer headroom");
}

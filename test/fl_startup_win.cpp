#include "framelink.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int main() {
    // Pin this test to one allowed CPU and favor its caller thread. A successful
    // create must publish the endpoint even when the accept thread runs later.
    DWORD_PTR processMask=0, systemMask=0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) ||
        !SetProcessAffinityMask(GetCurrentProcess(), processMask & (~processMask + 1)) ||
        !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) return 2;
    for (unsigned i=0; i<12; ++i) {
        char name[96]; std::snprintf(name,sizeof(name),"startup-%lu-%u",GetCurrentProcessId(),i);
        flChannel *consumer=nullptr,*producer=nullptr;
        auto made=flCreateChannel(name,16,16,FL_FORMAT_BGRA8,4,&consumer);
        if(made!=FL_OK) {std::fprintf(stderr,"create: %s\n",flResultString(made));return 1;}
        // Deliberately no retry, sleep, logging or unrelated work here.
        auto opened=flOpenProducer(name,&producer);
        if(opened!=FL_OK) {
            std::fprintf(stderr,"create returned before endpoint ready: cycle=%u result=%s\n",i,flResultString(opened));
            flClose(consumer);return 1;
        }
        flClose(producer);flClose(consumer);
    }
    std::puts("12 immediate create/open cycles passed with delayed accept-thread scheduling");
}

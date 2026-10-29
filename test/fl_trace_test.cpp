#include "framelink_trace.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#include <cstring>
int main(int argc,char** argv) {
    if(argc>1 && !std::strcmp(argv[1],"bench")) {
        volatile uint64_t value=0;
        {fltrace::Scope warmup("warmup");}
        auto begin=fltrace::clockNs();
        for(unsigned i=0;i<200000;i++) {fltrace::Scope scope("benchmark"); value=value+1; scope.result(0);}
        auto elapsed=fltrace::clockNs()-begin;
        std::cout << "enabled="<<fltrace::enabled()<<" ns_per_scope="<<(double)elapsed/200000<<" value="<<value<<"\n";
        return 0;
    }
    if(!fltrace::enabled()) {
        fltrace::Scope s("disabled");
        if(s.active || !fltrace::flush())return 1;
        return 0;
    }
    const bool threadLimit=argc>1 && !std::strcmp(argv[1],"threads");
    if(threadLimit) {
        for(unsigned n=0;n<65;n++){std::thread t([]{fltrace::Scope s("threaded");});t.join();}
    }
    std::vector<std::thread> threads;
    for(unsigned t=0;t<(threadLimit?0u:4u);t++) threads.emplace_back([t]{for(unsigned n=0;n<128;n++){
        fltrace::Scope s("threaded",t);s.event.sequence=n;s.result(7);
    }});
    // Concurrent snapshots may contain a prefix only, but never partial events.
    for(int i=0;i<8;i++)if(!fltrace::flush())return 2;
    for(auto& t:threads)t.join();
    if(!fltrace::flush())return 3;
#ifdef _WIN32
    int pid=_getpid();
#else
    int pid=getpid();
#endif
    std::string path=std::string(std::getenv("FRAMELINK_TRACE"))+"."+std::to_string(pid)+".csv";
    std::ifstream f(path);std::string line;size_t rows=0;bool loss=false;
    while(std::getline(f,line)){
        if(line.find(",threaded,")!=std::string::npos)rows++;
        if(line.find(threadLimit?"# dropped_events=0,rejected_threads=1,capacity_per_thread=16":"# dropped_events=448,rejected_threads=0,capacity_per_thread=16")!=std::string::npos)loss=true;
    }
    if(rows!=64 || !loss){std::cerr<<"rows="<<rows<<" loss="<<loss<<"\n";return 4;}
    return 0;
}

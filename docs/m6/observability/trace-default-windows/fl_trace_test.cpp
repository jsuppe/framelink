#include "framelink_trace.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#include <cstring>
#include <sstream>
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
    if(argc>1 && !std::strcmp(argv[1],"details")) {
        fltrace::Event e{};e.op="policy";e.start=1;e.details=fltrace::Policy|fltrace::Device;
        e.deviceLo=UINT64_MAX;e.deviceHi=7;e.resource=2;e.usage=3;e.operations=4;e.deviceKind=5;
        e.missingUsage=6;e.unmetPreference=7;e.rejectedResource=8;e.forbiddenOperations=9;
        fltrace::record(e);
        fltrace::payload("copy",4,1234567890123ULL,1);
        if(!fltrace::flush())return 5;
#ifdef _WIN32
        int pid=_getpid();
#else
        int pid=getpid();
#endif
        std::ifstream input(std::string(std::getenv("FRAMELINK_TRACE"))+"."+std::to_string(pid)+".csv");
        std::string line;unsigned rows=0;
        while(std::getline(input,line)) {
            if(line.empty()||line[0]=='#'||line.rfind("thread,",0)==0)continue;
            std::vector<std::string> fields;std::istringstream stream(line);std::string field;
            while(std::getline(stream,field,','))fields.push_back(field);
            if(fields.size()!=27)return 6;
            if(fields[1]=="policy") {
                if(fields[10]!="96"||fields[11]!="0"||fields[15]!="18446744073709551615"||fields[24]!="9")return 7;
            }else if(fields[1]=="copy") {
                if(fields[10]!="1"||fields[11]!="1234567890123"||fields[19]!="1"||fields[24]!="0")return 8;
            }else return 9;
            ++rows;
        }
        return rows==2?0:10;
    }
    if(argc>2 && !std::strcmp(argv[1],"capacity")) {
        const size_t expected=std::stoul(argv[2]);
        for(size_t n=0;n<=expected;++n) {fltrace::Scope scope("capacity");}
        if(!fltrace::flush())return 11;
#ifdef _WIN32
        const int pid=_getpid();
#else
        const int pid=getpid();
#endif
        std::ifstream input(std::string(std::getenv("FRAMELINK_TRACE"))+"."+std::to_string(pid)+".csv");
        std::string line;size_t rows=0;bool footer=false;
        while(std::getline(input,line)) {
            if(line.find(",capacity,")!=std::string::npos)++rows;
            if(line.find("# dropped_events=1,rejected_threads=0,capacity_per_thread="+std::to_string(expected))==0)footer=true;
        }
        return rows==expected&&footer?0:12;
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

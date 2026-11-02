#pragma once
// End-of-run process costs only: never executed on a frame path.
#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
class BoundaryResources {
 const char* role;
public:
 explicit BoundaryResources(const char*r):role(r){}
 ~BoundaryResources(){
  uint64_t cpuNs=0,peakBytes=0;
#ifdef _WIN32
  FILETIME created{},exited{},kernel{},user{};
  CHECK(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user));
  auto ticks=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
  cpuNs=(ticks(kernel)+ticks(user))*100;
  PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);
  CHECK(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)));
  peakBytes=counters.PeakWorkingSetSize;
#else
  rusage usage{};CHECK(!getrusage(RUSAGE_SELF,&usage));
  cpuNs=(uint64_t(usage.ru_utime.tv_sec)+usage.ru_stime.tv_sec)*1000000000ULL+
        (uint64_t(usage.ru_utime.tv_usec)+usage.ru_stime.tv_usec)*1000;
  peakBytes=uint64_t(usage.ru_maxrss)*1024;
#endif
  const auto before=nowNs();CHECK(flTraceFlush()==FL_OK);
#ifdef _WIN32
  CHECK(benchtrace::flush());
#endif
  const auto flushNs=nowNs()-before;
  std::ofstream out(prefix+".resources-"+role+".json");
  out<<"{\"role\":\""<<role<<"\",\"process_cpu_before_flush_ns\":"<<cpuNs
     <<",\"peak_resident_before_flush_bytes\":"<<peakBytes
     <<",\"explicit_control_flush_ns\":"<<flushNs
     <<",\"automatic_exit_flush_included\":false}\n";
  out.close();CHECK(out.good());
 }
};

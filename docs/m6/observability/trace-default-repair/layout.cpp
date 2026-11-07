#include "framelink_trace.hpp"
#include <cstdio>
int main(){printf("event_bytes=%zu default_per_thread_bytes=%zu max_threads_default_bytes=%zu max_configured_bytes=%zu\n",sizeof(fltrace::Event),sizeof(fltrace::Event)*4096,sizeof(fltrace::Event)*4096*64,sizeof(fltrace::Event)*1048576*64);}

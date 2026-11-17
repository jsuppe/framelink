#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <gst/gst.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d:%s\n",__LINE__,#x);exit(1);}}while(0)
static void sample(unsigned frame){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;CHECK(K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof p));DWORD h=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&h));printf("%u,%lld,%llu,%lu,%lu\n",frame,(long long)g_get_monotonic_time(),(unsigned long long)p.PrivateUsage,p.PageFaultCount,h);fflush(stdout);}
int main(int argc,char**argv){gst_init(&argc,&argv);CHECK(argc==3);bool pooled=!strcmp(argv[1],"pool");CHECK(pooled||!strcmp(argv[1],"allocate"));unsigned count=atoi(argv[2]);const gsize bytes=3840u*2160u*4u;GstBufferPool*pool=nullptr;
if(pooled){pool=gst_buffer_pool_new();auto*c=gst_buffer_pool_get_config(pool);gst_buffer_pool_config_set_params(c,nullptr,bytes,2,2);CHECK(gst_buffer_pool_set_config(pool,c));CHECK(gst_buffer_pool_set_active(pool,TRUE));
GstBuffer *a=nullptr,*b=nullptr,*extra=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&a,nullptr)==GST_FLOW_OK);CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK);GstBufferPoolAcquireParams params{};params.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;CHECK(gst_buffer_pool_acquire_buffer(pool,&extra,&params)==GST_FLOW_EOS&&extra==nullptr);gst_buffer_unref(a);gst_buffer_unref(b);}
fprintf(stderr,"mode=%s count=%u bytes=%llu version=%s\n",argv[1],count,(unsigned long long)bytes,gst_version_string());puts("frame,monotonic_us,private_bytes,page_faults,handles");sample(0);GstBuffer*held=nullptr;
for(unsigned i=0;i<count;i++){GstBuffer*b=nullptr;if(pooled)CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK);else b=gst_buffer_new_allocate(nullptr,bytes,nullptr);CHECK(b&&b!=held);GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE)&&m.size==bytes);memset(m.data,i%251,bytes);CHECK(m.data[bytes-1]==i%251);gst_buffer_unmap(b,&m);
if(held){GstMapInfo old{};CHECK(gst_buffer_map(held,&old,GST_MAP_READ));CHECK(old.data[0]==(i-1)%251&&old.data[bytes-1]==(i-1)%251);gst_buffer_unmap(held,&old);gst_buffer_unref(held);}held=b;if((i+1)%100==0)sample(i+1);}
if(held)gst_buffer_unref(held);if(pool){CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);}sample(count+1);gst_deinit();return 0;}

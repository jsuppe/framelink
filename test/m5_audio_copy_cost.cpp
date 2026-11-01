// Isolated cost of AudioReader's gst_buffer_fill copy, not full transport cost.
// Reused hot source/destination excludes allocation, kernel copies and scheduling.
#include <gst/gst.h>
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
  gst_init(&argc, &argv);
  constexpr unsigned iterations=100000;
  for (gsize bytes : {gsize(1920), gsize(3840), gsize(4096), gsize(7680)}) {
    std::vector<unsigned char> source(bytes,0x5a);
    auto* buffer=gst_buffer_new_allocate(nullptr,bytes,nullptr);
    if (!buffer) return 1;
    auto begin=g_get_monotonic_time();
    for (unsigned i=0;i<iterations;++i) {
      source[0]=static_cast<unsigned char>(i);
      if(gst_buffer_fill(buffer,0,source.data(),bytes)!=bytes) return 2;
    }
    auto elapsed=g_get_monotonic_time()-begin;
    unsigned char check=0;
    gst_buffer_extract(buffer,0,&check,1);
    if(check!=static_cast<unsigned char>(iterations-1)) return 3;
    printf("{\"bytes\":%zu,\"iterations\":%u,\"hot_gst_buffer_fill_ns_per_call\":%.3f,\"allocation_included\":false}\n",size_t(bytes),iterations,double(elapsed)*1000/iterations);
    gst_buffer_unref(buffer);
  }
}

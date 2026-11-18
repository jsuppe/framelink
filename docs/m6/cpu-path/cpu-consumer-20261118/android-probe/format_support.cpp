#include <initializer_list>
#include <android/hardware_buffer.h>
#include <cstdio>
int main(){
 struct F{const char*n;uint32_t f;};
 for(F f:{F{"RGBA8",AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM},F{"RGB10A2",AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM},F{"YUV420",AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420}})
 for(bool render:{false,true})for(uint64_t r:{(uint64_t)AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,(uint64_t)AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN}){
  AHardwareBuffer_Desc d{};d.width=1920;d.height=1080;d.layers=1;d.format=f.f;
  d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|r|(render?AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT:0);
  AHardwareBuffer*b=nullptr;int a=AHardwareBuffer_allocate(&d,&b);if(b)AHardwareBuffer_release(b);
  printf("%-8s %-6s %-11s supported=%d allocate=%d\n",f.n,render?"render":"",r==AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN?"READ_OFTEN":"READ_RARELY",AHardwareBuffer_isSupported(&d),a);
 }
}

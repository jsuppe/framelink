#include "framelink_shared_cpu.h"
int main(void){
 flSharedCpuDescriptor d={0},out={0};unsigned char wire[FL_SHARED_CPU_WIRE_SIZE];
 d.version=FL_SHARED_CPU_VERSION;d.resourceType=FL_MEMORY_SHARED_CPU;d.format=FL_FORMAT_RGBA8;
 d.width=2;d.height=2;d.planeCount=1;d.generation=1;d.allocationId=2;d.allocationBytes=16;d.planeStride[0]=8;
 if(flSharedCpuDescriptorValidate(&d)!=FL_MEMORY_OK)return 1;
 if(flSharedCpuDescriptorEncode(&d,wire,sizeof(wire))!=FL_MEMORY_OK)return 2;
 if(flSharedCpuDescriptorDecode(wire,sizeof(wire),&out)!=FL_MEMORY_OK)return 3;
 return out.allocationId!=2||out.planeStride[0]!=8||out.resourceType!=FL_MEMORY_SHARED_CPU;
}

#define FL_EXPORTS
#include "framelink_shared_cpu.h"
#include "fl_layout.h"
#include <cstring>
namespace {
void put32(uint8_t*& p,uint32_t v){for(unsigned i=0;i<4;i++){*p++=uint8_t(v);v>>=8;}}
void put64(uint8_t*& p,uint64_t v){for(unsigned i=0;i<8;i++){*p++=uint8_t(v);v>>=8;}}
uint32_t get32(const uint8_t*& p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=uint32_t(*p++)<<(8*i);return v;}
uint64_t get64(const uint8_t*& p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=uint64_t(*p++)<<(8*i);return v;}
}
extern "C" {
flMemoryResult flSharedCpuDescriptorValidate(const flSharedCpuDescriptor* d){
    if(!d)return FL_MEMORY_INVALID;
    if(d->version!=FL_SHARED_CPU_VERSION)return FL_MEMORY_VERSION_MISMATCH;
    if(d->resourceType!=FL_MEMORY_SHARED_CPU||d->flags||d->reserved32||d->reserved64||
       !d->generation||!d->allocationId||!d->allocationBytes||d->allocationBytes>FL_SHARED_CPU_MAX_BYTES)return FL_MEMORY_INVALID;
    const bool nv12=d->format==FL_FORMAT_NV12;
    if(!nv12&&d->format!=FL_FORMAT_BGRA8&&d->format!=FL_FORMAT_RGBA8&&d->format!=FL_FORMAT_RGB10A2)return FL_MEMORY_INVALID;
    if(!fl::validGeometry(d->width,d->height,flFormat(d->format))||d->planeCount!=(nv12?2u:1u))return FL_MEMORY_INVALID;
    const uint64_t rowBytes=uint64_t(d->width)*(nv12?1:4),alignment=nv12?2:4;
    uint64_t previousEnd=0;
    for(unsigned p=0;p<4;p++){
        if(p>=d->planeCount){if(d->planeOffset[p]||d->planeStride[p])return FL_MEMORY_INVALID;continue;}
        const uint64_t rows=p?d->height/2:d->height,offset=d->planeOffset[p],stride=d->planeStride[p];
        if(offset%alignment||stride%alignment||offset<previousEnd||offset>d->allocationBytes||
           stride<rowBytes||stride>(d->allocationBytes-offset)/rows)return FL_MEMORY_INVALID;
        previousEnd=offset+stride*rows;
    }
    return FL_MEMORY_OK;
}
flMemoryResult flSharedCpuDescriptorEncode(const flSharedCpuDescriptor* d,void* data,size_t size){
    if(!data||size!=FL_SHARED_CPU_WIRE_SIZE)return FL_MEMORY_INVALID;
    const auto result=flSharedCpuDescriptorValidate(d);if(result!=FL_MEMORY_OK)return result;
    uint8_t wire[FL_SHARED_CPU_WIRE_SIZE]{};auto*p=wire;
    put32(p,0x31534346u); // FCS1, distinct from native/channel/memory-policy tags.
    put32(p,d->version);put32(p,d->resourceType);put32(p,d->format);put32(p,d->width);put32(p,d->height);put32(p,d->planeCount);put32(p,d->flags);
    put64(p,d->generation);put64(p,d->allocationId);put64(p,d->allocationBytes);put64(p,0);
    for(auto v:d->planeOffset)put64(p,v);
    for(auto v:d->planeStride)put64(p,v);
    std::memcpy(data,wire,sizeof(wire));return FL_MEMORY_OK;
}
flMemoryResult flSharedCpuDescriptorDecode(const void* data,size_t size,flSharedCpuDescriptor* out){
    if(!data||!out||size!=FL_SHARED_CPU_WIRE_SIZE)return FL_MEMORY_INVALID;
    auto*p=static_cast<const uint8_t*>(data);if(get32(p)!=0x31534346u)return FL_MEMORY_INVALID;
    flSharedCpuDescriptor d{};d.version=get32(p);if(d.version!=FL_SHARED_CPU_VERSION)return FL_MEMORY_VERSION_MISMATCH;
    d.resourceType=get32(p);d.format=get32(p);d.width=get32(p);d.height=get32(p);d.planeCount=get32(p);d.flags=get32(p);
    d.generation=get64(p);d.allocationId=get64(p);d.allocationBytes=get64(p);d.reserved64=get64(p);
    for(auto&v:d.planeOffset)v=get64(p);
    for(auto&v:d.planeStride)v=get64(p);
    auto result=flSharedCpuDescriptorValidate(&d);if(result==FL_MEMORY_OK)*out=d;return result;
}
}

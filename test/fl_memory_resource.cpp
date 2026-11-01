#include "framelink_memory.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)

static flMemoryResourceInfo query(flChannel* channel,flImage* image) {
    flMemoryResourceInfo info{};info.version=FL_MEMORY_INFO_VERSION;
    CHECK(flImageMemoryInfo(channel,image,&info,sizeof(info))==FL_OK);
    return info;
}
static void policy(const flMemoryResourceInfo& info) {
    CHECK(info.knownFields & FL_MEMORY_INFO_MAPPED_USAGE);
    CHECK(info.knownFields & FL_MEMORY_INFO_MAP_OPERATIONS);
    flMemoryRequirements req{}; CHECK(flMemoryRequirementsInit(&req)==FL_MEMORY_OK);
    req.requiredUsage=FL_MEMORY_CPU_READ;
    flMemoryPath path{1,info.resourceType,info.mappedUsage,info.mapOperations,{}};
    flMemoryDecision decision{};
    CHECK(flMemoryEvaluate(&req,&path,&decision)==(info.mapOperations ? FL_MEMORY_INCOMPATIBLE : FL_MEMORY_OK));
    CHECK(decision.forbiddenOperations==info.mapOperations);
    req.allowedOperations=info.mapOperations;
    CHECK(flMemoryEvaluate(&req,&path,&decision)==FL_MEMORY_OK);
}
int main(int argc,char**argv) {
    flFormat format=argc>1 ? flFormat(std::strtoul(argv[1],nullptr,10)) : FL_FORMAT_RGBA8;
    CHECK(format>=FL_FORMAT_BGRA8 && format<=FL_FORMAT_RGBA8);
    std::string name="fl-memory-resource-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    flChannel* consumer=nullptr;flChannel* producer=nullptr;
    auto created=flCreateChannelEx(name.c_str(),64,32,format,2,FL_MAP_CPU,&consumer);
    if(created!=FL_OK) {
        std::fprintf(stderr,"resource allocation unavailable: format=%u result=%s\n",unsigned(format),flResultString(created));
        return 77; // Hardware skip is NOT a pass; callers report it separately.
    }
    CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
    flBuffer buffer{}; CHECK(flAcquireBuffer(producer,&buffer,2000)==FL_OK);
    auto before=query(producer,buffer.image);
    CHECK(before.width==64 && before.height==32 && before.logicalFormat==uint32_t(format));
    CHECK(before.generation>0 && before.nativeFormat!=0);
    CHECK(before.interopCapabilities==(FL_MEMORY_INTEROP_RING_IMPORT | FL_MEMORY_INTEROP_BORROW_NATIVE));
    CHECK(before.reserved==0);
    CHECK(!(before.knownFields & FL_MEMORY_INFO_MAPPED_USAGE));
    CHECK(before.mappedUsage==0);
    auto unchanged=before;
    CHECK(flImageMemoryInfo(nullptr,buffer.image,&before,sizeof(before))==FL_INVALID);
    CHECK(!std::memcmp(&before,&unchanged,sizeof(before)));
    CHECK(flImageMemoryInfo(producer,buffer.image,&before,sizeof(before)-1)==FL_INVALID);
    CHECK(!std::memcmp(&before,&unchanged,sizeof(before)));
    CHECK(flImageMemoryInfo(consumer,buffer.image,&before,sizeof(before))==FL_INVALID);
    CHECK(!std::memcmp(&before,&unchanged,sizeof(before)));
    before.version=2;unchanged=before;
    CHECK(flImageMemoryInfo(producer,buffer.image,&before,sizeof(before))==FL_VERSION_MISMATCH);
    CHECK(!std::memcmp(&before,&unchanged,sizeof(before)));
    before=query(producer,buffer.image);
#if defined(_WIN32)
    CHECK(before.resourceType==FL_MEMORY_D3D11);
    CHECK(before.knownFields & FL_MEMORY_INFO_DEVICE);
    CHECK(before.deviceIdKind==FL_MEMORY_DEVICE_DXGI_LUID);
    CHECK(!(before.knownFields & FL_MEMORY_INFO_PLANE_LAYOUT));
    CHECK(before.nativePlaneCount==0 && before.planeStride[0]==0);
    CHECK(before.mapOperations==(FL_MEMORY_COPY|FL_MEMORY_READBACK|FL_MEMORY_UPLOAD));
    CHECK(before.syncTransport==(FL_MEMORY_SYNC_CPU_WAIT|FL_MEMORY_SYNC_TIMELINE));
    CHECK(before.handleTransport==FL_MEMORY_HANDLE_NT);
#elif defined(__ANDROID__)
    CHECK(before.resourceType==FL_MEMORY_AHB);
    CHECK(!(before.knownFields & (FL_MEMORY_INFO_DEVICE|FL_MEMORY_INFO_PLANE_LAYOUT)));
    CHECK(before.deviceIdKind==0 && before.nativePlaneCount==0);
    CHECK(before.directUsage & FL_MEMORY_GPU_SAMPLE);
    CHECK(before.handleTransport==FL_MEMORY_HANDLE_AHB);
    if(format==FL_FORMAT_NV12) CHECK(!(before.knownFields & FL_MEMORY_INFO_MAP_OPERATIONS));
    else CHECK(before.mapOperations==(format==FL_FORMAT_BGRA8 ? FL_MEMORY_COPY|FL_MEMORY_CONVERT : 0));
#else
    CHECK(before.resourceType==FL_MEMORY_DMABUF);
    CHECK(before.knownFields & FL_MEMORY_INFO_PLANE_LAYOUT);
    CHECK(before.nativePlaneCount==(format==FL_FORMAT_NV12 ? 2u:1u));
    CHECK(before.planeStride[0]>0 && before.mapOperations==0);
    // Producer's received descriptor does not carry allocator usage/device ID.
    CHECK(!(before.knownFields & (FL_MEMORY_INFO_DEVICE|FL_MEMORY_INFO_NATIVE_USAGE)));
    CHECK(before.handleTransport==FL_MEMORY_HANDLE_DMABUF);
#endif
#ifndef _WIN32
    CHECK(before.syncTransport==(FL_MEMORY_SYNC_CPU_WAIT|FL_MEMORY_SYNC_FD));
#endif
    flMappedImage mapped{}; CHECK(flImageMapPlanes(buffer.image,&mapped)==FL_OK);
    auto during=query(producer,buffer.image);policy(during);
    for(uint32_t p=0;p<mapped.planes;p++)
        for(uint32_t row=0;row<mapped.plane[p].rows;row++)
            std::memset(static_cast<unsigned char*>(mapped.plane[p].pixels)+row*mapped.plane[p].stride,
                        0x30+p,mapped.plane[p].rowBytes);
    auto afterWrite=query(producer,buffer.image);CHECK(afterWrite.mapOperations==during.mapOperations);
    flImageUnmap(buffer.image);
    CHECK(!(query(producer,buffer.image).knownFields & FL_MEMORY_INFO_MAPPED_USAGE));
    CHECK(flSubmit(producer,&buffer,123456789)==FL_OK);
    flFrame frame{}; CHECK(flAcquireFrame(consumer,&frame,2000)==FL_OK);
    CHECK(frame.ptsNs==123456789);
    auto received=query(consumer,frame.image);
    CHECK(received.generation==before.generation && received.nativeFormat==before.nativeFormat);
#ifdef _WIN32
    CHECK(received.mapOperations==(FL_MEMORY_COPY|FL_MEMORY_READBACK));
    CHECK(!std::memcmp(received.deviceId,before.deviceId,sizeof(received.deviceId)));
#elif !defined(__ANDROID__)
    CHECK(received.knownFields & FL_MEMORY_INFO_DEVICE);
    CHECK(received.deviceIdKind==FL_MEMORY_DEVICE_DRM_NODE);
    CHECK(received.knownFields & FL_MEMORY_INFO_NATIVE_USAGE);
    CHECK(received.knownFields & FL_MEMORY_INFO_DIRECT_USAGE);
#endif
    CHECK(flImageMapPlanes(frame.image,&mapped)==FL_OK);
    policy(query(consumer,frame.image));
    for(uint32_t p=0;p<mapped.planes;p++)
        for(uint32_t row=0;row<mapped.plane[p].rows;row++)
            for(uint32_t col=0;col<mapped.plane[p].rowBytes;col++)
                CHECK(static_cast<unsigned char*>(mapped.plane[p].pixels)[row*mapped.plane[p].stride+col]==0x30+p);
    flImageUnmap(frame.image);flRelease(consumer,&frame);
    flClose(producer);flClose(consumer);
    std::printf("resource format=%u: ownership, actual metadata, strict mapping policy, pixels/PTS and unknown-field checks passed\n",unsigned(format));
}

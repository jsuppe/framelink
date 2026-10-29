#include "framelink.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1);} } while(0)
int main(int argc,char** argv) {
    std::string arg=argc>1?argv[1]:"nv12";
    flFormat format = arg=="rgba" ? FL_FORMAT_RGBA8 : arg=="bgra" ? FL_FORMAT_BGRA8 : arg=="rgb10a2" ? FL_FORMAT_RGB10A2 : FL_FORMAT_NV12;
    auto id=std::chrono::steady_clock::now().time_since_epoch().count();
    std::string name="portable-map-"+std::to_string(id);
    flChannel *consumer=nullptr,*producer=nullptr;
    CHECK(flCreateChannelEx(name.c_str(),130,66,format,1,FL_MAP_CPU,&consumer)==FL_OK);
    for(int i=0;i<100 && !producer;i++) {
        flResult r=flOpenProducer(name.c_str(),&producer);
        CHECK(r==FL_OK || r==FL_NOT_FOUND);
        if(!producer)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(producer);
    for(unsigned frame=0;frame<8;frame++) {
        flBuffer buffer{}; CHECK(flAcquireBuffer(producer,&buffer,2000)==FL_OK);
        flBuffer reserved{}; CHECK(flAcquireBuffer(producer,&reserved,0)==FL_TIMEOUT);
        flMappedImage mapped{}; CHECK(flImageMapPlanes(buffer.image,&mapped)==FL_OK);
        CHECK(mapped.planes==(format==FL_FORMAT_NV12?2u:1u));
        for(unsigned p=0;p<mapped.planes;p++) {
            auto& plane=mapped.plane[p];
            CHECK(plane.rowBytes==(format==FL_FORMAT_NV12?130u:520u));
            CHECK(plane.rows==(p?33u:66u)); CHECK(plane.stride>=plane.rowBytes);
            for(unsigned y=0;y<plane.rows;y++)for(unsigned x=0;x<plane.rowBytes;x++)
                ((unsigned char*)plane.pixels)[y*plane.stride+x]=(unsigned char)(frame*17+p*79+y*3+x*7);
        }
        flImageUnmap(buffer.image); CHECK(flSubmit(producer,&buffer,frame)==FL_OK);
        flFrame received{}; CHECK(flAcquireFrame(consumer,&received,2000)==FL_OK);
        CHECK(received.ptsNs==frame);
        flFrame extra{}; CHECK(flAcquireFrame(consumer,&extra,0)==FL_TIMEOUT);
        flFrame forged=received;forged._readyValue++;
        CHECK(flRelease(consumer,&forged)==FL_INVALID);
        CHECK(flImageMapPlanes(received.image,&mapped)==FL_OK);
        for(unsigned p=0;p<mapped.planes;p++) {
            auto& plane=mapped.plane[p];
            for(unsigned y=0;y<plane.rows;y++)for(unsigned x=0;x<plane.rowBytes;x++)
                CHECK(((unsigned char*)plane.pixels)[y*plane.stride+x]==(unsigned char)(frame*17+p*79+y*3+x*7));
        }
        flImageUnmap(received.image); CHECK(flRelease(consumer,&received)==FL_OK);
        CHECK(flRelease(consumer,&received)==FL_INVALID);
    }
    flClose(producer);flClose(consumer);
    puts("Portable CPU plane mapping: 8 patterned frames verified");
}

#include "../src/fl_metadata.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 CHECK(sizeof(fl::HelloMsg)==24&&sizeof(fl::FrameReadyMsg)==24&&sizeof(fl::FrameReadyMetadataMsg)==64);
 CHECK(fl::compatibleFeatures(1,0));CHECK(!fl::compatibleFeatures(1|(1<<16),0));CHECK(fl::compatibleFeatures(1|(1<<16),1));CHECK(!fl::compatibleFeatures(1,1|(2<<16)));CHECK(fl::compatibleFeatures(1,7));
 fl::FrameReadyMetadataMsg wire{};wire.metadata={1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|FL_FRAME_DISCONT,1,17,9,{}};
 fl::Message message;message.type=fl::MsgType::FrameReadyMetadata;message.payload.resize(sizeof(wire));memcpy(message.payload.data(),&wire,sizeof(wire));flFrameMetadata out{};
 CHECK(fl::decodeFrame(message,true,out)&&!memcmp(&out,&wire.metadata,sizeof(out)));
 for(unsigned which=0;which<6;++which){auto bad=wire;switch(which){case 0:bad.metadata.version=9;break;case 1:bad.metadata.reserved[0]=1;break;case 2:bad.metadata.flags=8;break;case 3:bad.metadata.dtsNs=-1;break;case 4:bad.metadata.durationNs=UINT64_MAX;break;case 5:bad.metadata.streamGeneration=0;break;}memcpy(message.payload.data(),&bad,sizeof(bad));CHECK(!fl::decodeFrame(message,true,out));}
 memcpy(message.payload.data(),&wire,sizeof(wire));message.payload.push_back(0);CHECK(!fl::decodeFrame(message,true,out));message.payload.resize(sizeof(wire)-1);CHECK(!fl::decodeFrame(message,true,out));
 message.type=fl::MsgType::FrameReady;message.payload.resize(sizeof(fl::FrameReadyMsg));CHECK(fl::decodeFrame(message,false,out)&&out.version==0);CHECK(!fl::decodeFrame(message,true,out));
 puts("metadata wire sizes, feature agreement, canonical validation and malformed messages pass");
}

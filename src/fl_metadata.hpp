#pragma once
#include "fl_protocol.h"
#include "fl_ipc.h"
#include <climits>
namespace fl {
constexpr uint32_t FrameMetadataFeature=1u;
inline uint32_t localFeatures(uint32_t flags){return FrameMetadataFeature|((flags&FL_REQUIRE_FRAME_METADATA)?FrameMetadataFeature<<16:0);}
inline bool compatibleFeatures(uint32_t local,uint32_t peer){return !((local>>16)&~(peer&0xffffu))&&!((peer>>16)&~(local&0xffffu));}
inline flResult validateMetadata(const flFrameMetadata& m){
 if(m.version!=FL_FRAME_METADATA_VERSION)return FL_VERSION_MISMATCH;
 if((m.flags&~(FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION|FL_FRAME_DISCONT))||m.reserved[0]||m.reserved[1]||!m.streamGeneration)return FL_INVALID;
 if((m.flags&FL_FRAME_HAS_DTS)?m.dtsNs<0:m.dtsNs!=0)return FL_INVALID;
 if((m.flags&FL_FRAME_HAS_DURATION)?m.durationNs>INT64_MAX:m.durationNs!=0)return FL_INVALID;
 return FL_OK;
}
inline const FrameReadyMsg* decodeFrame(const Message& message,bool required,flFrameMetadata& metadata){
 metadata={};
 if(message.type==MsgType::FrameReady){
  return !required&&message.payload.size()==sizeof(FrameReadyMsg)?message.as<FrameReadyMsg>():nullptr;
 }
 if(message.type!=MsgType::FrameReadyMetadata||message.payload.size()!=sizeof(FrameReadyMetadataMsg))return nullptr;
 const auto*extended=message.as<FrameReadyMetadataMsg>();
 if(validateMetadata(extended->metadata)!=FL_OK)return nullptr;
 metadata=extended->metadata;return &extended->frame;
}
inline bool sendFrame(Channel& link,const FrameReadyMsg& frame,const flFrameMetadata& metadata,int fence=-1){
 FrameReadyMetadataMsg extended{frame,metadata};
 const auto type=metadata.version?MsgType::FrameReadyMetadata:MsgType::FrameReady;
 const void*data=metadata.version?(const void*)&extended:(const void*)&frame;
 size_t size=metadata.version?sizeof(extended):sizeof(frame);
#ifdef _WIN32
 (void)fence;return link.send(type,data,size);
#else
 return link.sendWithFds(type,data,size,fence>=0?&fence:nullptr,fence>=0?1:0);
#endif
}
}

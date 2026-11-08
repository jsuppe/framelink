// Explicit CPU-only copy-in. No implicit GPU mapping, conversion or fallback.
static gboolean sinkSharedCpuStart(GstFrameLinkSink*self){
 if(!self->session||self->ch||self->nativeMemory||self->memoryPolicyApplied||self->slots||
    fl_gst_session_get_config(self->session,&self->sessionConfig)!=FL_GST_SESSION_READY||
    !self->sessionConfig.videoTrackId||self->sessionConfig.audioTrackId||
    fl_gst_session_has_event_stream(self->session,self->sessionConfig.generation))return FALSE;
 self->cpuChannel=fl_gst_session_claim_shared_cpu(self->session,TRUE,self->sessionConfig.generation);if(!self->cpuChannel)return FALSE;
 flSessionConfig cfg{};uint32_t ops=0,flags=0;int producer=0;
 if(flSharedCpuQuery(self->cpuChannel,&cfg,&ops,&flags,&producer,&self->cpuSlots)!=FL_OK||!producer||!(ops&FL_MEMORY_COPY)||
    flags!=(FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS)||cfg.generation!=self->sessionConfig.generation){sinkDisconnect(self);return FALSE;}
 if(!self->sessionRegistered){if(fl_gst_session_register_track(self->session,FL_GST_TRACK_VIDEO)!=FL_GST_SESSION_READY){sinkDisconnect(self);return FALSE;}self->sessionRegistered=TRUE;}
 self->preserveTimestamps=self->preserveFrameMetadata=TRUE;self->streamGeneration=cfg.generation;
 self->sessionFrames=0;self->sessionLastPts=-1;self->sessionHaveSegment=FALSE;self->sessionGapPending=FALSE;
 self->received=self->submitted=self->dropped=self->ringFullDrops=0;self->cpuOutstanding=0;self->haveInfo=FALSE;
 gst_base_sink_set_sync(GST_BASE_SINK(self),FALSE);
 return fl_gst_session_track_ready(self->session,FL_GST_TRACK_VIDEO,cfg.generation)==FL_GST_SESSION_READY;
}
static bool sinkSharedCpuRelease(GstFrameLinkSink*self){
 if(!self->cpuOutstanding||flSharedCpuReceiveRelease(self->cpuChannel)!=FL_OK)return false;
 --self->cpuOutstanding;return true;
}
static GstFlowReturn sinkSharedCpuRender(GstFrameLinkSink*self,GstBuffer*buffer){
 if(g_atomic_int_get(&self->cpuFlushing))return GST_FLOW_FLUSHING;
 if(!self->haveInfo)return GST_FLOW_NOT_NEGOTIATED;
 // A single memory block avoids hidden GStreamer merge copies during mapping.
 if(gst_buffer_n_memory(buffer)!=1)return GST_FLOW_ERROR;
 // Only ordinary system-memory inputs: do not silently map native GPU memory.
 for(guint i=0;i<gst_buffer_n_memory(buffer);i++)if(!gst_memory_is_type(gst_buffer_peek_memory(buffer,i),GST_ALLOCATOR_SYSMEM))return GST_FLOW_ERROR;
 guint64 pts=0;flFrameMetadata metadata{};
 if(!frameMetadataFromBuffer(buffer,&GST_BASE_SINK(self)->segment,self->streamGeneration,pts,metadata)||
    metadata.streamGeneration!=self->sessionConfig.generation||self->sessionFrames==G_MAXUINT64)return GST_FLOW_ERROR;
 if(self->sessionMarkDiscont)metadata.flags|=FL_FRAME_DISCONT;
 // The explicit CPU mode never drops/overwrites. Respect negotiated unread
 // budget, wait only for already-submitted ownership, fail on socket deadline.
 while(self->cpuOutstanding>=self->sessionConfig.videoQueueFrames)
  if(!sinkSharedCpuRelease(self))return g_atomic_int_get(&self->cpuFlushing)?GST_FLOW_FLUSHING:GST_FLOW_ERROR;
 flSharedCpuLease*lease=nullptr;flMappedImage view{};
 auto result=flSharedCpuAcquireWrite(self->cpuChannel,self->sessionFrames%self->cpuSlots,&lease,&view);
 while(result==FL_BUSY&&self->cpuOutstanding){if(!sinkSharedCpuRelease(self))return GST_FLOW_ERROR;result=flSharedCpuAcquireWrite(self->cpuChannel,self->sessionFrames%self->cpuSlots,&lease,&view);}
 if(result!=FL_OK)return GST_FLOW_ERROR;
 struct Lease{flSharedCpuLease*p;~Lease(){if(p)flSharedCpuCancelWrite(p);}} held{lease};
 GstVideoFrame input{};if(!gst_video_frame_map(&input,&self->vinfo,buffer,GST_MAP_READ))return GST_FLOW_ERROR;
 bool valid=view.planes==GST_VIDEO_FRAME_N_PLANES(&input);guint64 bytes=0;
 for(guint p=0;p<view.planes&&valid;p++){
  guint rows=p?self->sessionConfig.video.height/2:self->sessionConfig.video.height;
  guint rowBytes=self->sessionConfig.video.format==FL_FORMAT_NV12?self->sessionConfig.video.width:self->sessionConfig.video.width*4;
  valid=view.plane[p].rows==rows&&view.plane[p].rowBytes==rowBytes&&view.plane[p].stride>=rowBytes&&GST_VIDEO_FRAME_PLANE_STRIDE(&input,p)>=gint(rowBytes);
  if(!valid)break;
  for(guint y=0;y<rows;y++)memcpy(static_cast<guint8*>(view.plane[p].pixels)+gsize(y)*view.plane[p].stride,
   static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&input,p))+gsize(y)*GST_VIDEO_FRAME_PLANE_STRIDE(&input,p),rowBytes);
  bytes+=guint64(rows)*rowBytes;
 }
 gst_video_frame_unmap(&input);
 if(bytes)gsttrace::payload("gst.shared_cpu_copy_sink",gsttrace::id(self->channel),bytes,FL_MEMORY_COPY);
 if(!valid)return GST_FLOW_ERROR;
 result=flSharedCpuSubmitWithMetadata(lease,self->sessionFrames+1,pts,&metadata);held.p=nullptr;if(result!=FL_OK)return GST_FLOW_ERROR;
 ++self->cpuOutstanding;++self->sessionFrames;++self->submitted;self->sessionLastPts=pts;self->sessionGapPending=FALSE;return GST_FLOW_OK;
}
static bool sinkSharedCpuEos(GstFrameLinkSink*self){
 if(flSharedCpuSendEos(self->cpuChannel)!=FL_OK)return false;
 // Local EOS cannot tear down storage while the peer still owns final frames.
 while(self->cpuOutstanding)if(!sinkSharedCpuRelease(self))return false;
 return true;
}

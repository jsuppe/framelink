// Included after the source type. Explicit copy-out path; no native image casts.
static gboolean srcSharedCpuStart(GstFrameLinkSrc*self){
 if(!self->session||self->ch||self->nativeMemory||self->gpuReadback||self->memoryPolicyApplied||self->maxQueueAgeMs||
    fl_gst_session_get_config(self->session,&self->sessionConfig)!=FL_GST_SESSION_READY||
    !self->sessionConfig.videoTrackId||(self->sessionConfig.audioTrackId&&!fl_gst_session_has_event_stream(self->session,self->sessionConfig.generation)))return FALSE;
 auto fmt=toGstFormat(flFormat(self->sessionConfig.video.format));if(fmt==GST_VIDEO_FORMAT_UNKNOWN)return FALSE;
 self->cpuChannel=fl_gst_session_claim_shared_cpu(self->session,FALSE,self->sessionConfig.generation);
 if(!self->cpuChannel)return FALSE;
 flSessionConfig cfg{};uint32_t ops=0,flags=0,slots=0;int producer=1;
 if(flSharedCpuQuery(self->cpuChannel,&cfg,&ops,&flags,&producer,&slots)!=FL_OK||producer||
    !(ops&FL_MEMORY_COPY)||flags!=(FL_SHARED_CPU_FRAME_METADATA|FL_SHARED_CPU_ORDERED_EOS)||cfg.generation!=self->sessionConfig.generation){
  flSharedCpuClose(self->cpuChannel);self->cpuChannel=nullptr;return FALSE;
 }
 if(!self->sessionRegistered){
  if(fl_gst_session_register_track(self->session,FL_GST_TRACK_VIDEO)!=FL_GST_SESSION_READY){flSharedCpuClose(self->cpuChannel);self->cpuChannel=nullptr;return FALSE;}
  self->sessionRegistered=TRUE;
 }
 self->width=cfg.video.width;self->height=cfg.video.height;g_free(self->format);self->format=g_strdup(gst_video_format_to_string(fmt));
 self->preserveTimestamps=self->preserveFrameMetadata=TRUE;self->sessionFrames=0;self->sessionLastPts=-1;self->sessionSequence=0;
 self->pendingGapCount=0;self->videoEndSeen=FALSE;self->haveTimestamp=FALSE;
 // Materialize both bounded cache allocations before declaring track readiness.
 // Retain both temporarily so the second acquisition cannot reuse the first.
 GstVideoInfo info;gst_video_info_init(&info);
 if(!gst_video_info_set_format(&info,fmt,self->width,self->height))return FALSE;
 GstBuffer*warm[2]{};bool ready=true;
 for(unsigned i=0;i<2;i++){
  warm[i]=srcCpuOutputBuffer(self,info.size);GstMapInfo map{};
  if(!warm[i]||!gst_buffer_map(warm[i],&map,GST_MAP_WRITE)){ready=false;break;}
  memset(map.data,0,map.size);gst_buffer_unmap(warm[i],&map);
 }
 for(auto*b:warm)if(b)gst_buffer_unref(b);
 if(!ready){srcDropCpuOutput(self);return FALSE;}
 gst_base_src_set_do_timestamp(GST_BASE_SRC(self),FALSE);
 return fl_gst_session_track_ready(self->session,FL_GST_TRACK_VIDEO,cfg.generation)==FL_GST_SESSION_READY;
}
// Unlike native latest-wins handling, this path never skips submitted frames:
// every control record must refer to the exact count already delivered.
static bool srcSharedCpuEvents(GstFrameLinkSrc*self,guint kind,guint64 pts){
 if(!fl_gst_session_has_event_stream(self->session,self->sessionConfig.generation))return true;
 const auto end=g_get_monotonic_time()+gint64(self->sessionConfig.idleTimeoutMs)*1000;
 while(g_get_monotonic_time()<end){
  FlGstVideoEvent event{};auto remaining=end-g_get_monotonic_time();
  if(fl_gst_session_receive_video_event(self->session,self->sessionConfig.generation,guint(std::max<gint64>(1,(remaining+999)/1000)),&event)!=FL_GST_SESSION_READY)return false;
  if(event.afterFrames!=self->sessionFrames)return false;
  if(event.kind==FL_GST_VIDEO_GAP){if(self->pendingGapCount>=32)return false;self->pendingGaps[self->pendingGapCount++]=event;continue;}
  return event.kind==kind&&event.ptsNs==pts;
 }
 return false;
}
static GstFlowReturn srcSharedCpuCreate(GstFrameLinkSrc*self,GstBuffer**out){
 if(g_atomic_int_get(&self->flushing))return GST_FLOW_FLUSHING;
 const auto deadline=g_get_monotonic_time()+gint64(self->sessionConfig.idleTimeoutMs)*1000;
 for(;;){
  flSessionConfig cfg{};
  if(g_atomic_int_get(&self->flushing))return GST_FLOW_FLUSHING;
  if(fl_gst_session_get_config(self->session,&cfg)!=FL_GST_SESSION_READY||cfg.generation!=self->sessionConfig.generation||g_get_monotonic_time()>=deadline)return GST_FLOW_ERROR;
  const auto remaining=deadline-g_get_monotonic_time();if(remaining<=0)return GST_FLOW_ERROR;
  auto ready=flSharedCpuWaitReadable(self->cpuChannel,guint(std::min<gint64>(50,(remaining+999)/1000)));
  if(ready==FL_OK)break;if(ready!=FL_TIMEOUT)return GST_FLOW_ERROR;
 }
 flSharedCpuLease*lease=nullptr;flMappedImage view{};uint64_t seq=0;int64_t pts=0;int eos=0;
 auto result=flSharedCpuReceiveEvent(self->cpuChannel,&lease,&view,&seq,&pts,&eos);
 if(result!=FL_OK)return g_atomic_int_get(&self->flushing)?GST_FLOW_FLUSHING:GST_FLOW_ERROR;
 if(eos){
  if(!srcSharedCpuEvents(self,FL_GST_VIDEO_END,self->sessionLastPts<0?0:guint64(self->sessionLastPts))||!srcEmitGaps(self))return GST_FLOW_ERROR;
  if(fl_gst_session_video_eos(self->session,self->sessionConfig.generation,self->sessionFrames,self->sessionLastPts)!=FL_GST_SESSION_READY)return GST_FLOW_ERROR;
  return GST_FLOW_EOS;
 }
 struct Lease{flSharedCpuLease*p;~Lease(){if(p)flSharedCpuReleaseRead(p);}} held{lease};
 flFrameMetadata metadata{};
 if(flSharedCpuReadMetadata(lease,&metadata)!=FL_OK||metadata.streamGeneration!=self->sessionConfig.generation||
    seq!=self->sessionSequence+1)return GST_FLOW_ERROR;
 GstVideoInfo info;gst_video_info_init(&info);
 if(!gst_video_info_set_format(&info,toGstFormat(flFormat(self->sessionConfig.video.format)),self->width,self->height))return GST_FLOW_ERROR;
 auto*buf=srcCpuOutputBuffer(self,info.size);if(!buf)return GST_FLOW_ERROR;
 struct Buffer{GstBuffer*p;~Buffer(){if(p)gst_buffer_unref(p);}} owned{buf};
 GstMapInfo map{};if(!gst_buffer_map(buf,&map,GST_MAP_WRITE))return GST_FLOW_ERROR;
 bool valid=view.planes==GST_VIDEO_INFO_N_PLANES(&info);guint64 bytes=0;
 memset(map.data,0,map.size);
 for(guint p=0;p<view.planes&&valid;p++){
  const guint rows=p?self->height/2:self->height;
  const guint rowBytes=self->sessionConfig.video.format==FL_FORMAT_NV12?self->width:self->width*4;
  valid=p<GST_VIDEO_MAX_PLANES&&view.plane[p].rows==rows&&view.plane[p].rowBytes==rowBytes&&view.plane[p].stride>=rowBytes&&
   info.stride[p]>=gint(rowBytes)&&info.offset[p]+gsize(rows-1)*info.stride[p]+rowBytes<=map.size;
  if(!valid)break;
  for(guint y=0;y<rows;y++)memcpy(map.data+info.offset[p]+gsize(y)*info.stride[p],static_cast<const guint8*>(view.plane[p].pixels)+gsize(y)*view.plane[p].stride,rowBytes);
  bytes+=guint64(rows)*rowBytes;
 }
 gst_buffer_unmap(buf,&map);
 if(bytes)gsttrace::payload("gst.shared_cpu_copy_source",gsttrace::id(self->channel),bytes,FL_MEMORY_COPY);
 // Finish all mapped reads and return the slot before downstream sees a buffer.
 result=flSharedCpuReleaseRead(lease);held.p=nullptr;
 if(!valid||result!=FL_OK)return GST_FLOW_ERROR;
 if((!self->sessionFrames||(metadata.flags&FL_FRAME_DISCONT))&&!srcSharedCpuEvents(self,FL_GST_VIDEO_BARRIER,pts))return GST_FLOW_ERROR;
 gint64 mapped=0;
 if(fl_gst_session_map_running_time(self->session,self->sessionConfig.generation,pts,TRUE,&mapped)!=FL_GST_SESSION_READY)return GST_FLOW_ERROR;
 GST_BUFFER_PTS(buf)=mapped;
 GST_BUFFER_DTS(buf)=GST_CLOCK_TIME_NONE;GST_BUFFER_DURATION(buf)=GST_CLOCK_TIME_NONE;
 if(metadata.flags&FL_FRAME_HAS_DURATION){if(metadata.durationNs>guint64(G_MAXINT64-mapped))return GST_FLOW_ERROR;GST_BUFFER_DURATION(buf)=metadata.durationNs;}
 if(metadata.flags&FL_FRAME_HAS_DTS){
  if(fl_gst_session_map_running_time(self->session,self->sessionConfig.generation,metadata.dtsNs,TRUE,&mapped)!=FL_GST_SESSION_READY)return GST_FLOW_ERROR;
  GST_BUFFER_DTS(buf)=mapped;metadata.dtsNs=mapped;
 }
 if(!self->sessionFrames||(metadata.flags&FL_FRAME_DISCONT)){GST_BUFFER_FLAG_SET(buf,GST_BUFFER_FLAG_DISCONT);metadata.flags|=FL_FRAME_DISCONT;}
 auto*meta=(GstFrameLinkFrameMeta*)gst_buffer_add_meta(buf,frameMetaInfo(),nullptr);if(!meta)return GST_FLOW_ERROR;meta->frame=metadata;
 self->sessionSequence=seq;++self->sessionFrames;self->sessionLastPts=pts;
 self->pendingDelivery=gst_buffer_ref(buf);owned.p=nullptr;*out=buf;return GST_FLOW_OK;
}

// An independently auto-created hop is NOT a same-allocation relay. Two real
// native hops (framelinkavsink native-memory=true into framelinkavsrc): each
// must accept its own reservation, and the second must reject the first
// hop's reader-owned memory, even with valid rewritten timestamps.
namespace autorelay {
struct Hop {
 GstElement*tx=nullptr,*rx=nullptr,*sink=nullptr,*src=nullptr,*vi=nullptr,*ai=nullptr,*vo=nullptr,*ao=nullptr;
 GstBufferPool*pool=nullptr;GstFrameLinkMemoryPolicy policy=nativeAvPolicy(0,true);
 bool rejected=false; // this hop posted the ownership error
 explicit Hop(const char*tag){
  const std::string feature=relaytest::nativeFeature();GError*error=nullptr;
  tx=gst_parse_launch(("framelinkavsink name=transport native-memory=true appsrc name=vi is-live=true format=time caps=video/x-raw("+feature+
   "),format=RGBA,width=64,height=32,framerate=30/1 ! transport.video_sink appsrc name=ai is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! transport.audio_sink").c_str(),&error);
  CHECK(tx&&!error);
  rx=gst_parse_launch("framelinkavsrc name=transport native-memory=true video-format=RGBA width=64 height=32 framerate=30/1 transport.video_src ! appsink name=vo sync=false async=false enable-last-sample=false transport.audio_src ! appsink name=ao sync=false async=false enable-last-sample=false",&error);
  CHECK(rx&&!error);
  sink=gst_bin_get_by_name(GST_BIN(tx),"transport");src=gst_bin_get_by_name(GST_BIN(rx),"transport");
  vi=gst_bin_get_by_name(GST_BIN(tx),"vi");ai=gst_bin_get_by_name(GST_BIN(tx),"ai");
  vo=gst_bin_get_by_name(GST_BIN(rx),"vo");ao=gst_bin_get_by_name(GST_BIN(rx),"ao");
  gchar*directory=g_dir_make_tmp((std::string("framelink-auto-relay-")+tag+"-XXXXXX").c_str(),nullptr);CHECK(directory);
  const std::string path=std::string(directory)+"/session";g_free(directory);
  for(auto*bin:{sink,src}){
   g_object_set(bin,"session",path.c_str(),"video-format","RGBA","width",64u,"height",32u,
     "video-pool",2u,"video-queue-frames",1u,"connect-timeout-ms",2000u,"idle-timeout-ms",1000u,nullptr);
   gboolean accepted=FALSE;g_signal_emit_by_name(bin,"set-local-native-candidate",&policy,&accepted);CHECK(accepted);
  }
  g_object_set(vo,"emit-signals",TRUE,nullptr);
  g_signal_connect(vo,"propose-allocation",G_CALLBACK(nativeAvReaderAllocation),&policy);
  CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
  for(auto*bin:{sink,src}){
   auto*child=gst_bin_get_by_name(GST_BIN(bin),"video");CHECK(child);
   GstState state=GST_STATE_NULL;const auto deadline=g_get_monotonic_time()+5000000;
   do{gst_element_get_state(child,&state,nullptr,0);if(state==GST_STATE_PLAYING)break;g_usleep(1000);}while(g_get_monotonic_time()<deadline);
   CHECK(state==GST_STATE_PLAYING);gst_object_unref(child);
  }
  auto*caps=gst_caps_from_string(("video/x-raw("+feature+"),format=RGBA,width=64,height=32,framerate=30/1").c_str());
  auto*q=gst_query_new_allocation(caps,TRUE);
  guint8 wire[FL_MEMORY_REQUIREMENTS_WIRE_SIZE];CHECK(flMemoryRequirementsEncode(&policy.requirements[0],wire,sizeof wire)==FL_MEMORY_OK);
  auto*requirements=g_bytes_new(wire,sizeof wire);gst_structure_set(gst_query_writable_structure(q),"framelink-memory-requirements",G_TYPE_BYTES,requirements,nullptr);g_bytes_unref(requirements);
  gst_structure_set(gst_query_writable_structure(q),"framelink-completion-contract",G_TYPE_STRING,relaytest::producerCompletion(),nullptr);
  auto*pad=gst_element_get_static_pad(sink,"video_sink");CHECK(gst_pad_query(pad,q));
  gst_query_parse_nth_allocation_pool(q,0,&pool,nullptr,nullptr,nullptr);CHECK(pool);
  gst_query_unref(q);gst_caps_unref(caps);gst_object_unref(pad);CHECK(gst_buffer_pool_set_active(pool,TRUE));
 }
 // Send one frame from this hop's own pool; return the final reader's video
 // sample (FrameLink reader memory) after checking pixels, PCM and timing.
 GstSample* sendOwn(unsigned char value){
  GstBuffer*own=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&own,nullptr)==GST_FLOW_OK);
  CHECK(relaytest::isNativeMemory(gst_buffer_peek_memory(own,0)));relaytest::fillPoolBuffer(sink,own,value);
  GST_BUFFER_PTS(own)=37000000;GST_BUFFER_DTS(own)=36999900;GST_BUFFER_DURATION(own)=1000000;
  auto*pcm=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(pcm,0,value+1,192);GST_BUFFER_PTS(pcm)=0;GST_BUFFER_DURATION(pcm)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(vi),own)==GST_FLOW_OK);CHECK(gst_app_src_push_buffer(GST_APP_SRC(ai),pcm)==GST_FLOW_OK);
  auto*vs=gst_app_sink_try_pull_sample(GST_APP_SINK(vo),2*GST_SECOND);auto*as=gst_app_sink_try_pull_sample(GST_APP_SINK(ao),2*GST_SECOND);CHECK(vs&&as);
  auto*vb=gst_sample_get_buffer(vs);auto*ab=gst_sample_get_buffer(as);
  CHECK(GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb)-100);
  CHECK(GST_BUFFER_DURATION(vb)==1000000&&GST_BUFFER_DURATION(ab)==1000000);
  CHECK(relaytest::isReaderMemory(gst_buffer_peek_memory(vb,0)));
  auto*meta=gst_buffer_get_video_meta(vb);CHECK(meta&&meta->n_planes==1);
  GstMapInfo map{};auto*outmem=gst_buffer_peek_memory(vb,0);CHECK(gst_memory_map(outmem,&map,GST_MAP_READ));
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(map.data[meta->offset[0]+y*meta->stride[0]+x]==value);
  gst_memory_unmap(outmem,&map);guint8 bytes[192];CHECK(gst_buffer_get_size(ab)==192&&gst_buffer_extract(ab,0,bytes,192)==192);
  for(auto b:bytes)CHECK(b==value+1);
  gst_sample_unref(as);return vs;
 }
 ~Hop(){
  CHECK(gst_element_set_state(tx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(rx,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);
  CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(vo),0));
  CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
  // A bin that refused foreign storage stays failed; it is not silently reusable.
  if(rejected)for(auto*bin:{sink,src})CHECK(gst_element_set_state(bin,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE);
  for(auto*object:{vi,ai,vo,ao,sink,src,tx,rx})gst_object_unref(object);
 }
};
static void negative(){
 Hop first("first");GstSample*incoming=first.sendOwn(92);
 {
  Hop second("second");gst_sample_unref(second.sendOwn(93)); // own reservation: accepted
  // Metadata is valid; storage remains the first hop's reader-owned allocation.
  // Rewriting timestamps on a shallow copy does not confer reservation ownership.
  auto*source=gst_sample_get_buffer(incoming);const auto pts=GST_BUFFER_PTS(source);auto*foreign=gst_buffer_copy(source);
  CHECK(foreign&&gst_buffer_peek_memory(foreign,0)==gst_buffer_peek_memory(source,0));
  GST_BUFFER_PTS(foreign)=38000000;GST_BUFFER_DTS(foreign)=37999900;GST_BUFFER_DURATION(foreign)=1000000;
  const auto began=g_get_monotonic_time();CHECK(gst_app_src_push_buffer(GST_APP_SRC(second.vi),foreign)==GST_FLOW_OK);
  auto*bus=gst_element_get_bus(second.tx);auto*message=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));
  CHECK(message&&GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR);GError*failure=nullptr;gchar*debug=nullptr;gst_message_parse_error(message,&failure,&debug);
  CHECK(failure&&strstr(failure->message,"own reserved allocation"));
  const auto elapsed=g_get_monotonic_time()-began;CHECK(elapsed<1500000);
  printf("automatic second hop: own reservation pixels/PCM/37ms passed; first-hop reader rejected %.3fms (%s)\n",elapsed/1000.0,failure->message);
  g_clear_error(&failure);g_free(debug);gst_message_unref(message);gst_object_unref(bus);
  CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(second.vo),0));second.rejected=true;
  // The rejected shallow copy must not mutate the original metadata or pixels.
  CHECK(GST_BUFFER_PTS(source)==pts&&GST_BUFFER_DTS(source)==pts-100&&GST_BUFFER_DURATION(source)==1000000);
  auto*meta=gst_buffer_get_video_meta(source);auto*memory=gst_buffer_peek_memory(source,0);GstMapInfo map{};
  CHECK(meta&&gst_memory_map(memory,&map,GST_MAP_READ));
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<256;++x)CHECK(map.data[meta->offset[0]+y*meta->stride[0]+x]==92);
  gst_memory_unmap(memory,&map);
 }
 gst_sample_unref(incoming);
}
}

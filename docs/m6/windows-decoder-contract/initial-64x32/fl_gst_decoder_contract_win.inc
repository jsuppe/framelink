// Opt-in installed-codec diagnostic, not a decoder interoperability claim.
// Observe genuine decoder allocations without granting a completion contract
// on the decoder's behalf or hiding an intervening GPU copy.
static void decoderContractDiagnostic(){
 GError*error=nullptr;
 auto*pipeline=gst_parse_launch(
  "videotestsrc num-buffers=8 pattern=black ! "
  "video/x-raw,format=NV12,width=64,height=32,framerate=30/1 ! "
  "x264enc tune=zerolatency speed-preset=ultrafast key-int-max=1 ! "
  "h264parse ! d3d11h264dec ! video/x-raw(memory:D3D11Memory),format=NV12 ! "
  "appsink name=decoded sync=false max-buffers=1 drop=false",&error);
 CHECK(pipeline&&!error);
 auto*sink=gst_bin_get_by_name(GST_BIN(pipeline),"decoded");CHECK(sink);
 auto name="decoder-contract-"+std::to_string(g_get_monotonic_time());
 flChannel*consumer=nullptr,*producer=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_NV12,2,0,&consumer)==FL_OK);
 CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 auto owner=std::make_shared<FrameLinkD3DProducer>(producer);CHECK(owner->valid());
 CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstClockTime first=GST_CLOCK_TIME_NONE,previous=GST_CLOCK_TIME_NONE;
 for(unsigned n=0;n<8;++n){
  auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);
  if(!sample)avTerminal(pipeline,GST_MESSAGE_EOS);
  CHECK(sample);
  auto*buffer=gst_sample_get_buffer(sample);CHECK(buffer&&gst_buffer_n_memory(buffer)==1);
  auto*memory=gst_buffer_peek_memory(buffer,0);CHECK(gst_is_d3d11_memory(memory));
  auto*caps=gst_sample_get_caps(sample);GstVideoInfo info{};
  CHECK(gst_video_info_from_caps(&info,caps)&&GST_VIDEO_INFO_FORMAT(&info)==GST_VIDEO_FORMAT_NV12);
  CHECK(GST_VIDEO_INFO_WIDTH(&info)==64&&GST_VIDEO_INFO_HEIGHT(&info)==32);
  CHECK(GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buffer)));
  if(n){CHECK(GST_BUFFER_PTS(buffer)>previous);
   CHECK(GST_BUFFER_PTS(buffer)-first==gst_util_uint64_scale(n,GST_SECOND,30));}
  else first=GST_BUFFER_PTS(buffer);
  previous=GST_BUFFER_PTS(buffer);
  auto*texture=(ID3D11Texture2D*)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(memory));
  CHECK(texture);D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
  CHECK(desc.Format==DXGI_FORMAT_NV12&&desc.Width>=64&&desc.Height>=32);
  CHECK(!gst_mini_object_get_qdata(GST_MINI_OBJECT(memory),FrameLinkD3DProducer::key()));
  // Same memory kind and matching caps are NOT allocation/ownership agreement.
  CHECK(owner->submit(buffer,static_cast<int64_t>(GST_BUFFER_PTS(buffer)))==FL_INVALID);
  flFrame absent{};CHECK(flAcquireFrame(consumer,&absent,0)==FL_TIMEOUT);
  printf("DECODER,%u,pts=%llu,width=%u,height=%u,array=%u,mips=%u,bind=%u,misc=%u,strict=FL_INVALID\n",
   n,static_cast<unsigned long long>(GST_BUFFER_PTS(buffer)),desc.Width,desc.Height,
   desc.ArraySize,desc.MipLevels,desc.BindFlags,desc.MiscFlags);
  gst_sample_unref(sample);
 }
 CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND));
 CHECK(gst_app_sink_is_eos(GST_APP_SINK(sink)));avTerminal(pipeline,GST_MESSAGE_EOS);
 gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(sink);gst_object_unref(pipeline);
 owner.reset();flClose(consumer);
 puts("8/8 real H.264 D3D11 decoder buffers: native NV12, exact relative PTS, foreign allocation rejection; no interoperability or zero-copy claim");
}

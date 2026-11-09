// Broker-bound native producer + authenticated PCM/event integration. The
// receiver intentionally selects CPU readback: this is not end-to-end zero-copy.
#include "../gst/framelink_session_transport.h"
#include <gst/app/gstappsink.h>
static void avStreamPair(GSocketConnection**a,GSocketConnection**b){
 auto*l=g_socket_listener_new();auto port=g_socket_listener_add_any_inet_port(l,nullptr,nullptr);CHECK(port);
 std::thread peer([&]{*a=g_socket_listener_accept(l,nullptr,nullptr,nullptr);});
 auto*c=g_socket_client_new();g_socket_client_set_timeout(c,2);*b=g_socket_client_connect_to_host(c,"127.0.0.1",port,nullptr,nullptr);
 peer.join();CHECK(*a&&*b);g_object_unref(c);g_object_unref(l);
}
static void avTerminal(GstElement*p,GstMessageType expected){
 auto*bus=gst_element_get_bus(p);auto*m=gst_bus_timed_pop_filtered(bus,3*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));
 if(m&&GST_MESSAGE_TYPE(m)==GST_MESSAGE_ERROR){GError*e=nullptr;gchar*d=nullptr;gst_message_parse_error(m,&e,&d);fprintf(stderr,"native AV: %s %s\n",e->message,d?d:"");g_clear_error(&e);g_free(d);}
 CHECK(m&&GST_MESSAGE_TYPE(m)==expected);gst_message_unref(m);gst_object_unref(bus);
}
static void nativeProducerAv(unsigned mode){
 fprintf(stderr,"native AV mode %u\n",mode);
 auto*s=fl_gst_session_new(3);auto*r=fl_gst_session_new(3);
 flSessionConfig cfg{};cfg.version=2;cfg.sessionId.bytes[0]=1;cfg.clockId.bytes[0]=2;cfg.generation=1;
 cfg.videoTrackId=1;cfg.audioTrackId=2;cfg.audio={FL_PCM_S16,48000,2};cfg.audioQueueMs=80;
 cfg.video={FL_FORMAT_RGBA8,64,32,30,1};cfg.videoQueueFrames=1;cfg.connectTimeoutMs=cfg.idleTimeoutMs=1000;
 flSessionCapabilities offer{};offer.version=2;offer.videoCount=1;offer.video[0]=cfg.video;offer.audioCount=1;offer.audio[0]=cfg.audio;
 CHECK(fl_gst_session_configure(s,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
 CHECK(fl_gst_session_configure(r,&cfg,&offer,&offer)==FL_GST_SESSION_READY);
 guint8 secret[32]{};secret[0]=73;auto*key=g_bytes_new(secret,32);
 GSocketConnection*audioPeer=nullptr;
 for(bool audio:{false,true}){
  GSocketConnection*a=nullptr,*b=nullptr;avStreamPair(&a,&b);gboolean ok=FALSE;
  auto authenticate=audio?fl_gst_session_authenticate_audio_stream:fl_gst_session_authenticate_event_stream;
  std::thread peer([&]{ok=authenticate(s,a,TRUE,key,1000,nullptr,nullptr);});
  CHECK(authenticate(r,b,FALSE,key,1000,nullptr,nullptr));peer.join();CHECK(ok);
  g_object_unref(a);if(audio)audioPeer=b;else g_object_unref(b);
 }
 g_bytes_unref(key);
 flClockMapping mapping{};mapping.sourceClockId=cfg.clockId;mapping.targetClockId.bytes[0]=8;mapping.targetAnchorNs=1000000;
 CHECK(fl_gst_session_set_mapping(r,1,&mapping,0)==FL_GST_SESSION_READY);
 auto name="native-av-"+std::to_string(g_get_monotonic_time());flChannel*channel=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_REQUIRE_FRAME_METADATA,&channel)==FL_OK);
 GError*e=nullptr;
 auto*tx=gst_parse_launch("framelinkavsink name=transport native-memory=true appsrc name=vi is-live=true format=time caps=video/x-raw(memory:D3D11Memory),format=RGBA,width=64,height=32,framerate=30/1 ! transport.video_sink appsrc name=ai is-live=true format=time caps=audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2,channel-mask=(bitmask)0x3 ! transport.audio_sink",&e);CHECK(tx&&!e);
 auto*rx=gst_parse_launch("framelinkavsrc name=transport video-format=RGBA width=64 height=32 framerate=30/1 transport.video_src ! appsink name=vo sync=false async=false transport.audio_src ! appsink name=ao sync=false async=false",&e);CHECK(rx&&!e);
 auto*sink=gst_bin_get_by_name(GST_BIN(tx),"transport");auto*src=gst_bin_get_by_name(GST_BIN(rx),"transport");
 testBindNativePolicy(sink,name,FL_MEMORY_D3D11);
 gboolean adopted=FALSE;g_signal_emit_by_name(src,"adopt-channel",channel,&adopted);CHECK(adopted);
 auto*video=gst_bin_get_by_name(GST_BIN(sink),"video");g_object_set(video,"async",FALSE,"enable-last-sample",FALSE,nullptr);gst_object_unref(video);
 for(auto entry:{std::make_pair(tx,s),std::make_pair(rx,r)}){auto*c=fl_gst_session_create_context(entry.second);gst_element_set_context(entry.first,c);gst_context_unref(c);}
 auto*vi=gst_bin_get_by_name(GST_BIN(tx),"vi");auto*ai=gst_bin_get_by_name(GST_BIN(tx),"ai");auto*vo=gst_bin_get_by_name(GST_BIN(rx),"vo");auto*ao=gst_bin_get_by_name(GST_BIN(rx),"ao");
 CHECK(gst_element_set_state(rx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);CHECK(gst_element_set_state(tx,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 auto*caps=gst_caps_from_string("video/x-raw(memory:D3D11Memory),format=RGBA,width=64,height=32,framerate=30/1");
 auto*q=gst_query_new_allocation(caps,TRUE);testQueryMemoryRequirements(q,true);
 gst_structure_set(gst_query_writable_structure(q),"framelink-completion-contract",G_TYPE_STRING,"same-device-before-abandon-v1",nullptr);
 auto*pad=gst_element_get_static_pad(sink,"video_sink");CHECK(gst_pad_query(pad,q));
 GstBufferPool*pool=nullptr;guint size=0,min=0,max=0;gst_query_parse_nth_allocation_pool(q,0,&pool,&size,&min,&max);CHECK(pool&&max==2);
 gst_query_unref(q);gst_caps_unref(caps);gst_object_unref(pad);CHECK(gst_buffer_pool_set_active(pool,TRUE));
 const unsigned count=mode==2?0:8;
 for(unsigned n=0;n<count;++n){
  GstBuffer*b=nullptr;CHECK(gst_buffer_pool_acquire_buffer(pool,&b,nullptr)==GST_FLOW_OK);
  auto*mem=gst_buffer_peek_memory(b,0);CHECK(gst_is_d3d11_memory(mem));
  auto*reservation=(FrameLinkD3DReservation*)gst_mini_object_get_qdata(GST_MINI_OBJECT(mem),FrameLinkD3DProducer::key());CHECK(reservation);
  auto*texture=(ID3D11Texture2D*)gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem));CHECK(texture==flImageD3D11(reservation->buffer.image));
  ID3D11Device*d=nullptr;texture->GetDevice(&d);ID3D11DeviceContext*c=nullptr;d->GetImmediateContext(&c);
  std::vector<unsigned char>pixels(64*32*4,static_cast<unsigned char>(n+1));c->UpdateSubresource(texture,0,nullptr,pixels.data(),256,0);c->Release();d->Release();
  GST_BUFFER_PTS(b)=37000000+n*1000000;GST_BUFFER_DTS(b)=GST_BUFFER_PTS(b)-100;GST_BUFFER_DURATION(b)=1000000;
  auto*a=gst_buffer_new_allocate(nullptr,192,nullptr);gst_buffer_memset(a,0,n+31,192);GST_BUFFER_PTS(a)=n*1000000;GST_BUFFER_DURATION(a)=1000000;
  CHECK(gst_app_src_push_buffer(GST_APP_SRC(vi),b)==GST_FLOW_OK);CHECK(gst_app_src_push_buffer(GST_APP_SRC(ai),a)==GST_FLOW_OK);
  auto*vs=gst_app_sink_try_pull_sample(GST_APP_SINK(vo),2*GST_SECOND);auto*as=gst_app_sink_try_pull_sample(GST_APP_SINK(ao),2*GST_SECOND);CHECK(vs&&as);
  auto*vb=gst_sample_get_buffer(vs);auto*ab=gst_sample_get_buffer(as);
  CHECK(GST_BUFFER_PTS(vb)==38000000+n*1000000&&GST_BUFFER_DTS(vb)==GST_BUFFER_PTS(vb)-100);
  CHECK(GST_BUFFER_PTS(ab)==1000000+n*1000000&&GST_BUFFER_PTS(vb)-GST_BUFFER_PTS(ab)==37000000);
  CHECK(GST_BUFFER_DURATION(vb)==1000000&&GST_BUFFER_DURATION(ab)==1000000);
  GstMapInfo map{};CHECK(gst_buffer_map(vb,&map,GST_MAP_READ));CHECK(map.size==pixels.size());for(gsize i=0;i<map.size;++i)CHECK(map.data[i]==n+1);gst_buffer_unmap(vb,&map);
  guint8 pcm[192];CHECK(gst_buffer_extract(ab,0,pcm,192)==192);for(auto v:pcm)CHECK(v==n+31);
  gst_sample_unref(vs);gst_sample_unref(as);
 }
 if(mode==3){CHECK(g_socket_shutdown(g_socket_connection_get_socket(audioPeer),TRUE,TRUE,nullptr));avTerminal(rx,GST_MESSAGE_ERROR);}
 else{
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?vi:ai))==GST_FLOW_OK);
  auto*bus=gst_element_get_bus(rx);CHECK(!gst_bus_timed_pop_filtered(bus,30*GST_MSECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR)));gst_object_unref(bus);
  CHECK(gst_app_src_end_of_stream(GST_APP_SRC(mode%2?ai:vi))==GST_FLOW_OK);avTerminal(tx,GST_MESSAGE_EOS);avTerminal(rx,GST_MESSAGE_EOS);
  guint64 frames=999;gint64 last=999;CHECK(fl_gst_session_wait_video_eos(r,1,0,&frames,&last)==FL_GST_SESSION_READY&&frames==count&&last==(count?37000000+(count-1)*1000000:-1LL));
 }
 auto start=g_get_monotonic_time();gst_element_set_state(tx,GST_STATE_NULL);gst_element_set_state(rx,GST_STATE_NULL);CHECK(g_get_monotonic_time()-start<1000000);
 CHECK(gst_buffer_pool_set_active(pool,FALSE));gst_object_unref(pool);
 for(auto*object:{vi,ai,vo,ao,sink,src,tx,rx})gst_object_unref(object);
 g_object_unref(audioPeer);g_object_unref(s);g_object_unref(r);
 puts("native producer AV: original allocation, exact pixels/PCM, 37ms mapping and terminal semantics pass");
}

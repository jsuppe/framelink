// Exact policy agreement, independent of native-device availability.
static void nativeMemoryAgreement(GBytes*key,GBytes*wrong){
 for(unsigned mode=0;mode<20;++mode){
  fprintf(stderr,"native memory agreement mode %u\n",mode);
  Pair pair;auto*s=fl_gst_session_new(3);auto*c=fl_gst_session_new(3);
  auto cfg=config(3);auto caps=offer(cfg);
  CHECK(fl_gst_session_configure(c,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  if(mode==2)++cfg.generation;if(mode==3)++cfg.epochNs;if(mode==4)++cfg.clockId.bytes[0];
  CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  GstFrameLinkMemoryPolicy a{};a.version=1;a.sessionGeneration=cfg.generation;
  for(unsigned i=0;i<2;++i){
   CHECK(flMemoryRequirementsInit(&a.requirements[i])==FL_MEMORY_OK);
   a.requirements[i].allowedResourceTypes=FL_MEMORY_D3D11;
   a.requirements[i].allowedOperations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
   a.paths[i].version=1;a.paths[i].resourceType=FL_MEMORY_D3D11;
   a.paths[i].supportedUsage=i?FL_MEMORY_CPU_READ:FL_MEMORY_GPU_RENDER;
   a.requirements[i].requiredUsage=a.paths[i].supportedUsage;
  }
  a.paths[1].operations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
  auto b=a;b.sessionGeneration=1;
  if(mode==5)b.requirements[0].preferredUsage=FL_MEMORY_GPU_SAMPLE;
  if(mode==6)b.requirements[1].allowedOperations|=FL_MEMORY_UPLOAD;
  if(mode==7)b.paths[0].supportedUsage|=FL_MEMORY_GPU_SAMPLE;
  if(mode==8)b.paths[1].operations=FL_MEMORY_COPY;
  if(mode==9)b.reserved=1;
  if(mode==10)b.paths[0].reserved[0]=1;
  if(mode==11)b.requirements[0].reserved[0]=1;
  if(mode==12)b.requirements[0].allowedOperations=0; // consumer copy cannot bypass strict producer
  if(mode==13){for(unsigned i=0;i<2;++i){b.paths[i].resourceType=FL_MEMORY_SHARED_CPU;b.requirements[i].allowedResourceTypes=FL_MEMORY_SHARED_CPU;}}
  if(mode==14){std::swap(b.paths[0],b.paths[1]);std::swap(b.requirements[0],b.requirements[1]);}
  if(mode==18)b.version=2;
  if(mode==19)b.sessionGeneration=0;
  gboolean sr=FALSE,cr=FALSE;GError*se=nullptr,*ce=nullptr;
  std::thread server([&]{sr=fl_gst_session_authenticate_native_memory(s,pair.server,TRUE,key,&a,300,nullptr,&se);});
  if(mode==15)cr=fl_gst_session_authenticate_cpu_bootstrap(c,pair.client,FALSE,key,300,nullptr,&ce);
  else if(mode==16)cr=fl_gst_session_authenticate_event_stream(c,pair.client,FALSE,key,300,nullptr,&ce);
  else cr=fl_gst_session_authenticate_native_memory(c,pair.client,mode==17,mode==1?wrong:key,&b,300,nullptr,&ce);
  server.join();CHECK(mode?(!sr&&!cr&&se&&ce):(sr&&cr&&!se&&!ce));
  if(!mode){guint8 sent=73,got=0;gsize n=0;
   CHECK(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(pair.server)),&sent,1,&n,nullptr,nullptr)&&n==1);
   CHECK(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(pair.client)),&got,1,&n,nullptr,nullptr)&&n==1&&got==sent);
   CHECK(!fl_gst_session_has_event_stream(c,1));
  }else{flSessionConfig out{};CHECK(fl_gst_session_get_config(c,&out)==FL_GST_SESSION_CANCELLED);}
  g_clear_error(&se);g_clear_error(&ce);g_object_unref(s);g_object_unref(c);
 }
 for(bool cancel:{false,true}){
  Pair pair;auto*s=fl_gst_session_new(1);auto cfg=config(1);auto caps=offer(cfg);
  CHECK(fl_gst_session_configure(s,&cfg,&caps,&caps)==FL_GST_SESSION_READY);
  GstFrameLinkMemoryPolicy p{};p.version=1;p.sessionGeneration=1;
  for(unsigned i=0;i<2;++i){flMemoryRequirementsInit(&p.requirements[i]);p.paths[i].version=1;p.paths[i].resourceType=FL_MEMORY_D3D11;}
  auto*token=g_cancellable_new();GError*e=nullptr;std::thread interrupter;
  if(cancel)interrupter=std::thread([&]{g_usleep(20000);g_cancellable_cancel(token);});
  auto start=g_get_monotonic_time();CHECK(!fl_gst_session_authenticate_native_memory(s,pair.server,TRUE,key,&p,100,token,&e));
  CHECK(e&&g_get_monotonic_time()-start<1000000);if(interrupter.joinable())interrupter.join();
  g_clear_error(&e);g_object_unref(token);g_object_unref(s);
 }
 puts("native memory exact agreement: success, 19 mismatch/invalid/domain negatives, deadline and cancellation passed");
}

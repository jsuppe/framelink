#pragma once
// Test-only, versioned little-endian AU tape. Included after fixture options/helpers.
// At most one AU is read by the feeder at a time; never cache decoded frames.
// The runner must pin the complete tape hash before and after replay.
namespace decoder_tape {
static constexpr uint64_t magic=0x3150415441554c46ULL; // FLAUTAP1
static constexpr uint64_t endMagic=0x31444e4555414c46ULL;
static constexpr uint64_t maxAuBytes=64ULL*1024*1024;
static void put(std::ostream& f,uint64_t n){for(unsigned i=0;i<8;++i)f.put(char(n>>(8*i)));CHECK(f.good());}
static uint64_t get(std::istream& f){uint64_t n=0;for(unsigned i=0;i<8;++i){int c=f.get();CHECK(c!=std::char_traits<char>::eof());n|=uint64_t(uint8_t(c))<<(8*i);}return n;}
static std::string encodedPipeline(){return "videotestsrc num-buffers="+std::to_string(frames())+" pattern=snow ! video/x-raw,format=NV12,width="+std::to_string(inputWidth())+",height="+std::to_string(inputHeight())+",framerate="+std::to_string(o.fps)+"/1 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max="+std::to_string(o.fps)+" ! h264parse ! video/x-h264,alignment=au";}
static void validateCaps(GstCaps* c){
 CHECK(c&&gst_caps_is_fixed(c)&&gst_caps_get_size(c)==1);const auto*s=gst_caps_get_structure(c,0);
 int w=0,h=0,n=0,d=0;const char*a=gst_structure_get_string(s,"alignment");
 CHECK(gst_structure_has_name(s,"video/x-h264")&&a&&strcmp(a,"au")==0);
 CHECK(gst_structure_get_int(s,"width",&w)&&w==int(inputWidth())&&gst_structure_get_int(s,"height",&h)&&h==int(inputHeight()));
 CHECK(gst_structure_get_fraction(s,"framerate",&n,&d)&&n==int(o.fps)&&d==1);
}
static void prepare(const std::string& name){
 CHECK(!fs::exists(name));std::ofstream f(name,std::ios::binary|std::ios::out);CHECK(f.good());
 GError*error=nullptr;auto pipeline=encodedPipeline()+" ! appsink name=encoded sync=false async=false enable-last-sample=false max-buffers=1 drop=false";
 auto*pipe=gst_parse_launch(pipeline.c_str(),&error);CHECK(pipe&&!error);auto*sink=gst_bin_get_by_name(GST_BIN(pipe),"encoded");CHECK(sink);
 CHECK(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstCaps*fixed=nullptr;uint64_t origin=0;
 for(unsigned i=0;i<frames();++i){
  auto*sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);CHECK(sample);auto*c=gst_sample_get_caps(sample);validateCaps(c);
  if(!fixed){fixed=gst_caps_ref(c);auto*text=gst_caps_to_string(c);CHECK(text);const auto length=strlen(text);CHECK(length>0&&length<=65536);
   put(f,magic);put(f,1);put(f,frames());put(f,o.width);put(f,o.height);put(f,inputWidth());put(f,inputHeight());put(f,o.fps);put(f,length);f.write(text,std::streamsize(length));g_free(text);
  }else CHECK(gst_caps_is_equal(fixed,c));
  auto*b=gst_sample_get_buffer(sample);CHECK(b&&GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(b)));if(!i)origin=GST_BUFFER_PTS(b);
  CHECK(GST_BUFFER_PTS(b)>=origin&&GST_BUFFER_PTS(b)-origin==tick(i,o.fps));
  GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_READ)&&m.size>0&&m.size<=maxAuBytes);
  put(f,i);put(f,m.size);put(f,GST_BUFFER_PTS(b));put(f,GST_BUFFER_DTS(b));put(f,GST_BUFFER_DURATION(b));put(f,GST_BUFFER_FLAGS(b));put(f,GST_BUFFER_OFFSET(b));put(f,GST_BUFFER_OFFSET_END(b));
  f.write(reinterpret_cast<const char*>(m.data),std::streamsize(m.size));CHECK(f.good());gst_buffer_unmap(b,&m);gst_sample_unref(sample);
 }
 CHECK(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND)&&gst_app_sink_is_eos(GST_APP_SINK(sink)));
 auto*bus=gst_element_get_bus(pipe);auto*msg=gst_bus_timed_pop_filtered(bus,10*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(msg&&GST_MESSAGE_TYPE(msg)==GST_MESSAGE_EOS);gst_message_unref(msg);gst_object_unref(bus);
 put(f,endMagic);put(f,frames());f.flush();CHECK(f.good());f.close();CHECK(!f.fail());
 CHECK(gst_element_set_state(pipe,GST_STATE_NULL)!=GST_STATE_CHANGE_FAILURE);gst_caps_unref(fixed);gst_object_unref(sink);gst_object_unref(pipe);
 auto receipt=file("fixture-prepared.json");receipt<<"{\"schema\":1,\"prepared\":true,\"encoded_aus\":"<<frames()<<",\"tape_bytes\":"<<fs::file_size(name)<<",\"decoded_cache\":false,\"external_sha256_required\":true}\n";CHECK(receipt.good());
}
class Reader {
 std::ifstream f;GstCaps*caps_=nullptr;unsigned next=0;uint64_t origin=0;bool ended=false;
 public:
 explicit Reader(const std::string& name):f(name,std::ios::binary){
  CHECK(f.good()&&get(f)==magic&&get(f)==1&&get(f)==frames()&&get(f)==o.width&&get(f)==o.height&&get(f)==inputWidth()&&get(f)==inputHeight()&&get(f)==o.fps);
  const auto n=get(f);CHECK(n>0&&n<=65536);std::string text(size_t(n),'\0');f.read(text.data(),std::streamsize(n));CHECK(f.good()&&text.find('\0')==std::string::npos);
  caps_=gst_caps_from_string(text.c_str());validateCaps(caps_);
  // Validate the complete record structure before readiness/epoch. Seeking is
  // bounded-memory: the runner independently hashes all payload bytes. Replay
  // still checks every read, so truncation/change after this pass also fails.
  const auto records=f.tellg();CHECK(records>=std::streampos(0));
  const auto length=fs::file_size(name);
  for(unsigned i=0;i<frames();++i){
   CHECK(get(f)==i);const auto size=get(f);CHECK(size>0&&size<=maxAuBytes);
   const auto pts=get(f);get(f);get(f);const auto flags=get(f);get(f);get(f);
   CHECK(GST_CLOCK_TIME_IS_VALID(pts)&&flags<=UINT32_MAX);if(!i)origin=pts;
   CHECK(pts>=origin&&pts-origin==tick(i,o.fps));
   const auto pos=f.tellg();CHECK(pos>=std::streampos(0));const auto offset=uint64_t(std::streamoff(pos));
   CHECK(offset<=length&&size<=length-offset);f.seekg(std::streamoff(size),std::ios::cur);CHECK(f.good());
  }
  next=frames();finish();f.clear();f.seekg(records);CHECK(f.good());next=0;origin=0;ended=false;
 }
 GstCaps*caps()const{return caps_;}
 GstBuffer*read(){
  CHECK(next<frames()&&!ended);CHECK(get(f)==next);const auto size=get(f);CHECK(size>0&&size<=maxAuBytes);
  const auto pts=get(f),dts=get(f),duration=get(f),flags=get(f),offset=get(f),offsetEnd=get(f);
  CHECK(GST_CLOCK_TIME_IS_VALID(pts)&&flags<=UINT32_MAX);if(!next)origin=pts;CHECK(pts>=origin&&pts-origin==tick(next,o.fps));
  auto*b=gst_buffer_new_allocate(nullptr,gsize(size),nullptr);CHECK(b);GstMapInfo m{};CHECK(gst_buffer_map(b,&m,GST_MAP_WRITE)&&m.size==size);
  f.read(reinterpret_cast<char*>(m.data),std::streamsize(size));CHECK(f.good());gst_buffer_unmap(b,&m);
  GST_BUFFER_PTS(b)=pts;GST_BUFFER_DTS(b)=dts;GST_BUFFER_DURATION(b)=duration;GST_BUFFER_FLAGS(b)=guint(flags);GST_BUFFER_OFFSET(b)=offset;GST_BUFFER_OFFSET_END(b)=offsetEnd;++next;return b;
 }
 void finish(){CHECK(!ended&&next==frames()&&get(f)==endMagic&&get(f)==frames());CHECK(f.peek()==std::char_traits<char>::eof()&&f.eof()&&!f.bad());ended=true;}
 ~Reader(){if(caps_)gst_caps_unref(caps_);}
};
} // namespace decoder_tape

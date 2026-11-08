#include "../gst/fl_gst_egl_readback.hpp"
#include <vector>
#include <string>
#include <thread>
#include <algorithm>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s EGL=%x GL=%x\n",__LINE__,#x,eglGetError(),glGetError());exit(1);}}while(0)
static GstFrameLinkMemoryPolicy policy(){
 GstFrameLinkMemoryPolicy p{};p.version=1;p.sessionGeneration=1;
 for(unsigned i=0;i<2;i++){
  CHECK(flMemoryRequirementsInit(&p.requirements[i])==FL_MEMORY_OK);
  p.requirements[i].allowedResourceTypes=FL_MEMORY_DMABUF;
  p.requirements[i].allowedOperations=FL_MEMORY_COPY|FL_MEMORY_READBACK;
  p.paths[i].version=FL_MEMORY_VERSION;p.paths[i].resourceType=FL_MEMORY_DMABUF;
 }
 p.paths[0].operations=FL_MEMORY_COPY|FL_MEMORY_READBACK;return p;
}
int main(){
 auto p=policy();CHECK(FrameLinkEglReadback::permits(p));
 for(unsigned endpoint=0;endpoint<2;endpoint++){
  auto denied=p;denied.requirements[endpoint].allowedOperations=FL_MEMORY_COPY;CHECK(!FrameLinkEglReadback::permits(denied));
 }
 {auto denied=p;denied.paths[0].operations=FL_MEMORY_COPY;CHECK(!FrameLinkEglReadback::permits(denied));}
 auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");CHECK(platform);
 auto foreignDisplay=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);CHECK(eglInitialize(foreignDisplay,nullptr,nullptr));CHECK(eglBindAPI(EGL_OPENGL_ES_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};EGLConfig cfg{};EGLint count=0;CHECK(eglChooseConfig(foreignDisplay,attrs,&cfg,1,&count)&&count);
 EGLint size[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 auto foreignSurface=eglCreatePbufferSurface(foreignDisplay,cfg,size);auto foreignContext=eglCreateContext(foreignDisplay,cfg,EGL_NO_CONTEXT,ctx);CHECK(eglMakeCurrent(foreignDisplay,foreignSurface,foreignSurface,foreignContext));
 glClearColor(0.25f,0.5f,0.75f,1.0f);
 auto checkForeign=[&]{CHECK(eglGetCurrentDisplay()==foreignDisplay&&eglGetCurrentContext()==foreignContext);GLfloat clear[4]{};glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);CHECK(clear[0]==0.25f&&clear[1]==0.5f&&clear[2]==0.75f&&clear[3]==1.0f&&glGetError()==GL_NO_ERROR);};
 FrameLinkEglReadback reader;
 // Same adapter across formats/geometry; every source is released and destroyed
 // before the next channel, exercising absence of retained source references.
 for(auto format:{FL_FORMAT_BGRA8,FL_FORMAT_RGBA8})for(unsigned w:{1920u,3840u}){
  unsigned h=w==1920?1080:2160;
  std::string name="egl-readback-"+std::to_string(getpid())+"-"+std::to_string(w)+"-"+std::to_string(format);
  flChannel*c=nullptr,*producer=nullptr;
  CHECK(flCreateChannelEx(name.c_str(),w,h,format,1,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
  std::vector<unsigned char>expected(size_t(w)*h*4),result(expected.size());
  for(unsigned n=0;n<8;n++){
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    auto*i=expected.data()+(size_t(y)*w+x)*4;i[0]=(x+n)%256;i[1]=(y+3*n)%256;i[2]=(x+y+7*n)%256;i[3]=(x+y+n)%256;
   }
   flBuffer b{};CHECK(flAcquireBuffer(producer,&b,2000)==FL_OK);flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);
   for(unsigned y=0;y<h;y++)memcpy((char*)m.plane[0].pixels+y*m.plane[0].stride,expected.data()+size_t(y)*w*4,w*4);
   flImageUnmap(b.image);CHECK(flSubmit(producer,&b,n)==FL_OK);flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);
   if(!n){auto denied=p;denied.requirements[1].allowedOperations=0;std::fill(result.begin(),result.end(),99);
    CHECK(!reader.read(c,f,denied,result.data(),result.size()));CHECK(std::all_of(result.begin(),result.end(),[](unsigned char v){return v==99;}));
    CHECK(!reader.read(c,f,p,result.data(),result.size()-1));
   }
   // The source owns a ready frame throughout the synchronous GPU read.
   bool ok=false;
   if(n&1){std::thread worker([&]{ok=reader.read(c,f,p,result.data(),result.size());CHECK(eglGetCurrentContext()==EGL_NO_CONTEXT);});worker.join();}
   else ok=reader.read(c,f,p,result.data(),result.size());
   CHECK(ok);checkForeign();CHECK(reader.gpuCopyBytes()==expected.size()&&reader.readbackBytes()==expected.size());
   if(result!=expected){for(size_t i=0;i<result.size();i++)if(result[i]!=expected[i]){fprintf(stderr,"pixel mismatch offset=%zu expected=%u actual=%u\n",i,expected[i],result[i]);break;}CHECK(false);}
   CHECK(flRelease(c,&f)==FL_OK);
  }
  flClose(producer);flClose(c);
 }
 // Unsupported planar layouts reject rather than convert color/plane content.
 {flChannel*c=nullptr,*producer=nullptr;auto name="egl-readback-nv12-"+std::to_string(getpid());
 CHECK(flCreateChannelEx(name.c_str(),130,66,FL_FORMAT_NV12,1,FL_MAP_CPU,&c)==FL_OK);CHECK(flOpenProducer(name.c_str(),&producer)==FL_OK);
 flBuffer b{};CHECK(flAcquireBuffer(producer,&b,2000)==FL_OK);CHECK(flSubmit(producer,&b,0)==FL_OK);flFrame f{};CHECK(flAcquireFrame(c,&f,2000)==FL_OK);
 std::vector<unsigned char>pixels(130*66*4,99);CHECK(!reader.read(c,f,p,pixels.data(),pixels.size()));CHECK(std::all_of(pixels.begin(),pixels.end(),[](unsigned char v){return v==99;}));CHECK(reader.gpuCopyBytes()==0&&reader.readbackBytes()==0);
 CHECK(flRelease(c,&f)==FL_OK);flClose(producer);flClose(c);}
 reader.reset();checkForeign();reader.reset();checkForeign();
 eglMakeCurrent(foreignDisplay,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(foreignDisplay,foreignContext);eglDestroySurface(foreignDisplay,foreignSurface);eglTerminate(foreignDisplay);
 puts("32 full spatial/channel/alpha frames, permission negatives, thread migration and resize passed");
}

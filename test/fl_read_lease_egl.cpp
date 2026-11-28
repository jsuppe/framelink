#include "framelink.h"
#include "framelink_sync_fd.h"
#ifdef __ANDROID__
#include "framelink_ahb.h"
#else
#include "framelink_dmabuf.h"
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <unistd.h>
#include <spawn.h>
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/sync_file.h>
extern char** environ;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s EGL=%x GL=%x\n",__LINE__,#x,eglGetError(),glGetError());std::exit(1);}}while(0)
static int producer(const char* name,bool crash,int reacquireMs=5000) {
 flChannel* p=nullptr;CHECK(flOpenProducer(name,&p)==FL_OK);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);void* data=nullptr;uint64_t stride=0;
 CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto* pixel=(unsigned char*)data+y*stride+x*4;pixel[0]=17;pixel[1]=91;pixel[2]=203;pixel[3]=255;}
 flImageUnmap(b.image);
 flBuffer spare{},returned{};CHECK(flAcquireBuffer(p,&spare,1000)==FL_OK);
 CHECK(flSubmit(p,&b,123456)==FL_OK);
 if(crash)_exit(99);
 CHECK(flAcquireBuffer(p,&returned,reacquireMs)==FL_OK); // waits for consumer's GPU lease
 CHECK(flCancelBuffer(p,&spare)==FL_OK && flCancelBuffer(p,&returned)==FL_OK);
 flClose(p);return 0;
}
// --gpu-hang-producer helper (decision 13, a REAL GPU reset): the producer
// renders its frame on the GPU, in a robust GLES 3 context, behind a shader
// that does not finish, and submits it with the exported native fence. It
// reports its context's reset status after the watchdog.
static int produceGpuHang(const char* name) {
 EGLDisplay display=EGL_NO_DISPLAY;
#ifndef __ANDROID__
 auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
 if(platform)display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
#else
 display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
 CHECK(display!=EGL_NO_DISPLAY && eglInitialize(display,nullptr,nullptr));
 EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
 EGLConfig config{};EGLint count=0;CHECK(eglChooseConfig(display,configAttrs,&config,1,&count)&&count);
 EGLint surfaceAttrs[]={EGL_WIDTH,64,EGL_HEIGHT,32,EGL_NONE};auto surface=eglCreatePbufferSurface(display,config,surfaceAttrs);CHECK(surface!=EGL_NO_SURFACE);
 EGLint robustAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT,EGL_TRUE,EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,EGL_LOSE_CONTEXT_ON_RESET_EXT,EGL_NONE};
 auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,robustAttrs);CHECK(context!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(display,surface,surface,context));
 auto createImage=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
 auto bindImage=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
 auto createSync=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
 auto exportFence=(PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
 auto resetStatus=(GLenum(GL_APIENTRY*)())eglGetProcAddress("glGetGraphicsResetStatusEXT");
 CHECK(createImage&&bindImage&&createSync&&exportFence);
 flChannel* p=nullptr;CHECK(flOpenProducer(name,&p)==FL_OK);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
 EGLImageKHR image=EGL_NO_IMAGE_KHR;
#ifdef __ANDROID__
 auto native=(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");CHECK(native);
 EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
 image=createImage(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native(flImageAHardwareBuffer(b.image)),attrs);
#else
 flDmabuf dma{};CHECK(flImageDmabuf(b.image,&dma)==FL_OK && dma.planes==1);
 EGLint attrs[]={EGL_WIDTH,64,EGL_HEIGHT,32,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)dma.fourcc,EGL_DMA_BUF_PLANE0_FD_EXT,dma.fd[0],EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)dma.offset[0],EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)dma.stride[0],EGL_NONE};
 image=createImage(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
#endif
 CHECK(image!=EGL_NO_IMAGE_KHR);
 GLuint target=0,fbo=0;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);bindImage(GL_TEXTURE_2D,image);
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){std::fprintf(stderr,"producer: the ring buffer is not renderable here (0x%x)\n",glCheckFramebufferStatus(GL_FRAMEBUFFER));return 77;}
 auto compile=[](GLenum type,const char* source){GLuint sh=glCreateShader(type);glShaderSource(sh,1,&source,nullptr);glCompileShader(sh);GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);CHECK(ok);return sh;};
 GLuint vs=compile(GL_VERTEX_SHADER,"#version 300 es\nin vec2 position;void main(){gl_Position=vec4(position,0.0,1.0);}");
 GLuint fs=compile(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;precision highp int;uniform int count;out vec4 color;void main(){uint x=uint(gl_FragCoord.x)*2654435761u+uint(gl_FragCoord.y);for(int i=0;i<count;i++){x=x*1664525u+1013904223u;}color=vec4(float(x&255u)/255.0,0.25,0.5,1.0);}");
 GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glBindAttribLocation(program,0,"position");glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);CHECK(linked);
 glUseProgram(program);const char* iterEnv=getenv("FL_TEST_HANG_ITERATIONS");glUniform1i(glGetUniformLocation(program,"count"),iterEnv?std::atoi(iterEnv):2000000000);
 GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glViewport(0,0,64,32);
 glDrawArrays(GL_TRIANGLE_STRIP,0,4); // the frame's GPU write; does not finish
 auto sync=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(sync!=EGL_NO_SYNC_KHR);glFlush();
 int fd=exportFence(display,sync);CHECK(fd>=0);
 CHECK(flSubmitSyncFd(p,&b,fd,654321)==FL_OK);
 // Wait for the watchdog, then report the context's view.
 GLenum reset=0;for(int i=0;i<900 && !reset;++i){std::this_thread::sleep_for(std::chrono::milliseconds(100));reset=resetStatus?resetStatus():0;}
 sync_file_info info;memset(&info,0,sizeof(info));ioctl(fd,SYNC_IOC_FILE_INFO,&info);
 std::fprintf(stderr,"producer: GL reset status 0x%x; its frame's fence status %d\n",reset,info.status);
 close(fd);flClose(p);return reset?0:3;
}
int main(int argc,char** argv) {
 if(argc==3 && !std::strcmp(argv[1],"--producer-gpu-hang"))return produceGpuHang(argv[2]);
 if(argc==3)return producer(argv[2],!std::strcmp(argv[1],"--producer-crash"));
 if(argc==4)return producer(argv[2],false,std::atoi(argv[3]));
 // --gpu-hang-producer (decision 13, a REAL GPU reset): the producer's frame is
 // written on the GPU behind a hang. The consumer records whether that frame
 // is delivered or reported as device loss, and the fence status.
 if(argc==2 && !std::strcmp(argv[1],"--gpu-hang-producer")){
  std::string name="egl-hang-producer-"+std::to_string(getpid());flChannel* c=nullptr;
  CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_GPU_RENDER|FL_DEFER_RELEASE,&c)==FL_OK);
  pid_t child=0;char mode[]="--producer-gpu-hang";char* args[]={argv[0],mode,const_cast<char*>(name.c_str()),nullptr};
  CHECK(posix_spawn(&child,argv[0],nullptr,nullptr,args,environ)==0);
  const auto start=std::chrono::steady_clock::now();auto ms=[&]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
  flFrame f{};flResult r=FL_TIMEOUT;while(r==FL_TIMEOUT && ms()<90000)r=flAcquireFrame(c,&f,100);
  std::fprintf(stderr,"consumer: flAcquireFrame=%s after %.0f ms%s\n",flResultString(r),ms(),r==FL_OK?" (the frame written behind the hang was DELIVERED)":"");
  if(r==FL_OK){void* data=nullptr;uint64_t stride=0;if(flImageMap(f.image,&data,&stride)==FL_OK){auto* px=(unsigned char*)data;
    std::fprintf(stderr,"consumer: delivered frame pixel 0 = %u,%u,%u,%u (a completed write has 64,128 in G,B)\n",px[0],px[1],px[2],px[3]);flImageUnmap(f.image);}
   flRelease(c,&f);}
  int status=0;CHECK(waitpid(child,&status,0)==child);
  std::fprintf(stderr,"consumer: producer exit %d\n",WIFEXITED(status)?WEXITSTATUS(status):-1);
  flClose(c);
  pid_t again=0;char* plain[]={argv[0],nullptr};CHECK(posix_spawn(&again,argv[0],nullptr,nullptr,plain,environ)==0);
  CHECK(waitpid(again,&status,0)==again);std::fprintf(stderr,"recovery: fresh run exit %d\n",WIFEXITED(status)?WEXITSTATUS(status):-1);
  CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
  return r==FL_DEVICE_LOST?0:2;
 }
 bool crash=argc==2 && !std::strcmp(argv[1],"--producer-death");
 bool delayed=argc==2 && !std::strcmp(argv[1],"--delayed-completion");
 // --gpu-busy-completion: the lease's sampling draw queues behind a real GPU
 // workload (calibrated draws of a long fragment loop), so its exported native
 // fence signals late because the GPU is busy, not because of a host gate. A raw
 // check (no FrameLink) first proves the fence stays unready for the hold.
 bool busy=argc==2 && !std::strcmp(argv[1],"--gpu-busy-completion");
 // --gpu-hang-consumer (M6 decision 13, a REAL GPU reset; run only on a free
 // host): the consumer's sampling draw queues behind a shader that does not
 // finish, in a robust context, so the driver's watchdog must reset it. The
 // lease's exported fence then completes - FrameLink must report
 // FL_DEVICE_LOST (an error completion), never retire it as a good read, and
 // still hand the producer its slot back. Afterwards a fresh normal run of
 // this test must pass (recovery).
 bool hang=argc==2 && !std::strcmp(argv[1],"--gpu-hang-consumer");
 EGLDisplay display=EGL_NO_DISPLAY;
#ifndef __ANDROID__
 auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
 if(platform)display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
#else
 display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
 if(display==EGL_NO_DISPLAY || !eglInitialize(display,nullptr,nullptr)){puts("SKIP: EGL display unavailable");return 77;}
 const char* extensions=eglQueryString(display,EGL_EXTENSIONS);
 if(!extensions || !strstr(extensions,"EGL_ANDROID_native_fence_sync")){puts("SKIP: native fence export unavailable");return 77;}
 EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
 EGLConfig config{};EGLint count=0;CHECK(eglChooseConfig(display,configAttrs,&config,1,&count)&&count);
 EGLint surfaceAttrs[]={EGL_WIDTH,64,EGL_HEIGHT,32,EGL_NONE};
 auto surface=eglCreatePbufferSurface(display,config,surfaceAttrs);CHECK(surface!=EGL_NO_SURFACE);
 if(hang && !strstr(extensions,"EGL_EXT_create_context_robustness")){puts("SKIP: EGL_EXT_create_context_robustness unavailable");return 77;}
 EGLint plainAttrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 EGLint robustAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT,EGL_TRUE,
     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,EGL_LOSE_CONTEXT_ON_RESET_EXT,EGL_NONE};
 auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,hang?robustAttrs:plainAttrs);CHECK(context!=EGL_NO_CONTEXT);
 CHECK(eglMakeCurrent(display,surface,surface,context));
 std::printf("EGL vendor=%s GL vendor=%s renderer=%s\n",eglQueryString(display,EGL_VENDOR),glGetString(GL_VENDOR),glGetString(GL_RENDERER));
 auto createImage=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
 auto destroyImage=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
 auto bindImage=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
 auto createSync=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
 auto destroySync=(PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
 auto exportFence=(PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
 CHECK(createImage&&destroyImage&&bindImage&&createSync&&destroySync&&exportFence);
 auto waitSync=(PFNEGLWAITSYNCKHRPROC)eglGetProcAddress("eglWaitSyncKHR");
 auto signalSync=(PFNEGLSIGNALSYNCKHRPROC)eglGetProcAddress("eglSignalSyncKHR");
 EGLSyncKHR gate=EGL_NO_SYNC_KHR;
 if(delayed){
  // Probe server-side support, not just the presence of extension symbols.
  // Some drivers only permit fence (not reusable) syncs in eglWaitSyncKHR.
  if(strstr(extensions,"EGL_KHR_reusable_sync")&&waitSync&&signalSync)gate=createSync(display,EGL_SYNC_REUSABLE_KHR,nullptr);
  bool supported=gate!=EGL_NO_SYNC_KHR&&waitSync(display,gate,0)==EGL_TRUE;
  EGLint why=eglGetError();
  // A successful wait call is insufficient: prove the exported native fence
  // stays unsignalled behind this gate, without any FrameLink participation.
  if(supported){
   auto probe=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(probe!=EGL_NO_SYNC_KHR);glFlush();
   int probeFd=exportFence(display,probe);CHECK(probeFd>=0);
   pollfd pending{probeFd,POLLIN,0};int ready=poll(&pending,1,20);CHECK(ready>=0);
   if(ready!=0){std::fprintf(stderr,"injection unavailable: native fence became ready behind unsignalled reusable gate (revents=%x)\n",pending.revents);supported=false;}
   CHECK(signalSync(display,gate,EGL_SIGNALED_KHR));CHECK(poll(&pending,1,2000)==1&&(pending.revents&POLLIN));
   close(probeFd);destroySync(display,probe);
  }
  if(gate!=EGL_NO_SYNC_KHR){CHECK(signalSync(display,gate,EGL_SIGNALED_KHR));destroySync(display,gate);gate=EGL_NO_SYNC_KHR;}
  if(!supported){std::printf("SKIP: controllable reusable-sync server wait unavailable EGL=%x\n",why);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return 77;}
 }
 std::string name="egl-lease-"+std::to_string(getpid());flChannel *c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_GPU_RENDER|FL_DEFER_RELEASE,&c)==FL_OK);
 pid_t child=0;char reacquire[]="120000";
 char* args[]={argv[0],const_cast<char*>(crash?"--producer-crash":"--producer"),const_cast<char*>(name.c_str()),hang?reacquire:nullptr,nullptr};
 CHECK(posix_spawn(&child,argv[0],nullptr,nullptr,args,environ)==0);
 flFrame frame{};uint64_t lease=0;CHECK(flAcquireFrame(c,&frame,1000)==FL_OK && flLeaseFrame(c,&frame,&lease)==FL_OK);
 EGLImageKHR image=EGL_NO_IMAGE_KHR;
#ifdef __ANDROID__
 auto native=(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");CHECK(native);
 EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
 image=createImage(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native(flImageAHardwareBuffer(frame.image)),attrs);
#else
 flDmabuf dma{};CHECK(flImageDmabuf(frame.image,&dma)==FL_OK && dma.planes==1);
 EGLint attrs[]={EGL_WIDTH,64,EGL_HEIGHT,32,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)dma.fourcc,EGL_DMA_BUF_PLANE0_FD_EXT,dma.fd[0],EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)dma.offset[0],EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)dma.stride[0],EGL_NONE};
 image=createImage(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
#endif
 CHECK(image!=EGL_NO_IMAGE_KHR);
 GLuint texture=0,output=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);bindImage(GL_TEXTURE_EXTERNAL_OES,image);CHECK(glGetError()==GL_NO_ERROR);
 glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 auto shader=[](GLenum type,const char* source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);std::fprintf(stderr,"%s",log);}CHECK(ok);return s;};
 GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 position;varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0.0,1.0);}");
 GLuint fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision mediump float;varying vec2 uv;uniform samplerExternalOES source;void main(){gl_FragColor=texture2D(source,uv);}");
 GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glBindAttribLocation(program,0,"position");glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);CHECK(linked);glUseProgram(program);
 glUniform1i(glGetUniformLocation(program,"source"),0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);
 GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glViewport(0,0,64,32);
 // GPU workload for --gpu-busy-completion: BUSY_SIZE^2 fragments of a
 // 2048-step loop per draw; the draw count is calibrated to hold the queue for
 // about BUSY_MS. Each draw stays short (well under GPU watchdog limits).
 constexpr int BUSY_SIZE=256;constexpr double BUSY_MS=300,HOLD_MS=100;
 GLuint busyProgram=0,busyTarget=0,busyFbo=0;unsigned busyDraws=0;
 auto now=[]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();};
 auto busyWork=[&](unsigned draws){glBindFramebuffer(GL_FRAMEBUFFER,busyFbo);glUseProgram(busyProgram);glViewport(0,0,BUSY_SIZE,BUSY_SIZE);
  glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE); // not opaque: tilers (Mali FPK) must not discard overdrawn draws
  for(unsigned i=0;i<draws;++i){glUniform1f(glGetUniformLocation(busyProgram,"seed"),float(i));glDrawArrays(GL_TRIANGLE_STRIP,0,4);}
  glDisable(GL_BLEND);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glUseProgram(program);glViewport(0,0,64,32);CHECK(glGetError()==GL_NO_ERROR);};
 if(busy){
  GLuint bfs=shader(GL_FRAGMENT_SHADER,"precision highp float;varying vec2 uv;uniform float seed;void main(){float v=uv.x+uv.y*0.37+seed;for(int i=0;i<2048;i++){v=fract(sin(v*12.9898+float(i)*0.001)*43758.5453);}gl_FragColor=vec4(v,v,v,1.0);}");
  busyProgram=glCreateProgram();glAttachShader(busyProgram,vs);glAttachShader(busyProgram,bfs);glBindAttribLocation(busyProgram,0,"position");glLinkProgram(busyProgram);GLint ok=0;glGetProgramiv(busyProgram,GL_LINK_STATUS,&ok);CHECK(ok);glDeleteShader(bfs);
  glGenTextures(1,&busyTarget);glBindTexture(GL_TEXTURE_2D,busyTarget);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,BUSY_SIZE,BUSY_SIZE,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  glGenFramebuffers(1,&busyFbo);glBindFramebuffer(GL_FRAMEBUFFER,busyFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,busyTarget,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);
  busyWork(1);glFinish(); // warm-up (compile, clocks)
  double per=0;for(unsigned n=1;n<=4096;n*=2){double t=now();busyWork(n);glFinish();double e=now()-t;if(e>=20){per=e/n;break;}}
  CHECK(per>0);busyDraws=unsigned(BUSY_MS/per)+1;
  // Raw control without FrameLink: the fence after the workload stays unready.
  double t=now();busyWork(busyDraws);auto probe=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(probe!=EGL_NO_SYNC_KHR);glFlush();
  int probeFd=exportFence(display,probe);CHECK(probeFd>=0);pollfd pending{probeFd,POLLIN,0};int ready=poll(&pending,1,int(HOLD_MS));CHECK(ready>=0);
  if(ready!=0){kill(child,SIGKILL);waitpid(child,nullptr,0);std::printf("SKIP: GPU workload did not hold the native fence for %.0f ms (draws=%u, %.3f ms each)\n",HOLD_MS,busyDraws,per);return 77;}
  CHECK(poll(&pending,1,5000)==1);std::fprintf(stderr,"busy calibration: %.3f ms/draw, %u draws, raw fence ready after %.1f ms\n",per,busyDraws,now()-t);
  close(probeFd);destroySync(display,probe);
 }
 if(delayed){gate=createSync(display,EGL_SYNC_REUSABLE_KHR,nullptr);CHECK(gate!=EGL_NO_SYNC_KHR);CHECK(waitSync(display,gate,0));}
 if(hang){
  auto resetStatus=(GLenum(GL_APIENTRY*)())eglGetProcAddress("glGetGraphicsResetStatusEXT");
  // FL_TEST_HANG_ITERATIONS (default 2e9) sets the loop; FL_TEST_HANG_PROBE=1
  // only times one such draw with glFinish and exits (calibration, no FrameLink).
  const char* iterEnv=getenv("FL_TEST_HANG_ITERATIONS");
  // GLSL ES 3.00 with the bound in a uniform: the compiler cannot see the
  // count (a constant bound was transformed: 2e9 iterations ran in 20 ms).
  GLuint hvs=shader(GL_VERTEX_SHADER,"#version 300 es\nin vec2 position;out vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0.0,1.0);}");
  GLuint hfs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform int count;out vec4 color;void main(){uint x=uint(gl_FragCoord.x)*2654435761u+uint(gl_FragCoord.y);for(int i=0;i<count;i++){x=x*1664525u+1013904223u;}color=vec4(float(x&255u)/255.0,float((x>>8)&255u)/255.0,0.5,1.0);}");
  GLuint hangProgram=glCreateProgram();glAttachShader(hangProgram,hvs);glAttachShader(hangProgram,hfs);glBindAttribLocation(hangProgram,0,"position");glLinkProgram(hangProgram);GLint ok=0;glGetProgramiv(hangProgram,GL_LINK_STATUS,&ok);CHECK(ok);
  GLuint hangTarget=0,hangFbo=0;glGenTextures(1,&hangTarget);glBindTexture(GL_TEXTURE_2D,hangTarget);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  glGenFramebuffers(1,&hangFbo);glBindFramebuffer(GL_FRAMEBUFFER,hangFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hangTarget,0);
  glUseProgram(hangProgram);glViewport(0,0,256,256);
  glUniform1i(glGetUniformLocation(hangProgram,"count"),iterEnv?std::atoi(iterEnv):2000000000);
  if(getenv("FL_TEST_HANG_PROBE")){glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glFinish();
   glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);double t=now();glDrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();
   std::vector<unsigned char> all(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,all.data());
   unsigned written=0;for(size_t i=0;i<all.size();i+=4)written+=all[i+3]!=0;
   std::fprintf(stderr,"probe: %u of 65536 pixels written by the draw\n",written);
   const GLenum rs=(GLenum)(eglGetProcAddress("glGetGraphicsResetStatusEXT")?((GLenum(GL_APIENTRY*)())eglGetProcAddress("glGetGraphicsResetStatusEXT"))():0);
   std::fprintf(stderr,"probe: %s iterations, draw+finish %.1f ms, GL error 0x%x, reset 0x%x\n",iterEnv?iterEnv:"2e9",now()-t,glGetError(),rs);kill(child,SIGKILL);waitpid(child,nullptr,0);return 0;}
  glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE); // not opaque: tilers must not discard it
  glDrawArrays(GL_TRIANGLE_STRIP,0,4); // does not finish
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glUseProgram(program);glViewport(0,0,64,32);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);
  glDrawArrays(GL_TRIANGLE_STRIP,0,4); // the lease's sampling read, behind the hang
  auto hsync=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(hsync!=EGL_NO_SYNC_KHR);glFlush();
  int hfd=exportFence(display,hsync);CHECK(hfd>=0);
  const double start=now();
  CHECK(flCompleteReadLeaseSyncFd(c,lease,hfd)==FL_OK);
  flResult r=FL_BUSY;while(r==FL_BUSY && now()-start<90000){r=flCollectReadLeases(c);if(r==FL_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  sync_file_info info;memset(&info,0,sizeof(info));const int infoOk=ioctl(hfd,SYNC_IOC_FILE_INFO,&info);
  const GLenum reset=resetStatus?resetStatus():0;
  std::fprintf(stderr,"hang: collect=%s after %.0f ms; fence status=%d (ioctl %d); GL reset status=0x%x\n",flResultString(r),now()-start,info.status,infoOk,reset);
  CHECK(r!=FL_BUSY);           // the watchdog ended the hang within 90 s
  // Measured, not assumed (decision 13): does the driver expose the reset on
  // the fence (error status -> FL_DEVICE_LOST), or complete it cleanly while
  // only the GL context reports the reset (FrameLink then sees a normal read)?
  const bool fenceShowsReset=r==FL_DEVICE_LOST;
  std::fprintf(stderr,"outcome: %s\n",fenceShowsReset?"fence completed with an error; FrameLink reported FL_DEVICE_LOST":
      "fence completed WITHOUT an error; FrameLink retired the lease as a normal read; only the GL context reported the reset");
  CHECK(fenceShowsReset || (r==FL_OK && info.status==1 && reset!=0));
  int status=0;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0); // slot handed back
  flFrame next{};const flResult after=flAcquireFrame(c,&next,10);
  CHECK(fenceShowsReset ? after==FL_DEVICE_LOST : after==FL_TIMEOUT || after==FL_DISCONNECTED); // terminal only if seen
  close(hfd);flClose(c);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
  // Recovery: a fresh process runs the normal GPU lease test on the same device.
  pid_t again=0;char* plain[]={argv[0],nullptr};CHECK(posix_spawn(&again,argv[0],nullptr,nullptr,plain,environ)==0);
  CHECK(waitpid(again,&status,0)==again && WIFEXITED(status) && WEXITSTATUS(status)==0);
  puts(fenceShowsReset?"GPU hang in the consumer's read: FL_DEVICE_LOST, slot handed back, terminal; a fresh run afterwards passed":
                       "GPU hang in the consumer's read: NOT visible on the fence (driver); slot handed back; a fresh run afterwards passed");
  return fenceShowsReset?0:2;
 }
 // Sample the imported allocation into a test-only verification surface.
 double busyStart=0;if(busy){busyStart=now();busyWork(busyDraws);}
 glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==GL_NO_ERROR);
 auto sync=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(sync!=EGL_NO_SYNC_KHR);glFlush();
 int fd=exportFence(display,sync);CHECK(fd>=0);
 if(delayed){auto getSync=(PFNEGLGETSYNCATTRIBKHRPROC)eglGetProcAddress("eglGetSyncAttribKHR");CHECK(getSync);EGLint gateState=0,fenceState=0;
  CHECK(getSync(display,gate,EGL_SYNC_STATUS_KHR,&gateState));CHECK(getSync(display,sync,EGL_SYNC_STATUS_KHR,&fenceState));
  pollfd check{fd,POLLIN,0};int ready=poll(&check,1,0);CHECK(ready>=0);
  std::fprintf(stderr,"injection: gate_status=%x fence_status=%x fd_ready=%d revents=%x\n",gateState,fenceState,ready,check.revents);
 }
 CHECK(flCompleteReadLeaseSyncFd(c,lease,fd)==FL_OK);destroySync(display,sync);
 if(busy){
  // While the fence is unready the lease stays BUSY and the producer (waiting
  // to reacquire the leased slot) stays blocked; checked over HOLD_MS.
  unsigned checks=0;while(now()-busyStart<HOLD_MS){pollfd state{fd,POLLIN,0};int ready=poll(&state,1,0);CHECK(ready==0);
   CHECK(flCollectReadLeases(c)==FL_BUSY);int status=0;CHECK(waitpid(child,&status,WNOHANG)==0);++checks;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(checks>=20);
  double t=now();bool done=false;while(now()-t<5000){auto r=flCollectReadLeases(c);pollfd state{fd,POLLIN,0};const int ready=poll(&state,1,0);
   if(r==FL_OK){CHECK(ready==1);done=true;break;} // never retired before its fence
   CHECK(r==FL_BUSY);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(done);std::fprintf(stderr,"busy lease: BUSY for %u checks over %.0f ms; retired %.1f ms after the workload was queued\n",checks,HOLD_MS,now()-busyStart);
 }
 if(delayed){
  for(unsigned i=0;i<20;++i){auto result=flCollectReadLeases(c);if(result!=FL_BUSY){pollfd state{fd,POLLIN,0};int ready=poll(&state,1,0);std::fprintf(stderr,"collect iteration=%u result=%d fence_ready=%d revents=%x\n",i,int(result),ready,state.revents);}CHECK(result==FL_BUSY);int status=0;CHECK(waitpid(child,&status,WNOHANG)==0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(signalSync(display,gate,EGL_SIGNALED_KHR));destroySync(display,gate);
 }
 close(fd);
 bool collected=false;for(int i=0;i<2000;++i){auto r=flCollectReadLeases(c);if(r==FL_OK){collected=true;break;}CHECK(r==FL_BUSY || (crash && r==FL_DISCONNECTED));std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(collected);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);unsigned char pixel[4]{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);CHECK(glGetError()==GL_NO_ERROR);
 CHECK(pixel[0]==17&&pixel[1]==91&&pixel[2]==203&&pixel[3]==255);
 if(busy){glDeleteProgram(busyProgram);glDeleteFramebuffers(1,&busyFbo);glDeleteTextures(1,&busyTarget);}
 glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&output);destroyImage(display,image);
 int status=0;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==(crash?99:0));
 flClose(c);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
 puts(busy?"GPU-busy delayed completion: lease BUSY until the native fence signalled, then retired with exact pixels":"separate-process EGL imported-allocation GPU read, native sync-file lease retirement and pixel verification passed");
}

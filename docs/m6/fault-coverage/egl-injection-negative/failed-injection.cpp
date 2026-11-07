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
#include <unistd.h>
#include <spawn.h>
#include <poll.h>
#include <sys/wait.h>
extern char** environ;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s EGL=%x GL=%x\n",__LINE__,#x,eglGetError(),glGetError());std::exit(1);}}while(0)
static int producer(const char* name,bool crash) {
 flChannel* p=nullptr;CHECK(flOpenProducer(name,&p)==FL_OK);
 flBuffer b{};CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);void* data=nullptr;uint64_t stride=0;
 CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){auto* pixel=(unsigned char*)data+y*stride+x*4;pixel[0]=17;pixel[1]=91;pixel[2]=203;pixel[3]=255;}
 flImageUnmap(b.image);
 flBuffer spare{},returned{};CHECK(flAcquireBuffer(p,&spare,1000)==FL_OK);
 CHECK(flSubmit(p,&b,123456)==FL_OK);
 if(crash)_exit(99);
 CHECK(flAcquireBuffer(p,&returned,5000)==FL_OK); // waits for consumer's GPU lease
 CHECK(flCancelBuffer(p,&spare)==FL_OK && flCancelBuffer(p,&returned)==FL_OK);
 flClose(p);return 0;
}
int main(int argc,char** argv) {
 if(argc==3)return producer(argv[2],!std::strcmp(argv[1],"--producer-crash"));
 bool crash=argc==2 && !std::strcmp(argv[1],"--producer-death");
 bool delayed=argc==2 && !std::strcmp(argv[1],"--delayed-completion");
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
 EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);CHECK(context!=EGL_NO_CONTEXT);
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
  if(gate!=EGL_NO_SYNC_KHR){CHECK(signalSync(display,gate,EGL_SIGNALED_KHR));destroySync(display,gate);gate=EGL_NO_SYNC_KHR;}
  if(!supported){std::printf("SKIP: controllable reusable-sync server wait unavailable EGL=%x\n",why);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return 77;}
 }
 std::string name="egl-lease-"+std::to_string(getpid());flChannel *c=nullptr;
 CHECK(flCreateChannelEx(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_GPU_RENDER|FL_DEFER_RELEASE,&c)==FL_OK);
 pid_t child=0;char* args[]={argv[0],const_cast<char*>(crash?"--producer-crash":"--producer"),const_cast<char*>(name.c_str()),nullptr};
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
 if(delayed){gate=createSync(display,EGL_SYNC_REUSABLE_KHR,nullptr);CHECK(gate!=EGL_NO_SYNC_KHR);CHECK(waitSync(display,gate,0));}
 // Sample the imported allocation into a test-only verification surface.
 glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==GL_NO_ERROR);
 auto sync=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(sync!=EGL_NO_SYNC_KHR);glFlush();
 int fd=exportFence(display,sync);CHECK(fd>=0);
 if(delayed){auto getSync=(PFNEGLGETSYNCATTRIBKHRPROC)eglGetProcAddress("eglGetSyncAttribKHR");CHECK(getSync);EGLint gateState=0,fenceState=0;
  CHECK(getSync(display,gate,EGL_SYNC_STATUS_KHR,&gateState));CHECK(getSync(display,sync,EGL_SYNC_STATUS_KHR,&fenceState));
  pollfd check{fd,POLLIN,0};int ready=poll(&check,1,0);CHECK(ready>=0);
  std::fprintf(stderr,"injection: gate_status=%x fence_status=%x fd_ready=%d revents=%x\n",gateState,fenceState,ready,check.revents);
 }
 CHECK(flCompleteReadLeaseSyncFd(c,lease,fd)==FL_OK);destroySync(display,sync);
 if(delayed){
  for(unsigned i=0;i<20;++i){auto result=flCollectReadLeases(c);if(result!=FL_BUSY){pollfd state{fd,POLLIN,0};int ready=poll(&state,1,0);std::fprintf(stderr,"collect iteration=%u result=%d fence_ready=%d revents=%x\n",i,int(result),ready,state.revents);}CHECK(result==FL_BUSY);int status=0;CHECK(waitpid(child,&status,WNOHANG)==0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(signalSync(display,gate,EGL_SIGNALED_KHR));destroySync(display,gate);
 }
 close(fd);
 bool collected=false;for(int i=0;i<2000;++i){auto r=flCollectReadLeases(c);if(r==FL_OK){collected=true;break;}CHECK(r==FL_BUSY || (crash && r==FL_DISCONNECTED));std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(collected);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);unsigned char pixel[4]{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);CHECK(glGetError()==GL_NO_ERROR);
 CHECK(pixel[0]==17&&pixel[1]==91&&pixel[2]==203&&pixel[3]==255);
 glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&output);destroyImage(display,image);
 int status=0;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==(crash?99:0));
 flClose(c);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
 puts("separate-process EGL imported-allocation GPU read, native sync-file lease retirement and pixel verification passed");
}

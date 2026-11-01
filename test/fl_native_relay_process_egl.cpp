#include "../samples/native_relay.h"
#include "framelink_sync_fd.h"
#ifdef __ANDROID__
#include "framelink_ahb.h"
#include <android/hardware_buffer.h>
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
#include <sys/wait.h>
extern char** environ;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s EGL=%x GL=%x\n",__LINE__,#x,eglGetError(),glGetError());std::exit(1);}}while(0)
#include <sys/socket.h>
#include <poll.h>
#include <dlfcn.h>
#include <vector>
#ifdef __ANDROID__
using Native=AHardwareBuffer*;
using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
static bool same(flImage*a,flImage*b){auto get=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");uint64_t x=0,y=0;return get&&!get(flImageAHardwareBuffer(a),&x)&&!get(flImageAHardwareBuffer(b),&y)&&x==y;}
static void allocate(Native*images){AHardwareBuffer_Desc d{};d.width=64;d.height=32;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;for(unsigned i=0;i<2;i++)CHECK(!AHardwareBuffer_allocate(&d,&images[i]));}
static void cleanup(Native*images){for(unsigned i=0;i<2;i++)AHardwareBuffer_release(images[i]);}
static void sendNative(int fd,Native image){CHECK(!AHardwareBuffer_sendHandleToUnixSocket(image,fd));}
static void receiveNative(int fd,Native&image){CHECK(!AHardwareBuffer_recvHandleFromUnixSocket(fd,&image));}
static flResult channel(const std::string&name,Native*images,flChannel**out){return flCreateChannelFromHardwareBuffers(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,images,out);}
#else
#include <gbm.h>
#include <drm_fourcc.h>
#include <sys/stat.h>
#include <fcntl.h>
using Native=flDmabuf;
static gbm_device* allocator=nullptr;static gbm_bo*objects[2]{};static int drm=-1;
static bool same(flImage*a,flImage*b){flDmabuf x{},y{};if(flImageDmabuf(a,&x)!=FL_OK||flImageDmabuf(b,&y)!=FL_OK||x.planes!=1||y.planes!=1)return false;struct stat sx{},sy{};return !fstat(x.fd[0],&sx)&&!fstat(y.fd[0],&sy)&&sx.st_dev==sy.st_dev&&sx.st_ino==sy.st_ino&&x.offset[0]==y.offset[0]&&x.stride[0]==y.stride[0]&&x.fourcc==y.fourcc&&x.modifier==y.modifier;}
static void allocate(Native*images){for(int n=128;n<136&&!allocator;n++){std::string path="/dev/dri/renderD"+std::to_string(n);drm=open(path.c_str(),O_RDWR|O_CLOEXEC);if(drm>=0){allocator=gbm_create_device(drm);if(!allocator)close(drm);}}CHECK(allocator);
 for(unsigned i=0;i<2;i++){objects[i]=gbm_bo_create(allocator,64,32,DRM_FORMAT_ABGR8888,GBM_BO_USE_LINEAR);CHECK(objects[i]);auto&d=images[i];d.planes=gbm_bo_get_plane_count(objects[i]);CHECK(d.planes==1);d.fourcc=gbm_bo_get_format(objects[i]);d.modifier=gbm_bo_get_modifier(objects[i]);CHECK(d.modifier==DRM_FORMAT_MOD_LINEAR);d.fd[0]=gbm_bo_get_fd(objects[i]);CHECK(d.fd[0]>=0);d.offset[0]=gbm_bo_get_offset(objects[i],0);d.stride[0]=gbm_bo_get_stride(objects[i]);}}
static void cleanup(Native*images){for(unsigned i=0;i<2;i++){close(images[i].fd[0]);if(objects[i])gbm_bo_destroy(objects[i]);}if(allocator){gbm_device_destroy(allocator);close(drm);}}
static void sendNative(int fd,Native image){char control[CMSG_SPACE(sizeof(int))]{};iovec iov{&image,sizeof(image)};msghdr m{};m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);auto*c=CMSG_FIRSTHDR(&m);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),image.fd,sizeof(int));CHECK(sendmsg(fd,&m,MSG_NOSIGNAL)==sizeof(image));}
static void receiveNative(int fd,Native&image){char control[CMSG_SPACE(sizeof(int))]{};iovec iov{&image,sizeof(image)};msghdr m{};m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);CHECK(recvmsg(fd,&m,MSG_WAITALL|MSG_CMSG_CLOEXEC)==sizeof(image)&&!(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC)));auto*c=CMSG_FIRSTHDR(&m);CHECK(c&&c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len==CMSG_LEN(sizeof(int))&&image.planes==1);memcpy(image.fd,CMSG_DATA(c),sizeof(int));}
static flResult channel(const std::string&name,Native*images,flChannel**out){return flCreateChannelFromDmabufs(name.c_str(),64,32,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,images,out);}
#endif
static void sendByte(int fd,char v){CHECK(send(fd,&v,1,MSG_NOSIGNAL)==1);}
static void receive(int fd,char expected){pollfd p{fd,POLLIN,0};CHECK(poll(&p,1,5000)==1);char v=0;CHECK(read(fd,&v,1)==1&&v==expected);}
static pid_t spawn(const char*exe,std::vector<std::string> args,std::vector<int> fds){
 posix_spawn_file_actions_t actions;CHECK(!posix_spawn_file_actions_init(&actions));
 for(unsigned i=0;i<fds.size();i++)CHECK(!posix_spawn_file_actions_adddup2(&actions,fds[i],100+i));
 std::vector<char*> argv{const_cast<char*>(exe)};for(auto&arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
 pid_t child=0;CHECK(!posix_spawn(&child,exe,&actions,nullptr,argv.data(),environ));posix_spawn_file_actions_destroy(&actions);return child;
}
static void join(pid_t child){int status=0;for(unsigned i=0;i<5000;i++){auto r=waitpid(child,&status,WNOHANG);CHECK(r>=0);if(r==child){CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);return;}usleep(1000);}CHECK(false);}
static int produce(const std::string&name,int control){
 alarm(20);flChannel*p=nullptr;CHECK(flOpenProducer((name+".up").c_str(),&p)==FL_OK);sendByte(control,'S');
 flBuffer buffers[2]{};for(auto&b:buffers)CHECK(flAcquireBuffer(p,&b,1000)==FL_OK);
 for(unsigned n=0;n<16;n++){auto&b=buffers[n%2];void*data=nullptr;uint64_t stride=0;CHECK(flImageMap(b.image,&data,&stride)==FL_OK);
  for(unsigned y=0;y<32;y++)for(unsigned x=0;x<64;x++){auto*v=(unsigned char*)data+y*stride+x*4;v[0]=17+n;v[1]=91+n;v[2]=203-n;v[3]=255;}flImageUnmap(b.image);
  flFrameMetadata m{1,FL_FRAME_HAS_DTS|FL_FRAME_HAS_DURATION,100+int64_t(n),10000000+n,77,{}};CHECK(flSetFrameMetadata(p,&b,&m)==FL_OK);CHECK(flSubmit(p,&b,1000+n)==FL_OK);
  receive(control,'H');flBuffer premature{};CHECK(flAcquireBuffer(p,&premature,0)==FL_TIMEOUT);sendByte(control,'C');
  flBuffer returned{};CHECK(flAcquireBuffer(p,&returned,5000)==FL_OK&&same(returned.image,b.image));b=returned;
 }
 for(auto&b:buffers)CHECK(flCancelBuffer(p,&b)==FL_OK);sendByte(control,'D');flClose(p);close(control);return 0;
}
static int relay(const char*exe,const std::string&name,int control,int producerControl){
 alarm(20);Native original[2]{};for(auto&b:original)receiveNative(control,b);
 flChannel*up=nullptr,*down=nullptr;CHECK(channel(name+".up",original,&up)==FL_OK);
 CHECK(flOpenProducer((name+".down").c_str(),&down)==FL_OK);auto child=spawn(exe,{"producer",name,"100"},{producerControl});close(producerControl);receive(control,'G');
 flsample::NativeRelay r(up,down,same);CHECK(r.initialize()==FL_OK);unsigned frames=0;
 while(frames<16){CHECK(r.collect()==FL_OK);flFrame f{};auto result=flAcquireFrame(up,&f,1);if(result==FL_TIMEOUT)continue;CHECK(result==FL_OK&&f.ptsNs==1000+frames);CHECK(r.forward(f)==FL_OK);++frames;}
 flResult done=FL_BUSY;while(done==FL_BUSY){done=r.finish();if(done==FL_BUSY)usleep(1000);}CHECK(done==FL_OK);receive(control,'D');join(child);
 flClose(down);flClose(up);cleanup(original);close(control);return 0;
}
int main(int argc,char**argv){
 if(argc==4&&!strcmp(argv[1],"producer"))return produce(argv[2],atoi(argv[3]));
 if(argc==5&&!strcmp(argv[1],"relay"))return relay(argv[0],argv[2],atoi(argv[3]),atoi(argv[4]));
 CHECK(argc==1);alarm(25);
#ifdef __ANDROID__
 CHECK(dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId"));
#endif
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
 std::string name="ahb-relay-process-"+std::to_string(getpid());
 int relayPair[2],producerPair[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,relayPair));CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,producerPair));
 timeval timeout{5,0};for(int fd:{relayPair[0],relayPair[1],producerPair[0],producerPair[1]}){CHECK(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));CHECK(!setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)));}
 Native original[2]{};allocate(original);Native reversed[2]={original[1],original[0]};flChannel*c=nullptr;
 CHECK(channel(name+".down",reversed,&c)==FL_OK);
 auto child=spawn(argv[0],{"relay",name,"100","101"},{relayPair[1],producerPair[1]});close(relayPair[1]);close(producerPair[1]);for(auto b:original)sendNative(relayPair[0],b);
 receive(producerPair[0],'S');sendByte(relayPair[0],'G');
 for(unsigned n=0;n<16;n++){
 flFrame frame{};CHECK(flAcquireFrame(c,&frame,5000)==FL_OK&&frame.ptsNs==1000+n);flFrameMetadata metadata{};
 CHECK(flGetFrameMetadata(c,&frame,&metadata)==FL_OK&&metadata.dtsNs==100+n&&metadata.durationNs==10000000+n&&metadata.streamGeneration==77);
 uint64_t lease=0;CHECK(flLeaseFrame(c,&frame,&lease)==FL_OK);sendByte(producerPair[0],'H');receive(producerPair[0],'C');
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
 // Sample the imported allocation into a test-only verification surface.
 glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==GL_NO_ERROR);
 auto sync=createSync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(sync!=EGL_NO_SYNC_KHR);glFlush();
 int fd=exportFence(display,sync);CHECK(fd>=0);
 CHECK(flCompleteReadLeaseSyncFd(c,lease,fd)==FL_OK);close(fd);destroySync(display,sync);
 bool collected=false;for(int i=0;i<2000;++i){auto r=flCollectReadLeases(c);if(r==FL_OK){collected=true;break;}CHECK(r==FL_BUSY);std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(collected);
 unsigned char pixels[64*32*4]{};glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK(glGetError()==GL_NO_ERROR);
 for(unsigned i=0;i<64*32;i++){auto*p=pixels+i*4;CHECK(p[0]==17+n&&p[1]==91+n&&p[2]==203-n&&p[3]==255);}
 glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&output);destroyImage(display,image);
 }
 receive(producerPair[0],'D');sendByte(relayPair[0],'D');join(child);close(producerPair[0]);close(relayPair[0]);
 flClose(c);cleanup(original);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
 puts("three-process native EGL relay16/16: original identity, reversed slots, exact pixels/PTS/DTS/duration, no upstream reuse while downstream held, actual EGL sampling and sync-fd retirement passed");
}

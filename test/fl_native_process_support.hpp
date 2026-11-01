#pragma once
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
static unsigned imageWidth=64,imageHeight=32;
#ifdef __ANDROID__
using Native=AHardwareBuffer*;
using GetId=int(*)(const AHardwareBuffer*,uint64_t*);
static bool same(flImage*a,flImage*b){auto get=(GetId)dlsym(RTLD_DEFAULT,"AHardwareBuffer_getId");uint64_t x=0,y=0;return get&&!get(flImageAHardwareBuffer(a),&x)&&!get(flImageAHardwareBuffer(b),&y)&&x==y;}
static void allocate(Native*images){AHardwareBuffer_Desc d{};d.width=imageWidth;d.height=imageHeight;d.layers=1;d.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;d.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;for(unsigned i=0;i<2;i++)CHECK(!AHardwareBuffer_allocate(&d,&images[i]));}
static void cleanup(Native*images){for(unsigned i=0;i<2;i++)AHardwareBuffer_release(images[i]);}
static void sendNative(int fd,Native image){CHECK(!AHardwareBuffer_sendHandleToUnixSocket(image,fd));}
static void receiveNative(int fd,Native&image){CHECK(!AHardwareBuffer_recvHandleFromUnixSocket(fd,&image));}
static flResult channel(const std::string&name,Native*images,flChannel**out){return flCreateChannelFromHardwareBuffers(name.c_str(),imageWidth,imageHeight,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,images,out);}
#else
#include <gbm.h>
#include <drm_fourcc.h>
#include <sys/stat.h>
#include <fcntl.h>
using Native=flDmabuf;
static gbm_device* allocator=nullptr;static gbm_bo*objects[2]{};static int drm=-1;
static bool same(flImage*a,flImage*b){flDmabuf x{},y{};if(flImageDmabuf(a,&x)!=FL_OK||flImageDmabuf(b,&y)!=FL_OK||x.planes!=1||y.planes!=1)return false;struct stat sx{},sy{};return !fstat(x.fd[0],&sx)&&!fstat(y.fd[0],&sy)&&sx.st_dev==sy.st_dev&&sx.st_ino==sy.st_ino&&x.offset[0]==y.offset[0]&&x.stride[0]==y.stride[0]&&x.fourcc==y.fourcc&&x.modifier==y.modifier;}
static void allocate(Native*images){for(int n=128;n<136&&!allocator;n++){std::string path="/dev/dri/renderD"+std::to_string(n);drm=open(path.c_str(),O_RDWR|O_CLOEXEC);if(drm>=0){allocator=gbm_create_device(drm);if(!allocator)close(drm);}}CHECK(allocator);
 for(unsigned i=0;i<2;i++){objects[i]=gbm_bo_create(allocator,imageWidth,imageHeight,DRM_FORMAT_ABGR8888,GBM_BO_USE_LINEAR);CHECK(objects[i]);auto&d=images[i];d.planes=gbm_bo_get_plane_count(objects[i]);CHECK(d.planes==1);d.fourcc=gbm_bo_get_format(objects[i]);d.modifier=gbm_bo_get_modifier(objects[i]);CHECK(d.modifier==DRM_FORMAT_MOD_LINEAR);d.fd[0]=gbm_bo_get_fd(objects[i]);CHECK(d.fd[0]>=0);d.offset[0]=gbm_bo_get_offset(objects[i],0);d.stride[0]=gbm_bo_get_stride(objects[i]);}}
static void cleanup(Native*images){for(unsigned i=0;i<2;i++){close(images[i].fd[0]);if(objects[i])gbm_bo_destroy(objects[i]);}if(allocator){gbm_device_destroy(allocator);close(drm);}}
static void sendNative(int fd,Native image){char control[CMSG_SPACE(sizeof(int))]{};iovec iov{&image,sizeof(image)};msghdr m{};m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);auto*c=CMSG_FIRSTHDR(&m);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),image.fd,sizeof(int));CHECK(sendmsg(fd,&m,MSG_NOSIGNAL)==sizeof(image));}
static void receiveNative(int fd,Native&image){char control[CMSG_SPACE(sizeof(int))]{};iovec iov{&image,sizeof(image)};msghdr m{};m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);CHECK(recvmsg(fd,&m,MSG_WAITALL|MSG_CMSG_CLOEXEC)==sizeof(image)&&!(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC)));auto*c=CMSG_FIRSTHDR(&m);CHECK(c&&c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len==CMSG_LEN(sizeof(int))&&image.planes==1);memcpy(image.fd,CMSG_DATA(c),sizeof(int));}
static flResult channel(const std::string&name,Native*images,flChannel**out){return flCreateChannelFromDmabufs(name.c_str(),imageWidth,imageHeight,FL_FORMAT_RGBA8,2,FL_MAP_CPU|FL_DEFER_RELEASE|FL_REQUIRE_FRAME_METADATA,images,out);}
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

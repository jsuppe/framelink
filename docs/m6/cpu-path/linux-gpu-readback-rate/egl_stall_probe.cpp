// Driver-only probe: no FrameLink. A linear GBM buffer (as a FrameLink ring
// slot) imported through EGL_EXT_image_dma_buf_import, drawn into a private
// RGBA texture and read back with glReadPixels + glFinish, the same calls as
// gst/fl_gst_egl_readback.hpp. Paced at FPS; prints every read over a threshold
// with its time since the first read.
//   egl_stall_probe WIDTH HEIGHT FPS SECONDS [THRESHOLD_MS] [--fresh-image]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static double now(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static GLuint shader(GLenum k,const char*s){GLuint id=glCreateShader(k);glShaderSource(id,1,&s,nullptr);glCompileShader(id);GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);CHECK(ok);return id;}
int main(int argc,char**argv){
 CHECK(argc>=5);const unsigned w=atoi(argv[1]),h=atoi(argv[2]),fps=atoi(argv[3]),seconds=atoi(argv[4]);const double threshold=argc>5?atof(argv[5]):0;
 bool fresh=false,write=false,toggle=false,two=false,mm=false;for(int i=5;i<argc;i++){if(!strcmp(argv[i],"--fresh-image"))fresh=true;if(!strcmp(argv[i],"--write"))write=true;if(!strcmp(argv[i],"--toggle"))toggle=true;if(!strcmp(argv[i],"--two-outputs"))two=true;if(!strcmp(argv[i],"--mmap-outputs"))mm=true;}
 // Producer side: a linear buffer on its own GBM device, two slots like the ring.
 int pfd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(pfd>=0);auto*pg=gbm_create_device(pfd);CHECK(pg);
 gbm_bo*bo[2];int fd[2];unsigned stride[2];
 for(int i=0;i<2;i++){bo[i]=gbm_bo_create(pg,w,h,GBM_FORMAT_ARGB8888,GBM_BO_USE_LINEAR);CHECK(bo[i]);fd[i]=gbm_bo_get_fd(bo[i]);CHECK(fd[i]>=0);stride[i]=gbm_bo_get_stride(bo[i]);}
 // Reader side: a private GBM display, as the adapter.
 int rfd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(rfd>=0);auto*rg=gbm_create_device(rfd);CHECK(rg);
 auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");CHECK(platform);
 EGLDisplay d=platform(EGL_PLATFORM_GBM_KHR,rg,nullptr);CHECK(d!=EGL_NO_DISPLAY&&eglInitialize(d,nullptr,nullptr)&&eglBindAPI(EGL_OPENGL_ES_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 EGLConfig cfg;EGLint n=0;CHECK(eglChooseConfig(d,attrs,&cfg,1,&n)&&n==1);
 EGLint sz[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE},ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
 auto surf=eglCreatePbufferSurface(d,cfg,sz);auto ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);CHECK(eglMakeCurrent(d,surf,surf,ctx));
 auto create=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");auto destroy=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
 auto bind=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
 auto vs=shader(GL_VERTEX_SHADER,"attribute vec2 position;varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0.,1.);}");
 auto fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision highp float;varying vec2 uv;uniform samplerExternalOES source;void main(){gl_FragColor=texture2D(source,uv).bgra;}");
 GLuint prog=glCreateProgram();glAttachShader(prog,vs);glAttachShader(prog,fs);glBindAttribLocation(prog,0,"position");glLinkProgram(prog);
 GLuint out,fbo;glGenTextures(1,&out);glGenFramebuffers(1,&fbo);glBindTexture(GL_TEXTURE_2D,out);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
 glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 std::vector<unsigned char> cpu(size_t(w)*h*4),cpu2(two&&!mm?size_t(w)*h*4:0);
 unsigned char* outs[2]={cpu.data(),two?cpu2.data():cpu.data()};
 if(mm){for(int k=0;k<2;k++){void*m=mmap(nullptr,size_t(w)*h*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(m!=MAP_FAILED);outs[k]=(unsigned char*)m;}if(!two)outs[1]=outs[0];} // untouched pages, as FrameLink's page allocator
 auto import=[&](int i){EGLint a[]={EGL_WIDTH,(EGLint)w,EGL_HEIGHT,(EGLint)h,EGL_LINUX_DRM_FOURCC_EXT,DRM_FORMAT_ARGB8888,EGL_DMA_BUF_PLANE0_FD_EXT,fd[i],EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)stride[i],EGL_NONE};
  auto img=create(d,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,a);CHECK(img!=EGL_NO_IMAGE_KHR);return img;};
 const unsigned frames=fps*seconds;std::vector<double> dur;double first=0;const double t0=now();
 for(unsigned f=0;f<frames;f++){
  const double due=t0+double(f)/fps;while(now()<due)usleep(200);
  const int i=f&1;
  if(write){ // producer: the bench's CPU write of the first rows through the DMA-BUF
   dma_buf_sync sy{};sy.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd[i],DMA_BUF_IOCTL_SYNC,&sy)==0);
   const size_t bytes=size_t(stride[i])*4;void*m=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd[i],0);CHECK(m!=MAP_FAILED);
   memset(m,int(f),bytes);CHECK(munmap(m,bytes)==0);sy.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CHECK(ioctl(fd[i],DMA_BUF_IOCTL_SYNC,&sy)==0);}
  const double s=now();if(!f)first=s;
  if(toggle){CHECK(eglBindAPI(EGL_OPENGL_ES_API)&&eglMakeCurrent(d,surf,surf,ctx));glBindTexture(GL_TEXTURE_2D,out);glBindFramebuffer(GL_FRAMEBUFFER,fbo);}
  auto img=import(i); // per frame, as the adapter does
  GLuint tex;glGenTextures(1,&tex);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,tex);bind(GL_TEXTURE_EXTERNAL_OES,img);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glUseProgram(prog);glUniform1i(glGetUniformLocation(prog,"source"),0);glViewport(0,0,w,h);
  GLfloat v[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,v);
  glDrawArrays(GL_TRIANGLE_STRIP,0,4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,outs[f&1]);glFinish();
  glDeleteTextures(1,&tex);destroy(d,img);
  if(toggle)eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT); // as the adapter's Restore
  const double e=(now()-s)*1e3;dur.push_back(e);
  if(threshold>0&&e>threshold)printf("slow t=%.3f s frame=%u %.2f ms\n",s-first,f,e);
 }
 auto sorted=dur;std::sort(sorted.begin(),sorted.end());
 printf("frames=%u p50=%.2f p99=%.2f max=%.2f ms (fresh=%d)\n",frames,sorted[sorted.size()/2],sorted[size_t(sorted.size()*0.99)],sorted.back(),fresh);
 return 0;
}

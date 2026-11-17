#pragma once
#include <vector>
// Verification output is private; imported source allocations are sampled only.
class BenchmarkGpu {
 EGLDisplay d=EGL_NO_DISPLAY;EGLContext c=EGL_NO_CONTEXT;EGLSurface s=EGL_NO_SURFACE;
 GLuint output=0,fbo=0,program=0,vs=0,fs=0;
 struct Image{flImage*source=nullptr;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint texture=0;};Image images[2];
 PFNEGLCREATEIMAGEKHRPROC create=nullptr;PFNEGLDESTROYIMAGEKHRPROC destroy=nullptr;
 PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind=nullptr;
 PFNEGLCREATESYNCKHRPROC syncCreate=nullptr;PFNEGLDESTROYSYNCKHRPROC syncDestroy=nullptr;PFNEGLDUPNATIVEFENCEFDANDROIDPROC exportFd=nullptr;
 static GLuint shader(GLenum type,const char*text){auto id=glCreateShader(type);glShaderSource(id,1,&text,nullptr);glCompileShader(id);GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);CHECK(ok);return id;}
public:
 void start(){
#ifdef __ANDROID__
 d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
 auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");CHECK(platform);d=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
#endif
 CHECK(d!=EGL_NO_DISPLAY&&eglInitialize(d,nullptr,nullptr));const char*ext=eglQueryString(d,EGL_EXTENSIONS);CHECK(ext&&strstr(ext,"EGL_ANDROID_native_fence_sync"));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLConfig cfg{};EGLint count=0;CHECK(eglChooseConfig(d,attrs,&cfg,1,&count)&&count);
 EGLint size[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};s=eglCreatePbufferSurface(d,cfg,size);c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctx);CHECK(s!=EGL_NO_SURFACE&&c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,s,s,c));
 create=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");destroy=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");bind=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");syncCreate=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");syncDestroy=(PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");exportFd=(PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");CHECK(create&&destroy&&bind&&syncCreate&&syncDestroy&&exportFd);
 vs=shader(GL_VERTEX_SHADER,"attribute vec2 position;varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0.,1.);}");fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision highp float;varying vec2 uv;uniform samplerExternalOES source;void main(){gl_FragColor=texture2D(source,uv);}");program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glBindAttribLocation(program,0,"position");glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);
 glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,imageWidth,imageHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 }
 // Prime shader/driver allocation before producer pacing; no media/control handshake.
 void warm(Native native){
#ifdef __ANDROID__
 auto get=(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");CHECK(get);EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};auto image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,get(native),attrs);
#else
 const auto&dma=native;EGLint attrs[]={EGL_WIDTH,(EGLint)imageWidth,EGL_HEIGHT,(EGLint)imageHeight,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)dma.fourcc,EGL_DMA_BUF_PLANE0_FD_EXT,dma.fd[0],EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)dma.offset[0],EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)dma.stride[0],EGL_NONE};auto image=create(d,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
#endif
 CHECK(image!=EGL_NO_IMAGE_KHR);GLuint texture=0;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);bind(GL_TEXTURE_EXTERNAL_OES,image);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glUseProgram(program);glUniform1i(glGetUniformLocation(program,"source"),0);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,imageWidth,imageHeight);GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glDrawArrays(GL_TRIANGLE_STRIP,0,4);unsigned char pixel[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glFinish();CHECK(glGetError()==GL_NO_ERROR);glDeleteTextures(1,&texture);destroy(d,image);
 }
 uint64_t read(flChannel*channel,const flFrame&frame,uint64_t lease,void*header,size_t bytes,unsigned n){
 CHECK(bytes==32);Image*cached=nullptr;for(auto&v:images)if(v.source==frame.image)cached=&v;
 if(!cached){for(auto&v:images)if(!v.source){cached=&v;break;}CHECK(cached);cached->source=frame.image;
#ifdef __ANDROID__
 auto native=(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");CHECK(native);EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};cached->image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native(flImageAHardwareBuffer(frame.image)),attrs);
#else
 flDmabuf dma{};CHECK(flImageDmabuf(frame.image,&dma)==FL_OK&&dma.planes==1);EGLint attrs[]={EGL_WIDTH,(EGLint)imageWidth,EGL_HEIGHT,(EGLint)imageHeight,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)dma.fourcc,EGL_DMA_BUF_PLANE0_FD_EXT,dma.fd[0],EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)dma.offset[0],EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)dma.stride[0],EGL_NONE};cached->image=create(d,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
#endif
 CHECK(cached->image!=EGL_NO_IMAGE_KHR);glGenTextures(1,&cached->texture);glBindTexture(GL_TEXTURE_EXTERNAL_OES,cached->texture);bind(GL_TEXTURE_EXTERNAL_OES,cached->image);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 }
 glUseProgram(program);glUniform1i(glGetUniformLocation(program,"source"),0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,cached->texture);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,imageWidth,imageHeight);GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glDrawArrays(GL_TRIANGLE_STRIP,0,4);CHECK(glGetError()==GL_NO_ERROR);
 auto sync=syncCreate(d,EGL_SYNC_NATIVE_FENCE_ANDROID,nullptr);CHECK(sync!=EGL_NO_SYNC_KHR);glFlush();int fd=exportFd(d,sync);CHECK(fd>=0);CHECK(flCompleteReadLeaseSyncFd(channel,lease,fd)==FL_OK);close(fd);syncDestroy(d,sync);
 bool retired=false;for(unsigned i=0;i<2000;i++){auto r=flCollectReadLeases(channel);if(r==FL_OK){retired=true;break;}CHECK(r==FL_BUSY);usleep(1000);}CHECK(retired);const auto retiredAt=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
 // Read only private verification output, never map/read back the shared source.
 glReadPixels(0,0,8,1,GL_RGBA,GL_UNSIGNED_BYTE,header);unsigned char pixel[4]{};glReadPixels(imageWidth-1,imageHeight-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);CHECK(glGetError()==GL_NO_ERROR);for(auto v:pixel)CHECK(v==(unsigned char)n);return retiredAt;
 }
 // Opt-in full-frame check for functional regressions, never timed benchmark work.
 void verifyUniform(unsigned char expected){
  std::vector<unsigned char> pixels(size_t(imageWidth)*imageHeight*4);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glReadPixels(0,0,imageWidth,imageHeight,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  CHECK(glGetError()==GL_NO_ERROR);
  for(size_t i=0;i<pixels.size();++i)if(pixels[i]!=expected){
   fprintf(stderr,"full-frame mismatch offset=%zu expected=%u actual=%u\n",i,unsigned(expected),unsigned(pixels[i]));CHECK(false);
  }
 }
 ~BenchmarkGpu(){if(d==EGL_NO_DISPLAY)return;for(auto&v:images)if(v.source){glDeleteTextures(1,&v.texture);destroy(d,v.image);}glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&output);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglDestroySurface(d,s);eglTerminate(d);}
};

#pragma once
#include "../../gst/framelink_native.h"
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <atomic>
#include <stdexcept>
#include <vector>
// Foreground qualification renderer. Input GstMemory remains pinned until GPU
// completion. Pixel-oracle readback is deliberate, outside the FrameLink boundary.
class NativeRenderer {
 EGLDisplay display=EGL_NO_DISPLAY;EGLSurface surface=EGL_NO_SURFACE;EGLContext context=EGL_NO_CONTEXT;
 GLuint program=0,vs=0,fs=0;bool poisoned=false;
 inline static std::atomic<bool> devicePoisoned{false};
 static void require(bool value,const char*why){if(!value)throw std::runtime_error(why);}
 static GLuint shader(GLenum type,const char*source){auto s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);require(ok,"native shader compile");return s;}
public:
 ~NativeRenderer(){if(poisoned)return; // failed GPU: quarantine context + sample until process restart
  if(display!=EGL_NO_DISPLAY){if(context!=EGL_NO_CONTEXT){glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);}if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);eglTerminate(display);}}
 void prepare(ANativeWindow*window){require(!devicePoisoned,"native GPU requires process restart after timeout");display=eglGetDisplay(EGL_DEFAULT_DISPLAY);require(display!=EGL_NO_DISPLAY&&eglInitialize(display,nullptr,nullptr),"native EGL initialize");
  EGLint attrs[]={EGL_SURFACE_TYPE,window?EGL_WINDOW_BIT:EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLConfig cfg{};EGLint count=0;require(eglChooseConfig(display,attrs,&cfg,1,&count)&&count,"native EGL config");
  EGLint size[]={EGL_WIDTH,160,EGL_HEIGHT,90,EGL_NONE};surface=window?eglCreateWindowSurface(display,cfg,window,nullptr):eglCreatePbufferSurface(display,cfg,size);require(surface!=EGL_NO_SURFACE,"native EGL surface");EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};context=eglCreateContext(display,cfg,EGL_NO_CONTEXT,contextAttrs);require(context!=EGL_NO_CONTEXT&&eglMakeCurrent(display,surface,surface,context),"native EGL context");
  vs=shader(GL_VERTEX_SHADER,"attribute vec2 position;varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0.,1.);}");
  fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision mediump float;varying vec2 uv;uniform samplerExternalOES source;void main(){gl_FragColor=texture2D(source,uv);}");
  program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glBindAttribLocation(program,0,"position");glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);require(linked,"native program link");
 }
 void render(GstSample*sample,unsigned n){
  auto*buffer=gst_sample_get_buffer(sample);require(gst_buffer_n_memory(buffer)==1,"native sample allocation count");auto*ahb=gst_framelink_memory_get_hardware_buffer(gst_buffer_peek_memory(buffer,0));require(ahb,"native sample missing AHB");
  auto native=(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");auto create=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");auto destroy=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");auto bind=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");auto syncCreate=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");auto syncWait=(PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");auto syncDestroy=(PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");require(native&&create&&destroy&&bind&&syncCreate&&syncWait&&syncDestroy,"native EGL functions");
  EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};auto image=create(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native(ahb),attrs);require(image!=EGL_NO_IMAGE_KHR,"native AHB import");GLuint texture=0;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);bind(GL_TEXTURE_EXTERNAL_OES,image);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glUseProgram(program);glUniform1i(glGetUniformLocation(program,"source"),0);GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glViewport(0,0,160,90);
  // Keep an extra ref across every GPU command. Failed completion quarantines it;
  // dropping only the enclosing sample must never release an in-flight reader.
  auto*pin=gst_buffer_ref(buffer);glDrawArrays(GL_TRIANGLE_STRIP,0,4);auto sync=syncCreate(display,EGL_SYNC_FENCE_KHR,nullptr);
  if(glGetError()!=GL_NO_ERROR||sync==EGL_NO_SYNC_KHR||syncWait(display,sync,EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,2000000000ULL)!=EGL_CONDITION_SATISFIED_KHR){poisoned=true;devicePoisoned=true;throw std::runtime_error("native GPU completion failed; allocation quarantined until restart");}
  syncDestroy(display,sync);gst_buffer_unref(pin);
  std::vector<unsigned char> pixels(160*90*4);glReadPixels(0,0,160,90,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());bool valid=glGetError()==GL_NO_ERROR;
  for(unsigned y=0;y<90&&valid;y++)for(unsigned x=0;x<160;x++){auto*p=pixels.data()+(y*160+x)*4;if(p[0]!=uint8_t(n)||p[1]!=x||p[2]!=y||p[3]!=255){valid=false;break;}}
  glDeleteTextures(1,&texture);destroy(display,image);require(valid,"native GPU pixel mismatch");require(eglSwapBuffers(display,surface),"native surface swap");
 }
};

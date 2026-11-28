#pragma once
// Explicit Linux DMA-BUF -> private GPU surface -> packed CPU readback.
// The caller owns a ready frame until read() returns, including failure. No
// source allocation is retained. Instances are serialized, not thread-affine.
#include "framelink_dmabuf.h"
#include "framelink_memory_policy.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <pthread.h>

class FrameLinkEglReadback {
    int fd_=-1;
    gbm_device* gbm_=nullptr;
    EGLDisplay display_=EGL_NO_DISPLAY;
    EGLContext context_=EGL_NO_CONTEXT;
    EGLSurface surface_=EGL_NO_SURFACE;
    GLuint program_=0, output_=0, fbo_=0;
    uint32_t width_=0,height_=0,major_=0,minor_=0;
    uint64_t gpuCopyBytes_=0,readbackBytes_=0;
    PFNEGLCREATEIMAGEKHRPROC create_=nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_=nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind_=nullptr;
    struct Restore {
        EGLDisplay display=eglGetCurrentDisplay();
        EGLContext context=eglGetCurrentContext();
        EGLSurface draw=eglGetCurrentSurface(EGL_DRAW),read=eglGetCurrentSurface(EGL_READ);
        EGLenum api=eglQueryAPI();
        ~Restore(){
            auto current=eglGetCurrentDisplay();
            if(current!=EGL_NO_DISPLAY)eglMakeCurrent(current,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            eglBindAPI(api);
            if(display!=EGL_NO_DISPLAY)eglMakeCurrent(display,draw,read,context);
        }
    };
    static GLuint shader(GLenum kind,const char* text){
        GLuint id=glCreateShader(kind);glShaderSource(id,1,&text,nullptr);glCompileShader(id);
        GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
        if(!ok){glDeleteShader(id);return 0;}return id;
    }
    bool initialize(uint32_t maj,uint32_t min){
        // A private GBM device gives this adapter a private platform display;
        // terminating it must not tear down an application's surfaceless EGL.
        char path[64];snprintf(path,sizeof(path),"/dev/dri/renderD%u",min);
        fd_=open(path,O_RDWR|O_CLOEXEC);struct stat st{};
        if(fd_<0||fstat(fd_,&st)||!S_ISCHR(st.st_mode)||major(st.st_rdev)!=maj||minor(st.st_rdev)!=min)return false;
        gbm_=gbm_create_device(fd_);if(!gbm_)return false;
        auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        if(!platform)return false;
        display_=platform(EGL_PLATFORM_GBM_KHR,gbm_,nullptr);
        if(display_==EGL_NO_DISPLAY||!eglInitialize(display_,nullptr,nullptr)||!eglBindAPI(EGL_OPENGL_ES_API))return false;
        const char* extensions=eglQueryString(display_,EGL_EXTENSIONS);
        if(!extensions||!strstr(extensions,"EGL_EXT_image_dma_buf_import"))return false;
        EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config{};EGLint count=0;
        if(!eglChooseConfig(display_,attrs,&config,1,&count)||count!=1)return false;
        EGLint size[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        surface_=eglCreatePbufferSurface(display_,config,size);
        context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,ctx);
        if(surface_==EGL_NO_SURFACE||context_==EGL_NO_CONTEXT||!eglMakeCurrent(display_,surface_,surface_,context_))return false;
        create_=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        destroy_=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        bind_=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if(!create_||!destroy_||!bind_)return false;
        auto vs=shader(GL_VERTEX_SHADER,"attribute vec2 position;varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0.,1.);}");
        auto fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision highp float;varying vec2 uv;uniform samplerExternalOES source;uniform bool swapRB;void main(){vec4 c=texture2D(source,uv);gl_FragColor=swapRB?c.bgra:c;}");
        if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
        program_=glCreateProgram();glAttachShader(program_,vs);glAttachShader(program_,fs);glBindAttribLocation(program_,0,"position");glLinkProgram(program_);
        glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);if(!ok)return false;
        glGenTextures(1,&output_);glGenFramebuffers(1,&fbo_);glDisable(GL_DITHER);
        major_=maj;minor_=min;return glGetError()==GL_NO_ERROR;
    }
    // With the context current: open the display on this node if needed and
    // size the output surface.
    bool bind(uint32_t maj,uint32_t min,uint32_t width,uint32_t height){
        if(display_!=EGL_NO_DISPLAY&&(maj!=major_||min!=minor_))reset();
        if(display_==EGL_NO_DISPLAY&&!initialize(maj,min)){reset();return false;}
        if(!eglBindAPI(EGL_OPENGL_ES_API)||!eglMakeCurrent(display_,surface_,surface_,context_))return false;
        glBindTexture(GL_TEXTURE_2D,output_);
        if(width_!=width||height_!=height){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            if(glGetError()!=GL_NO_ERROR)return false;
            width_=width;height_=height;
        }
        return true;
    }
    static void nodeOf(const uint8_t id[8],uint32_t& maj,uint32_t& min){
        maj=min=0;for(unsigned i=0;i<4;i++){maj|=uint32_t(id[i])<<(8*i);min|=uint32_t(id[4+i])<<(8*i);}
    }
public:
    FrameLinkEglReadback()=default;
    FrameLinkEglReadback(const FrameLinkEglReadback&)=delete;
    FrameLinkEglReadback& operator=(const FrameLinkEglReadback&)=delete;
    ~FrameLinkEglReadback(){reset();}
    void reset(){
        {Restore restore;
            if(restore.context==context_){restore.context=EGL_NO_CONTEXT;restore.display=EGL_NO_DISPLAY;}
            if(context_!=EGL_NO_CONTEXT&&eglBindAPI(EGL_OPENGL_ES_API)&&eglMakeCurrent(display_,surface_,surface_,context_)){
                glDeleteFramebuffers(1,&fbo_);glDeleteTextures(1,&output_);glDeleteProgram(program_);
                eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            }
            if(context_!=EGL_NO_CONTEXT)eglDestroyContext(display_,context_);
            if(surface_!=EGL_NO_SURFACE)eglDestroySurface(display_,surface_);
            if(display_!=EGL_NO_DISPLAY)eglTerminate(display_);
        }
        if(gbm_)gbm_device_destroy(gbm_);
        if(fd_>=0)close(fd_);
        fd_=-1;gbm_=nullptr;display_=EGL_NO_DISPLAY;context_=EGL_NO_CONTEXT;surface_=EGL_NO_SURFACE;
        program_=output_=fbo_=0;width_=height_=major_=minor_=0;
    }
    static bool permits(const GstFrameLinkMemoryPolicy& p){
        constexpr uint32_t ops=FL_MEMORY_COPY|FL_MEMORY_READBACK;
        flMemoryDecision decisions[2]{};
        return p.version==1&&!p.reserved&&p.sessionGeneration&&p.paths[0].resourceType==FL_MEMORY_DMABUF&&
            (p.paths[0].operations&ops)==ops&&flMemoryEvaluateBoundary(p.requirements,p.paths,2,decisions)==FL_MEMORY_OK;
    }
    // Before the first frame: open the display, context and output surface on
    // the ring's render node (on Linux, flChannelInfo.adapterLUID; see
    // framelink_dmabuf.h), so the first read does not pay for them. A failure
    // changes nothing: read() then initializes from the frame's own memory
    // info, which stays authoritative.
    //
    // The calling thread need not be the one that reads. If it had no EGL
    // context of its own, its per-thread EGL state is released here: otherwise
    // the driver tears it down when the thread exits (a GLib pool thread, about
    // 15 s later), and on NVIDIA that teardown stalled the reading thread's
    // readback by about 40 ms (docs/m6/cpu-path/linux-gpu-readback-rate).
    //
    // keepThread: the caller is the thread that will read (FrameLinkEglReader),
    // so its per-thread EGL state is kept for those reads.
    bool prepare(const uint8_t node[8],uint32_t width,uint32_t height,bool keepThread=false){
        if(!width||!height||width>uint32_t(std::numeric_limits<GLsizei>::max())||height>uint32_t(std::numeric_limits<GLsizei>::max()))return false;
        uint32_t maj=0,min=0;nodeOf(node,maj,min);
        const bool threadHadContext=eglGetCurrentContext()!=EGL_NO_CONTEXT;
        bool ok=false;
        {Restore restore;ok=bind(maj,min,width,height);}
        if(!threadHadContext&&!keepThread)eglReleaseThread();
        return ok;
    }
    uint64_t gpuCopyBytes()const{return gpuCopyBytes_;}
    uint64_t readbackBytes()const{return readbackBytes_;}
    bool read(flChannel* channel,const flFrame& frame,const GstFrameLinkMemoryPolicy& policy,void* output,size_t bytes){
        gpuCopyBytes_=readbackBytes_=0;
        if(!output||!permits(policy))return false;
        flMemoryResourceInfo info{};info.version=FL_MEMORY_INFO_VERSION;
        if(flImageMemoryInfo(channel,frame.image,&info,sizeof(info))!=FL_OK||
           !(info.knownFields&FL_MEMORY_INFO_DEVICE)||info.deviceIdKind!=FL_MEMORY_DEVICE_DRM_NODE||
           (info.logicalFormat!=FL_FORMAT_BGRA8&&info.logicalFormat!=FL_FORMAT_RGBA8)||!info.width||!info.height||
           info.width>uint32_t(std::numeric_limits<GLsizei>::max())||info.height>uint32_t(std::numeric_limits<GLsizei>::max())||
           uint64_t(info.width)*info.height*4>bytes)return false;
        flDmabuf dma{};
        if(flImageDmabuf(frame.image,&dma)!=FL_OK||dma.planes!=1||dma.modifier!=DRM_FORMAT_MOD_LINEAR||
           dma.fourcc!=(info.logicalFormat==FL_FORMAT_BGRA8?DRM_FORMAT_ARGB8888:DRM_FORMAT_ABGR8888)||
           dma.stride[0]>uint32_t(INT32_MAX)||dma.offset[0]>uint32_t(INT32_MAX))return false;
        uint32_t maj=0,min=0;nodeOf(info.deviceId,maj,min);
        Restore restore;
        if(!bind(maj,min,info.width,info.height))return false;
        glBindFramebuffer(GL_FRAMEBUFFER,fbo_);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output_,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
        EGLint attrs[]={EGL_WIDTH,(EGLint)info.width,EGL_HEIGHT,(EGLint)info.height,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)dma.fourcc,
            EGL_DMA_BUF_PLANE0_FD_EXT,dma.fd[0],EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)dma.offset[0],EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)dma.stride[0],EGL_NONE};
        auto image=create_(display_,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);if(image==EGL_NO_IMAGE_KHR)return false;
        GLuint texture=0;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,texture);bind_(GL_TEXTURE_EXTERNAL_OES,image);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glUseProgram(program_);glUniform1i(glGetUniformLocation(program_,"source"),0);glUniform1i(glGetUniformLocation(program_,"swapRB"),info.logicalFormat==FL_FORMAT_BGRA8);
        glViewport(0,0,info.width,info.height);GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);
        bool ok=glGetError()==GL_NO_ERROR;
        if(ok){glDrawArrays(GL_TRIANGLE_STRIP,0,4);ok=glGetError()==GL_NO_ERROR;if(ok)gpuCopyBytes_=uint64_t(info.width)*info.height*4;}
        if(ok){glReadPixels(0,0,info.width,info.height,GL_RGBA,GL_UNSIGNED_BYTE,output);ok=glGetError()==GL_NO_ERROR;if(ok)readbackBytes_=uint64_t(info.width)*info.height*4;}
        // Finish even on failure before the caller may release the source.
        glFinish();ok=glGetError()==GL_NO_ERROR&&ok;
        glDeleteTextures(1,&texture);destroy_(display_,image);return ok;
    }
};

// One thread owns the staging context for the receiver's life (decision 15,
// Jon chose A; docs/m6/fault-coverage/remaining-cells/feed-startup). The
// first time a thread makes an EGL context current, the NVIDIA driver spends
// about 30 ms; when that thread exits, its teardown stalls other readers by
// about 40 ms (docs/m6/cpu-path/linux-gpu-readback-rate). Here both happen on
// this thread: the first at start(), before any frame, the second when the
// reader is destroyed, after the last frame. read() is synchronous: the caller
// keeps the frame, and its slot, until the GPU read has finished.
class FrameLinkEglReader {
    std::mutex mutex_;
    std::condition_variable wake_,done_;
    std::thread thread_;
    bool started_=false,prepared_=false,stop_=false,pending_=false,finished_=false;
    flChannel* channel_=nullptr;const flFrame* frame_=nullptr;const GstFrameLinkMemoryPolicy* policy_=nullptr;
    void* output_=nullptr;size_t bytes_=0;bool ok_=false;
    uint64_t gpuCopyBytes_=0,readbackBytes_=0;
    void run(uint8_t node[8],uint32_t width,uint32_t height){
        pthread_setname_np(pthread_self(),"fl-egl-reader");
        {
            FrameLinkEglReadback readback;
            const bool prepared=width&&height&&readback.prepare(node,width,height,true);
            {std::lock_guard<std::mutex> lock(mutex_);started_=true;prepared_=prepared;}
            done_.notify_all();
            for(;;){
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock,[&]{return pending_||stop_;});
                if(!pending_)break;
                lock.unlock();
                // A failed prepare leaves read() to initialize, still here.
                const bool ok=readback.read(channel_,*frame_,*policy_,output_,bytes_);
                lock.lock();
                ok_=ok;gpuCopyBytes_=readback.gpuCopyBytes();readbackBytes_=readback.readbackBytes();
                pending_=false;finished_=true;
                lock.unlock();done_.notify_all();
            }
        }   // readback releases its context and display on this thread
        eglReleaseThread();
    }
public:
    FrameLinkEglReader()=default;
    FrameLinkEglReader(const FrameLinkEglReader&)=delete;
    FrameLinkEglReader& operator=(const FrameLinkEglReader&)=delete;
    // Starts the thread and opens the staging context on it (node and geometry
    // as for FrameLinkEglReadback::prepare; zero geometry skips preparation).
    // Returns whether the context is ready; on false the first read initializes.
    bool start(const uint8_t node[8],uint32_t width,uint32_t height){
        if(thread_.joinable())return prepared_;
        uint8_t copy[8];memcpy(copy,node,8);
        thread_=std::thread([this,copy,width,height]()mutable{run(copy,width,height);});
        std::unique_lock<std::mutex> lock(mutex_);done_.wait(lock,[&]{return started_;});
        return prepared_;
    }
    // FrameLinkEglReadback::read on the reader thread; waits for it to finish.
    bool read(flChannel* channel,const flFrame& frame,const GstFrameLinkMemoryPolicy& policy,void* output,size_t bytes){
        if(!thread_.joinable()){static const uint8_t none[8]{};start(none,0,0);}
        std::unique_lock<std::mutex> lock(mutex_);
        channel_=channel;frame_=&frame;policy_=&policy;output_=output;bytes_=bytes;
        gpuCopyBytes_=readbackBytes_=0;finished_=false;pending_=true;
        wake_.notify_one();
        done_.wait(lock,[&]{return finished_;});
        return ok_;
    }
    uint64_t gpuCopyBytes()const{return gpuCopyBytes_;}
    uint64_t readbackBytes()const{return readbackBytes_;}
    bool threadId(std::thread::id& id)const{if(!thread_.joinable())return false;id=thread_.get_id();return true;}
    ~FrameLinkEglReader(){
        if(!thread_.joinable())return;
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;}
        wake_.notify_one();thread_.join();
    }
};

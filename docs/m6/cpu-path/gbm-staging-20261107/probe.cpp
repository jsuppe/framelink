#include <gbm.h>
#include <drm_fourcc.h>
#include <linux/dma-buf.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void syncbuf(int fd,unsigned long long flags){dma_buf_sync s{flags};CHECK(ioctl(fd,DMA_BUF_IOCTL_SYNC,&s)==0);}
int main(){
 int dev=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(dev>=0);auto*g=gbm_create_device(dev);CHECK(g);fprintf(stderr,"backend=%s\n",gbm_device_get_backend_name(g));
 puts("width,height,usage,iteration,method,map_ms,copy_ms,unmap_ms,verified");
 for(unsigned w:{1920u,3840u}){unsigned h=w==1920?1080:2160;
 for(unsigned usage:{unsigned(GBM_BO_USE_LINEAR|GBM_BO_USE_RENDERING),unsigned(GBM_BO_USE_LINEAR),unsigned(GBM_BO_USE_LINEAR|GBM_BO_USE_WRITE)}){
 auto*b=gbm_bo_create(g,w,h,DRM_FORMAT_ARGB8888,usage);if(!b){fprintf(stderr,"unsupported allocation w=%u usage=%u\n",w,usage);continue;}
 fprintf(stderr,"allocation w=%u usage=%u modifier=%llu planes=%d\n",w,usage,(unsigned long long)gbm_bo_get_modifier(b),gbm_bo_get_plane_count(b));
 CHECK(gbm_bo_get_modifier(b)==DRM_FORMAT_MOD_LINEAR);CHECK(gbm_bo_get_plane_count(b)==1);
 int fd=gbm_bo_get_fd(b);CHECK(fd>=0);struct stat st{};CHECK(fstat(fd,&st)==0);unsigned stride=gbm_bo_get_stride(b);CHECK(st.st_size>=(long long)stride*h);
 auto*base=(unsigned char*)mmap(nullptr,st.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(base!=MAP_FAILED);
 std::vector<unsigned char>dst(size_t(w)*h*4,0);
 for(unsigned iter=0;iter<8;iter++){
 syncbuf(fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE);
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*4;x++)base[size_t(y)*stride+x]=(x+y+iter*17)%251;
 syncbuf(fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE);
 // Alternate order to retain rather than conceal warm-cache/phase effects.
 for(unsigned order=0;order<2;order++){unsigned method=order^(iter&1);void*data=nullptr;unsigned pitch=stride;double t=now();unsigned char*src;
 if(method){src=(unsigned char*)gbm_bo_map(b,0,0,w,h,GBM_BO_TRANSFER_READ,&pitch,&data);if(!src||src==MAP_FAILED){fprintf(stderr,"unsupported GBM map w=%u usage=%u iter=%u\n",w,usage,iter);continue;}}
 else{syncbuf(fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ);src=base;}
 double mapped=now()-t;CHECK(pitch>=w*4);t=now();
 for(unsigned y=0;y<h;y++)memcpy(dst.data()+size_t(y)*w*4,src+size_t(y)*pitch,w*4);
 double copied=now()-t;t=now();if(method)gbm_bo_unmap(b,data);else syncbuf(fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ);double unmapped=now()-t;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*4;x++)CHECK(dst[size_t(y)*w*4+x]==(x+y+iter*17)%251);
 printf("%u,%u,%u,%u,%s,%.6f,%.6f,%.6f,yes\n",w,h,usage,iter,method?"gbm_map":"dmabuf_mmap",mapped,copied,unmapped);fflush(stdout);
 }}munmap(base,st.st_size);close(fd);gbm_bo_destroy(b);
 }}gbm_device_destroy(g);close(dev);
}

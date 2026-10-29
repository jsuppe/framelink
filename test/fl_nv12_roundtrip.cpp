#include "framelink.h"
#include "framelink_dmabuf.h"
#include <drm_fourcc.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"pid%d line%d: %s\n",getpid(),__LINE__,#x);exit(1);}}while(0)
static unsigned char value(int frame,unsigned p,unsigned y,unsigned x){return (frame*17+p*79+y*3+x*7)&255;}
static flChannel* connect(const char* name){flChannel*c=nullptr;for(int n=0;n<200;n++){if(flOpenProducer(name,&c)==FL_OK)return c;usleep(10000);}return nullptr;}
int main(){
 alarm(20);
 char name[80];snprintf(name,sizeof(name),"nv12-%d",getpid());
 int ack[2],done[2];CHECK(pipe(ack)==0);CHECK(pipe(done)==0);
 pid_t child=fork();CHECK(child>=0);
 if(child==0){
  close(ack[1]);close(done[0]);flChannel*c=connect(name);CHECK(c);
  for(int n=0;n<12;n++){
   if(n==4){flChannelInfo old{};CHECK(flQuery(c,&old)==FL_OK);CHECK(flRequestGeometry(c,194,98,FL_FORMAT_NV12)==FL_OK);flChannelInfo q{};CHECK(flQuery(c,&q)==FL_OK);CHECK(q.width==194 && q.height==98 && q.generation>old.generation);}
   if(n==8){flClose(c);c=connect(name);CHECK(c);}
   flChannelInfo q{};CHECK(flQuery(c,&q)==FL_OK);CHECK(q.format==FL_FORMAT_NV12 && q.poolSize==8);
   flBuffer b{};CHECK(flAcquireBuffer(c,&b,2000)==FL_OK);
   flDmabuf d{};CHECK(flImageDmabuf(b.image,&d)==FL_OK);CHECK(d.planes==2 && d.fourcc==DRM_FORMAT_NV12 && d.modifier==DRM_FORMAT_MOD_LINEAR);
   void* single=nullptr;CHECK(flImageMap(b.image,&single,nullptr)==FL_FORMAT_UNSUPPORTED);
   flMappedImage m{};CHECK(flImageMapPlanes(b.image,&m)==FL_OK);CHECK(m.planes==2);
   for(unsigned p=0;p<2;p++){CHECK(m.plane[p].rowBytes==q.width);CHECK(m.plane[p].rows==(p?q.height/2:q.height));
    for(unsigned y=0;y<m.plane[p].rows;y++)for(unsigned x=0;x<q.width;x++)((unsigned char*)m.plane[p].pixels)[y*m.plane[p].stride+x]=value(n,p,y,x);
   }
   flImageUnmap(b.image);CHECK(flSubmit(c,&b,n)==FL_OK);
   char byte;CHECK(read(ack[0],&byte,1)==1);
   if(n==2){
    flChannelInfo before{},after{};CHECK(flQuery(c,&before)==FL_OK);
    CHECK(flRequestGeometry(c,194,98,FL_FORMAT_NV12)==FL_OK);
    CHECK(flQuery(c,&after)==FL_OK);CHECK(after.generation==before.generation && after.width==before.width);
    CHECK(write(done[1],"x",1)==1);CHECK(read(ack[0],&byte,1)==1);
   }
  }flClose(c);_exit(0);
 }
 close(ack[0]);close(done[1]);flChannel*c=nullptr;
 CHECK(flCreateChannelEx(name,131,66,FL_FORMAT_NV12,8,FL_MAP_CPU,&c)==FL_INVALID);
 CHECK(flCreateChannelEx(name,130,66,FL_FORMAT_NV12,8,FL_MAP_CPU,&c)==FL_OK);
 for(int n=0;n<12;n++){
  flFrame f{};flResult r;for(int j=0;j<500;j++){r=flAcquireFrame(c,&f,10);if(r==FL_OK)break;CHECK(r==FL_TIMEOUT || r==FL_DISCONNECTED);usleep(1000);}CHECK(r==FL_OK);CHECK(f.ptsNs==n);
  flChannelInfo q{};CHECK(flQuery(c,&q)==FL_OK);flMappedImage m{};CHECK(flImageMapPlanes(f.image,&m)==FL_OK);
  for(unsigned p=0;p<2;p++)for(unsigned y=0;y<m.plane[p].rows;y++)for(unsigned x=0;x<q.width;x++)CHECK(((unsigned char*)m.plane[p].pixels)[y*m.plane[p].stride+x]==value(n,p,y,x));
  if(n==2){CHECK(write(ack[1],"h",1)==1);char byte;CHECK(read(done[0],&byte,1)==1);
   // Held image remains valid across the refused resize.
   CHECK(((unsigned char*)m.plane[1].pixels)[0]==value(n,1,0,0));
  }
  flImageUnmap(f.image);CHECK(flRelease(c,&f)==FL_OK);CHECK(write(ack[1],"x",1)==1);
 }
 int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);flClose(c);
 puts("NV12 actual DMA-BUF two-process Y/UV pattern, resize, reattach and eight-slot FD transport passed");
}

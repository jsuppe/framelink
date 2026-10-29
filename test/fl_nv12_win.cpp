#include "framelink.h"
#include "framelink_d3d11.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static unsigned char value(unsigned n,unsigned p,unsigned y,unsigned x){return (n*17+p*79+y*3+x*7)&255;}
int main(int argc,char**argv){
 CHECK(argc==3 || argc==5);bool producer=strcmp(argv[1],"produce")==0;flChannel*ch=nullptr;
 const unsigned w=argc==5?(unsigned)atoi(argv[3]):130,h=argc==5?(unsigned)atoi(argv[4]):66;
 if(producer){for(int i=0;i<500;i++){if(flOpenProducer(argv[2],&ch)==FL_OK)break;Sleep(10);}CHECK(ch);}
 else{
  CHECK(flCreateChannel(argv[2],131,h,FL_FORMAT_NV12,4,&ch)==FL_INVALID);
  CHECK(flCreateChannel(argv[2],w,65,FL_FORMAT_NV12,4,&ch)==FL_INVALID);
  CHECK(flCreateChannel(argv[2],w,h,FL_FORMAT_NV12,4,&ch)==FL_OK);
 }
 flChannelInfo info{};CHECK(flQuery(ch,&info)==FL_OK);CHECK(info.format==FL_FORMAT_NV12);
 ComPtr<ID3D11DeviceContext>ctx;flChannelD3D11Device(ch)->GetImmediateContext(&ctx);
 if(producer){
  CHECK(flRequestGeometry(ch,131,h,FL_FORMAT_NV12)==FL_INVALID);
  CHECK(flRequestGeometry(ch,w,h,FL_FORMAT_BGRA8)==FL_FORMAT_UNSUPPORTED);
  for(unsigned n=0;n<400;n++){
   flBuffer b{};if(flAcquireBuffer(ch,&b,100)!=FL_OK)break;
   std::vector<unsigned char>data(w*h*3/2);
   for(unsigned p=0;p<2;p++)for(unsigned y=0;y<(p?h/2:h);y++)for(unsigned x=0;x<w;x++)data[(p?w*h:0)+y*w+x]=value(n,p,y,x);
   ctx->UpdateSubresource(flImageD3D11(b.image),0,nullptr,data.data(),w,0);
   if(flSubmit(ch,&b,n)!=FL_OK)break;Sleep(2);
  }
 }else{
  ComPtr<ID3D11Texture2D>staging;
  for(unsigned n=0;n<24;n++){
   flFrame f{};flResult r=FL_TIMEOUT;for(unsigned i=0;i<500;i++){r=flAcquireFrame(ch,&f,10);if(r==FL_OK)break;Sleep(2);}CHECK(r==FL_OK);
   auto*tex=flImageD3D11(f.image);D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);CHECK(d.Format==DXGI_FORMAT_NV12);
   if(!staging){d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CHECK(SUCCEEDED(flChannelD3D11Device(ch)->CreateTexture2D(&d,nullptr,&staging)));}
   ctx->CopyResource(staging.Get(),tex);D3D11_MAPPED_SUBRESOURCE m{};CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)));
   for(unsigned p=0;p<2;p++)for(unsigned y=0;y<(p?h/2:h);y++)for(unsigned x=0;x<w;x++)CHECK(((unsigned char*)m.pData)[(p?m.RowPitch*h:0)+y*m.RowPitch+x]==value((unsigned)f.ptsNs,p,y,x));
   ctx->Unmap(staging.Get(),0);CHECK(flRelease(ch,&f)==FL_OK);
  }
  puts("24 cross-process shared NV12 textures: every Y/UV byte and fence-ordered readback passed");
 }
 flClose(ch);return 0;
}

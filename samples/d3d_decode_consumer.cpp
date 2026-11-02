// Separate-process native receiver with a real DXGI window/swapchain.
// CPU waits retire GPU reads; no frame payload mapping in the display path.
#include "framelink_d3d11.h"
#include <gst/d3d11/gstd3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
static void require(bool ok,const char*m){if(!ok)throw std::runtime_error(m);}
static LRESULT CALLBACK windowProc(HWND w,UINT m,WPARAM p,LPARAM l){return DefWindowProc(w,m,p,l);}
int main(int argc,char**argv){
 gst_init(&argc,&argv);if(argc<2||argc>3){fprintf(stderr,"usage: fl_d3d_decode_consumer CHANNEL [--verify]\n");return 2;}
 const bool verify=argc==3&&std::string(argv[2])=="--verify";if(argc==3&&!verify)return 2;
 try{
 flChannel*channel=nullptr;require(flCreateChannelEx(argv[1],320,240,FL_FORMAT_BGRA8,2,FL_GPU_RENDER|FL_REQUIRE_FRAME_METADATA,&channel)==FL_OK,"create two-image BGRA ring");
 auto*device=flChannelD3D11Device(channel);auto*gd=gst_d3d11_device_new_wrapped(device);require(gd,"wrap device");
 ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);
 ComPtr<IDXGIDevice>dxgi;require(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))),"DXGI device");ComPtr<IDXGIAdapter>adapter;require(SUCCEEDED(dxgi->GetAdapter(&adapter)),"adapter");ComPtr<IDXGIFactory2>factory;require(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))),"factory");
 WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName=L"FrameLinkDecodeExample";RegisterClassW(&cls);
 HWND window=CreateWindowW(cls.lpszClassName,L"FrameLink: GPU-decoded NV12 converted to shared BGRA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,640,480,nullptr,nullptr,cls.hInstance,nullptr);require(window,"window");
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=320;desc.Height=240;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ComPtr<IDXGISwapChain1>swap;require(SUCCEEDED(factory->CreateSwapChainForHwnd(device,window,&desc,nullptr,nullptr,&swap)),"swapchain");
 GstVideoInfo info{};gst_video_info_set_format(&info,GST_VIDEO_FORMAT_BGRA,320,240);info.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;
 auto*converter=gst_d3d11_converter_new(gd,&info,&info,nullptr);require(converter,"display converter");
 D3D11_QUERY_DESC qdesc{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query>query;require(SUCCEEDED(device->CreateQuery(&qdesc,&query)),"GPU completion query");
 unsigned count=0;uint64_t previousSeq=0,gaps=0;int64_t previousPts=-1;auto deadline=g_get_monotonic_time()+15000000;
 while(count<60){MSG message;while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}
  flFrame frame{};auto result=flAcquireFrame(channel,&frame,100);
  if(result==FL_TIMEOUT||result==FL_DISCONNECTED){require(g_get_monotonic_time()<deadline,"consumer idle deadline");continue;}
  require(result==FL_OK,"acquire");require(frame.ptsNs>previousPts,"PTS must increase");previousPts=frame.ptsNs;if(previousSeq)gaps+=frame.sequence-previousSeq-1;previousSeq=frame.sequence;
  flFrameMetadata metadata{};require(flGetFrameMetadata(channel,&frame,&metadata)==FL_OK,"frame metadata");
  require((metadata.flags&FL_FRAME_HAS_DURATION)&&metadata.durationNs>=33333333&&metadata.durationNs<=33333334,"decoded frame duration");
  if(verify){
   // Qualification-only readback, explicitly excluded from zero-map display mode.
   flMappedImage map{};require(flImageMapPlanes(frame.image,&map)==FL_OK,"verification readback");
   const unsigned char expected[7][3]={{255,255,255},{0,255,255},{255,255,0},{0,255,0},{255,0,255},{0,0,255},{255,0,0}};
   bool pixels=true;for(unsigned bar=0;bar<7;++bar){const auto*pixel=(const unsigned char*)map.plane[0].pixels+60*map.plane[0].stride+(320*(2*bar+1)/14)*4;for(unsigned c=0;c<3;++c)if(std::abs(int(pixel[c])-expected[bar][c])>24)pixels=false;}
   flImageUnmap(frame.image);require(pixels,"decoded SMPTE color-bar mismatch");
  }
  auto*in=gst_buffer_new();auto*im=gst_d3d11_allocator_alloc_wrapped(nullptr,gd,flImageD3D11(frame.image),0,nullptr,nullptr);require(im,"wrap input");gst_buffer_append_memory(in,im);
  ComPtr<ID3D11Texture2D>back;require(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&back))),"swapchain backbuffer");auto*out=gst_buffer_new();auto*om=gst_d3d11_allocator_alloc_wrapped(nullptr,gd,back.Get(),0,nullptr,nullptr);require(om,"wrap backbuffer");gst_buffer_append_memory(out,om);
  require(gst_d3d11_converter_convert_buffer(converter,in,out),"GPU render to surface");
  // Retire the read only after the consumer's actual GPU access has completed.
  gst_d3d11_device_lock(gd);context->End(query.Get());context->Flush();HRESULT hr;auto end=g_get_monotonic_time()+2000000;
  do{hr=context->GetData(query.Get(),nullptr,0,0);if(hr==S_FALSE)g_usleep(100);}while(hr==S_FALSE&&g_get_monotonic_time()<end);gst_d3d11_device_unlock(gd);require(hr==S_OK,"GPU completion timeout");
  require(SUCCEEDED(swap->Present(1,0)),"present");gst_buffer_unref(in);gst_buffer_unref(out);require(flRelease(channel,&frame)==FL_OK,"release after GPU completion");++count;deadline=g_get_monotonic_time()+15000000;
 }
 printf("received=%u gpu_completed=%u accepted_presents=%u sequence_gaps=%llu validation_readbacks=%u pool=2; physical scanout unmeasured\n",count,count,count,(unsigned long long)gaps,verify?count:0);
 gst_object_unref(converter);swap.Reset();DestroyWindow(window);gst_object_unref(gd);flClose(channel);return gaps?3:0;
 }catch(const std::exception&e){fprintf(stderr,"native display example failed: %s\n",e.what());return 1;}
}

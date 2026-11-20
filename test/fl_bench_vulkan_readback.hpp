#pragma once
// Linux receiver verification for the rate harness: the counterpart of the
// Windows D3D11 staging copy and shared-fence completion. CPU reads of a GPU
// DMA-BUF mapping are uncached on discrete GPUs (about 9 MB/s measured on
// NVIDIA), so the receiver imports each held frame with framelink_vulkan.h,
// copies it on the GPU into host-cached memory, hands FrameLink the copy's
// real completion fence (SYNC_FD), and verifies the copied bytes.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "framelink_vulkan.h"
#include <dlfcn.h>
#include <map>
#include <vector>

struct VulkanReadback {
#define FL_RB_FUNCS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
  X(GetPhysicalDeviceQueueFamilyProperties) X(EnumerateDeviceExtensionProperties) X(GetPhysicalDeviceMemoryProperties) \
  X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateCommandPool) X(DestroyCommandPool) \
  X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer) X(CmdPipelineBarrier) \
  X(CmdCopyImageToBuffer) X(QueueSubmit) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(CreateBuffer) \
  X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) \
  X(UnmapMemory) X(InvalidateMappedMemoryRanges)
#define FL_RB_DECL(n) PFN_vk##n n=nullptr;
 FL_RB_FUNCS(FL_RB_DECL)
#undef FL_RB_DECL
 void* library=nullptr;PFN_vkGetInstanceProcAddr gipa=nullptr;VkInstance instance=VK_NULL_HANDLE;VkPhysicalDevice phys=VK_NULL_HANDLE;
 VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;uint32_t family=0;flVkContext ctx{};flVkSync sync{};
 VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cmd=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
 VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;uint8_t* host=nullptr;VkDeviceSize size=0;bool coherent=false;
 std::map<uint32_t,flVkImage> images;

 // Device on the ring's GPU with the interop extensions; FrameLink check.
 void start(flChannel*channel,VkDeviceSize bytes){
  library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);CHECK(library);
  gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));CHECK(gipa);
  auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE,"vkCreateInstance"));CHECK(create);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;CHECK(create(&ici,nullptr,&instance)==VK_SUCCESS);
#define FL_RB_LOAD(n) n=reinterpret_cast<PFN_vk##n>(gipa(instance,"vk" #n));CHECK(n);
  FL_RB_FUNCS(FL_RB_LOAD)
#undef FL_RB_LOAD
  flChannelInfo ring{};CHECK(flQuery(channel,&ring)==FL_OK);
  uint32_t n=0;EnumeratePhysicalDevices(instance,&n,nullptr);std::vector<VkPhysicalDevice> all(n);EnumeratePhysicalDevices(instance,&n,all.data());
  uint32_t required=0;auto names=flVkRequiredDeviceExtensions(&required);
  for(auto candidate:all){
   uint32_t en=0;EnumerateDeviceExtensionProperties(candidate,nullptr,&en,nullptr);std::vector<VkExtensionProperties> ext(en);EnumerateDeviceExtensionProperties(candidate,nullptr,&en,ext.data());
   auto has=[&](const char*name){for(auto&e:ext)if(!strcmp(e.extensionName,name))return true;return false;};
   bool ok=true;for(uint32_t i=0;i<required;++i)ok=ok&&has(names[i]);if(!ok||!has("VK_EXT_physical_device_drm"))continue;
   uint32_t qn=0;GetPhysicalDeviceQueueFamilyProperties(candidate,&qn,nullptr);std::vector<VkQueueFamilyProperties> qf(qn);GetPhysicalDeviceQueueFamilyProperties(candidate,&qn,qf.data());
   uint32_t q=UINT32_MAX;for(uint32_t i=0;i<qn&&q==UINT32_MAX;++i)if(qf[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT|VK_QUEUE_TRANSFER_BIT))q=i;if(q==UINT32_MAX)continue;
   std::vector<const char*> enable(names,names+required);enable.push_back("VK_EXT_physical_device_drm");
   float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=q;qci.queueCount=1;qci.pQueuePriorities=&priority;
   VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=uint32_t(enable.size());dci.ppEnabledExtensionNames=enable.data();
   VkDevice d=VK_NULL_HANDLE;if(CreateDevice(candidate,&dci,nullptr,&d)!=VK_SUCCESS)continue;
   flVkContext c{};CHECK(flVkInitContext(&c,instance,candidate,d,gipa,nullptr)==FL_OK);
   if(flVkCheckAdapter(&c,&ring)!=FL_OK){DestroyDevice(d,nullptr);continue;}
   phys=candidate;device=d;family=q;ctx=c;break;
  }
  CHECK(device); // no Vulkan device on the ring's GPU
  GetDeviceQueue(device,family,0,&queue);CHECK(flVkCreateSync(&ctx,channel,&sync)==FL_OK);
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pci.queueFamilyIndex=family;CHECK(CreateCommandPool(device,&pci,nullptr,&pool)==VK_SUCCESS);
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;CHECK(AllocateCommandBuffers(device,&ai,&cmd)==VK_SUCCESS);
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(CreateFence(device,&fi,nullptr,&fence)==VK_SUCCESS);
  size=bytes;VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;CHECK(CreateBuffer(device,&bi,nullptr,&buffer)==VK_SUCCESS);
  VkMemoryRequirements req{};GetBufferMemoryRequirements(device,buffer,&req);VkPhysicalDeviceMemoryProperties mp{};GetPhysicalDeviceMemoryProperties(phys,&mp);
  // Host-cached memory: CPU verification reads at memory speed.
  int type=-1;for(int pass=0;pass<2&&type<0;++pass)for(uint32_t t=0;t<mp.memoryTypeCount&&type<0;++t){
   const auto f=mp.memoryTypes[t].propertyFlags;const VkMemoryPropertyFlags want=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|(pass?0:VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
   if((req.memoryTypeBits&(1u<<t))&&(f&want)==want){type=int(t);coherent=f&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;}}
  CHECK(type>=0);VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=uint32_t(type);
  CHECK(AllocateMemory(device,&ma,nullptr,&memory)==VK_SUCCESS);CHECK(BindBufferMemory(device,buffer,memory,0)==VK_SUCCESS);
  void*p=nullptr;CHECK(MapMemory(device,memory,0,VK_WHOLE_SIZE,0,&p)==VK_SUCCESS);host=static_cast<uint8_t*>(p);
 }
 // GPU copy of the held frame into host memory (tightly packed rows: BGRA, or
 // NV12 luma then chroma); returns the copy's completion fd for FrameLink.
 int copy(flChannel*channel,const flFrame&frame,bool nv12,uint32_t width,uint32_t height){
  auto it=images.find(frame.slot);
  if(it!=images.end()&&!flVkImageCurrent(channel,&it->second)){DeviceWaitIdle(device);for(auto&i:images)flVkDestroyImage(&ctx,&i.second);images.clear();it=images.end();}
  if(it==images.end()){flVkImportDesc desc{};desc.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;desc.flags=FL_VK_IMPORT_NO_VIEW;flVkImage img{};
   CHECK(flVkImportImage(&ctx,channel,frame.image,&desc,&img)==FL_OK);it=images.emplace(frame.slot,img).first;}
  const flVkImage&img=it->second;
  CHECK(ResetCommandBuffer(cmd,0)==VK_SUCCESS);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;CHECK(BeginCommandBuffer(cmd,&begin)==VK_SUCCESS);
  VkImageMemoryBarrier b[2];uint32_t n=flVkAcquireBarriers(&img,family,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,0,b);
  CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,n,b);
  auto region=[&](VkImageAspectFlags aspect,uint32_t w,uint32_t h,VkDeviceSize offset){VkBufferImageCopy r{};r.bufferOffset=offset;r.imageSubresource.aspectMask=aspect;r.imageSubresource.layerCount=1;r.imageExtent={w,h,1};return r;};
  const VkDeviceSize luma=VkDeviceSize(width)*height;
  if(!nv12){auto r=region(VK_IMAGE_ASPECT_COLOR_BIT,width,height,0);CmdCopyImageToBuffer(cmd,img.image[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&r);}
  else if(img.imageCount==2){auto r0=region(VK_IMAGE_ASPECT_COLOR_BIT,width,height,0),r1=region(VK_IMAGE_ASPECT_COLOR_BIT,width/2,height/2,luma);
   CmdCopyImageToBuffer(cmd,img.image[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&r0);CmdCopyImageToBuffer(cmd,img.image[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&r1);}
  else{VkBufferImageCopy r[2]={region(VK_IMAGE_ASPECT_PLANE_0_BIT,width,height,0),region(VK_IMAGE_ASPECT_PLANE_1_BIT,width/2,height/2,luma)};
   CmdCopyImageToBuffer(cmd,img.image[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,2,r);}
  n=flVkReleaseBarriers(&img,family,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,0,b);
  CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,n,b);
  CHECK(EndCommandBuffer(cmd)==VK_SUCCESS);CHECK(ResetFences(device,1,&fence)==VK_SUCCESS);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;si.signalSemaphoreCount=1;si.pSignalSemaphores=&sync.exportable;
  CHECK(QueueSubmit(queue,1,&si,fence)==VK_SUCCESS);
  VkSemaphoreGetFdInfoKHR get{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};get.semaphore=sync.exportable;get.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  int fd=-1;CHECK(ctx.GetSemaphoreFdKHR(device,&get,&fd)==VK_SUCCESS);return fd; // -1: already complete
 }
 // The copied bytes, once the GPU copy has completed.
 const uint8_t* data(){
  CHECK(WaitForFences(device,1,&fence,VK_TRUE,5000000000ULL)==VK_SUCCESS);
  if(!coherent){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory;range.size=VK_WHOLE_SIZE;CHECK(InvalidateMappedMemoryRanges(device,1,&range)==VK_SUCCESS);}
  return host;
 }
 ~VulkanReadback(){
  if(device){DeviceWaitIdle(device);for(auto&i:images)flVkDestroyImage(&ctx,&i.second);flVkDestroySync(&ctx,&sync);
   UnmapMemory(device,memory);DestroyBuffer(device,buffer,nullptr);FreeMemory(device,memory,nullptr);DestroyFence(device,fence,nullptr);DestroyCommandPool(device,pool,nullptr);DestroyDevice(device,nullptr);}
  if(instance)DestroyInstance(instance,nullptr);
 }
#undef FL_RB_FUNCS
};

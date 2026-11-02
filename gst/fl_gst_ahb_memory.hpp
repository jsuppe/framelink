#pragma once
#include <gst/gst.h>
#include <android/hardware_buffer.h>
typedef struct {GstAllocator parent;} GstFrameLinkAhbAllocator;
typedef struct {GstAllocatorClass parent;} GstFrameLinkAhbAllocatorClass;
struct FrameLinkAhbMemory {GstMemory memory;AHardwareBuffer* buffer;};
G_DEFINE_TYPE(GstFrameLinkAhbAllocator,gst_framelink_ahb_allocator,GST_TYPE_ALLOCATOR)
static gpointer ahbMap(GstMemory*,gsize,GstMapFlags){return nullptr;}
static void ahbUnmap(GstMemory*){}
static GstMemory* ahbCopy(GstMemory*,gssize,gssize){return nullptr;}
static GstMemory* ahbShare(GstMemory*,gssize,gssize){return nullptr;}
static void gst_framelink_ahb_allocator_class_init(GstFrameLinkAhbAllocatorClass* klass){
    GST_ALLOCATOR_CLASS(klass)->free=[](GstAllocator*,GstMemory*m){
        auto*memory=reinterpret_cast<FrameLinkAhbMemory*>(m);AHardwareBuffer_release(memory->buffer);g_free(memory);
    };
}
static void gst_framelink_ahb_allocator_init(GstFrameLinkAhbAllocator* self){
    auto*a=GST_ALLOCATOR(self);a->mem_type="FrameLinkAHardwareBuffer";
    a->mem_map=ahbMap;a->mem_unmap=ahbUnmap;a->mem_copy=ahbCopy;a->mem_share=ahbShare;
    GST_OBJECT_FLAG_SET(a,GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

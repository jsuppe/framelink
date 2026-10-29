#pragma once
#include <cstdint>
#include <limits>
#include "framelink.h"
namespace fl {
inline bool validGeometry(uint32_t w,uint32_t h,flFormat f) {
    return w && h && w<=8192 && h<=8192 &&
        (f!=FL_FORMAT_NV12 || (!(w&1) && !(h&1)));
}
inline bool planeFits(uint64_t size,uint64_t offset,uint64_t stride,uint64_t rows,uint64_t rowBytes) {
    if(!rows || !rowBytes || stride<rowBytes || offset>size) return false;
    if(rows-1 && stride>(std::numeric_limits<uint64_t>::max()-rowBytes)/(rows-1)) return false;
    return (rows-1)*stride+rowBytes<=size-offset;
}
}

#include "fl_nv21.h"
#include <cstdio>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return false; } } while (0)
struct Plane { void* data; unsigned rowStride, pixelStride; };

static bool roundtrip(unsigned w, unsigned h, unsigned padding) {
    const unsigned stride = w + padding;
    std::vector<uint8_t> y(size_t(stride)*h + 2, 0xee), vu(size_t(stride)*h/2 + 2, 0xee);
    std::vector<uint8_t> scratch(size_t(w)*h*3/2 + 2, 0xdd);
    Plane p[] = {{y.data()+1,stride,1}, {vu.data()+2,stride,2}, {vu.data()+1,stride,2}};
    for (unsigned r=0;r<h;++r) for (unsigned x=0;x<w;++x) y[1+size_t(r)*stride+x]=uint8_t(r+x);
    for (unsigned r=0;r<h/2;++r) for (unsigned x=0;x<w;x+=2) {
        vu[1+size_t(r)*stride+x]=uint8_t(173+r+x);
        vu[2+size_t(r)*stride+x]=uint8_t(79+r+x);
    }
    // Repeated map/unmap must not double-swap cached chroma, touch padding,
    // or retain stale scratch. Mutations must survive the next map.
    for (unsigned cycle=0;cycle<4;++cycle) {
        CHECK(fl::readNv21(p,w,h,scratch.data()+1));
        for (unsigned r=0;r<h;++r) for (unsigned x=0;x<w;++x)
            CHECK(scratch[1+size_t(r)*w+x]==uint8_t(r+x+cycle));
        for (unsigned r=0;r<h/2;++r) for (unsigned x=0;x<w;x+=2) {
            CHECK(scratch[1+size_t(w)*h+size_t(r)*w+x]==uint8_t(79+r+x+cycle));
            CHECK(scratch[2+size_t(w)*h+size_t(r)*w+x]==uint8_t(173+r+x+cycle));
        }
        for (size_t i=1;i+1<scratch.size();++i) ++scratch[i];
        CHECK(fl::writeNv21(p,w,h,scratch.data()+1));
        CHECK(y.front()==0xee && y.back()==0xee && vu.front()==0xee && vu.back()==0xee);
        CHECK(scratch.front()==0xdd && scratch.back()==0xdd);
        for (unsigned r=0;r<h;++r) for (unsigned x=w;x<stride;++x) CHECK(y[1+size_t(r)*stride+x]==0xee);
        for (unsigned r=0;r<h/2;++r) for (unsigned x=w;x<stride;++x) CHECK(vu[1+size_t(r)*stride+x]==0xee);
    }
    // Unsupported plane descriptions must leave BOTH sides untouched so the
    // caller can use its generic converter, including native NV12 aliasing.
    for (unsigned variant=0;variant<9;++variant) {
        Plane q[]={p[0],p[1],p[2]}; unsigned tw=w,th=h;
        switch(variant) {
        case 0: std::swap(q[1].data,q[2].data); break;
        case 1: q[1].pixelStride=1; break;
        case 2: q[0].pixelStride=2; break;
        case 3: q[2].rowStride++; break;
        case 4: q[0].rowStride=w-1; break;
        case 5: q[1].rowStride=q[2].rowStride=w-1; break;
        case 6: tw--; break;
        case 7: th=0; break;
        case 8: q[2].data=nullptr; break;
        }
        const auto before=scratch, beforeY=y, beforeVU=vu;
        CHECK(!fl::readNv21(q,tw,th,scratch.data()+1));
        CHECK(!fl::writeNv21(q,tw,th,scratch.data()+1));
        CHECK(scratch==before && y==beforeY && vu==beforeVU);
    }
    return true;
}
int main() {
    if (!roundtrip(2,2,0) || !roundtrip(130,66,62) || !roundtrip(1920,1080,0)) return 1;
    std::puts("NV21 bulk conversion: all-byte roundtrips, padding, guards and fallback passed");
}

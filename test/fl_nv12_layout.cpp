#include "fl_layout.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(){
 CHECK(fl::validGeometry(1920,1080,FL_FORMAT_NV12));
 CHECK(!fl::validGeometry(1919,1080,FL_FORMAT_NV12));
 CHECK(!fl::validGeometry(1920,1079,FL_FORMAT_NV12));
 CHECK(!fl::validGeometry(0,1080,FL_FORMAT_NV12));
 CHECK(!fl::validGeometry(8194,1080,FL_FORMAT_NV12));
 CHECK(fl::validGeometry(17,19,FL_FORMAT_BGRA8));
 CHECK(fl::planeFits(256*66,0,256,66,130));
 CHECK(fl::planeFits(256*99,256*66,256,33,130));
 CHECK(!fl::planeFits(256*66,256*66,256,33,130));
 CHECK(!fl::planeFits(4096,0,129,2,130));
 CHECK(!fl::planeFits(4096,4097,256,1,130));
 CHECK(!fl::planeFits(UINT64_MAX,0,UINT64_MAX,3,130));
 CHECK(!fl::planeFits(4096,0,256,0,130));
 puts("NV12 geometry, padded plane bounds and overflow checks passed");
}

#define NOMINMAX
#include "win_sha256_rows.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
  WinSha256Rows hash;
  const unsigned char abc[]={'a','b','c'};
  const char* empty="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  const char* expected="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  for(unsigned i=0;i<3;++i) {
    CHECK(hash.digest(nullptr,0,0,0)==empty);
    CHECK(hash.digest(abc,3,3,1)==expected);
    CHECK(hash.digest(abc,1,1,3)==expected);
  }
  unsigned char padded[]={'a','b',0,0,'c','d',0,0,'e','f',0,0,'g','h',0,0};
  const char* rows="9c56cc51b374c3ba189210d5b6d4bf57790d351c96c47c02190ecf1e430635ab";
  CHECK(hash.digest(padded,4,2,4)==rows);
  for(unsigned y=0;y<4;++y) padded[y*4+2]=padded[y*4+3]=255;
  CHECK(hash.digest(padded,4,2,4)==rows);
  CHECK(hash.digest(padded,1,2,4).empty());
  CHECK(hash.digest(nullptr,4,2,4).empty());
  CHECK(hash.digest(padded,std::numeric_limits<size_t>::max(),2,2).empty());
  CHECK(hash.digest(abc,3,3,1)==expected); // invalid layouts consumed no bytes
  std::puts("CNG full SHA256 known vectors, reusable reset, active rows and invalid layout checks passed");
}

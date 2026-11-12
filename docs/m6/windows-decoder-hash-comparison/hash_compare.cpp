#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <glib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
static void check(bool ok,const char*what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static void status(NTSTATUS s,const char*what){if(s<0){std::fprintf(stderr,"NTSTATUS %08lx: %s\n",static_cast<unsigned long>(s),what);std::exit(1);}}
static std::string hex(const unsigned char*p,size_t n){static const char*h="0123456789abcdef";std::string out;out.reserve(n*2);for(size_t i=0;i<n;++i){out+=h[p[i]>>4];out+=h[p[i]&15];}return out;}
// Reusable object allocation is outside measured frames; each FinishHash resets it.
struct Cng {
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<unsigned char> object;
 Cng(){status(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_HASH_REUSABLE_FLAG),"open SHA256 reusable");DWORD bytes=0,length=0,digestLength=0;
  status(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof length,&bytes,0),"object length");check(bytes==sizeof length&&length,"object length value");
  status(BCryptGetProperty(algorithm,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&digestLength),sizeof digestLength,&bytes,0),"digest length");check(bytes==sizeof digestLength&&digestLength==32,"full SHA256 length");object.resize(length);
  status(BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,BCRYPT_HASH_REUSABLE_FLAG),"create reusable SHA256");}
 ~Cng(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
 std::string digest(const unsigned char*data,size_t stride,size_t rowBytes,size_t rows,bool contiguous){
  check(stride>=rowBytes&&(!contiguous||stride==rowBytes),"contiguous requires packed rows");
  if(contiguous){check(rowBytes*rows<=std::numeric_limits<ULONG>::max(),"CNG input bound");if(rowBytes*rows)status(BCryptHashData(hash,const_cast<PUCHAR>(data),static_cast<ULONG>(rowBytes*rows),0),"hash contiguous");}
  else for(size_t y=0;y<rows;++y)if(rowBytes)status(BCryptHashData(hash,const_cast<PUCHAR>(data+y*stride),static_cast<ULONG>(rowBytes),0),"hash row");
  std::array<unsigned char,32> digest{};status(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0),"finish/reset hash");return hex(digest.data(),digest.size());
 }
};
// Exactly the current decoder fixture's per-frame construction/row-update/string/free pattern.
static std::string glibRows(const unsigned char*data,size_t stride,size_t rowBytes,size_t rows){auto*c=g_checksum_new(G_CHECKSUM_SHA256);check(c!=nullptr,"GLib checksum allocation");for(size_t y=0;y<rows;++y)g_checksum_update(c,data+y*stride,rowBytes);std::string result=g_checksum_get_string(c);g_checksum_free(c);return result;}
int main(int argc,char**argv){check(argc==3&&std::string(argv[1])=="--out","usage: fl_hash_compare --out NEW_DIRECTORY");namespace fs=std::filesystem;fs::path out=argv[2];check(fs::create_directory(out),"fresh output directory required");std::ofstream raw(out/"timings.csv");check(raw.good(),"open timings");raw<<"profile,method,iteration,elapsed_ns,digest\n";raw.flush();Cng cng;
 const unsigned char abc[]={'a','b','c'};const std::string empty="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";const std::string abcHash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
 for(unsigned repeat=0;repeat<2;++repeat){check(cng.digest(abc,0,0,0,true)==empty&&glibRows(abc,0,0,0)==empty,"empty known vector");check(cng.digest(abc,3,3,1,true)==abcHash&&cng.digest(abc,1,1,3,false)==abcHash&&glibRows(abc,1,1,3)==abcHash,"abc known vector/reusable reset");}
 constexpr unsigned iterations=12;unsigned records=0;std::ofstream summary(out/"summary.csv");summary<<"profile,method,active_bytes,pitch,iterations,p50_ms,min_ms,max_ms,digest\n";
 for(unsigned geometry=0;geometry<2;++geometry){const size_t width=geometry?3840:1920,height=geometry?2160:1080,rowBytes=width*4;
  std::string packedDigest;
  for(bool padded:{false,true}){const size_t stride=rowBytes+(padded?256:0);std::vector<unsigned char> data(stride*height,0xa5);
   for(size_t y=0;y<height;++y)for(size_t x=0;x<rowBytes;++x){const size_t i=y*rowBytes+x;data[y*stride+x]=static_cast<unsigned char>((i*131+(i>>8)*17+29)&255);}
   const std::string profile=std::string(geometry?"4k":"1080p")+(padded?"-padded":"-packed");const auto expected=glibRows(data.data(),stride,rowBytes,height);if(!padded)packedDigest=expected;else check(expected==packedDigest,"padding excluded from digest");
   check(cng.digest(data.data(),stride,rowBytes,height,false)==expected,"CNG row oracle");if(!padded)check(cng.digest(data.data(),stride,rowBytes,height,true)==expected,"CNG contiguous oracle");
   if(padded){for(size_t y=0;y<height;++y)std::fill(data.begin()+y*stride+rowBytes,data.begin()+(y+1)*stride,0x5a);check(glibRows(data.data(),stride,rowBytes,height)==expected&&cng.digest(data.data(),stride,rowBytes,height,false)==expected,"changed padding must not change digest");}
   const unsigned methods=padded?2:3;const char*names[]={"glib-rows","cng-rows-reusable","cng-packed-reusable"};std::vector<double> samples[3];
   // Rotate order each iteration, compare identical warm in-memory bytes, no copies in interval.
   for(unsigned iteration=0;iteration<iterations;++iteration)for(unsigned offset=0;offset<methods;++offset){unsigned method=(iteration+offset)%methods;auto begin=std::chrono::steady_clock::now();auto actual=method==0?glibRows(data.data(),stride,rowBytes,height):cng.digest(data.data(),stride,rowBytes,height,method==2);auto end=std::chrono::steady_clock::now();auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();check(actual==expected,"every full-frame digest must match");samples[method].push_back(ns/1e6);raw<<profile<<','<<names[method]<<','<<iteration<<','<<ns<<','<<actual<<'\n';raw.flush();check(raw.good(),"timing output");++records;}
   for(unsigned method=0;method<methods;++method){auto&s=samples[method];std::sort(s.begin(),s.end());double median=(s[5]+s[6])/2;summary<<profile<<','<<names[method]<<','<<rowBytes*height<<','<<stride<<','<<iterations<<','<<median<<','<<s.front()<<','<<s.back()<<','<<expected<<'\n';std::cout<<profile<<' '<<names[method]<<" p50_ms="<<median<<" min_ms="<<s.front()<<" max_ms="<<s.back()<<'\n';}
   summary.flush();check(summary.good(),"summary output");
  }
 }
 check(records==120,"expected bounded timing sample count");std::ofstream result(out/"result.json");result<<"{\"pass\":true,\"known_vectors\":true,\"padding_excluded\":true,\"iterations\":12,\"timing_records\":120,\"digest_bits\":256}\n";check(result.good(),"result output");return 0;
}

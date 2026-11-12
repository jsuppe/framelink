#pragma once
// Test-oracle implementation only. Hash every active byte, excluding row padding.
// One instance belongs to one thread; reusable CNG state resets after FinishHash.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <limits>
#include <string>
#include <vector>

class WinSha256Rows {
 public:
  WinSha256Rows() {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_HASH_REUSABLE_FLAG) < 0) return;
    DWORD length=0, bytes=0, digestLength=0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&length), sizeof length, &bytes, 0) < 0 ||
        bytes!=sizeof length || !length) return;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&digestLength), sizeof digestLength, &bytes, 0) < 0 ||
        bytes!=sizeof digestLength || digestLength!=32) return;
    storage.resize(length);
    ready=BCryptCreateHash(algorithm, &hash, storage.data(), length, nullptr, 0,
                           BCRYPT_HASH_REUSABLE_FLAG)>=0;
  }
  ~WinSha256Rows() {
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
  }
  WinSha256Rows(const WinSha256Rows&)=delete;
  WinSha256Rows& operator=(const WinSha256Rows&)=delete;

  // Empty return means invalid layout or CNG failure, never a digest to accept.
  std::string digest(const unsigned char* data, size_t stride,
                     size_t rowBytes, size_t rows) {
    if(!ready || stride<rowBytes || rowBytes>(std::numeric_limits<ULONG>::max)() ||
       (rowBytes && rows && !data) ||
       (rows>1 && stride>((std::numeric_limits<size_t>::max)()-rowBytes)/(rows-1)))
      return {};
    if(rowBytes && rows) {
      if(stride==rowBytes && rows<=(std::numeric_limits<ULONG>::max)()/rowBytes) {
        if(!update(data,static_cast<ULONG>(rowBytes*rows))) return {};
      } else {
        for(size_t y=0;y<rows;++y)
          if(!update(data+y*stride,static_cast<ULONG>(rowBytes))) return {};
      }
    }
    std::array<unsigned char,32> output{};
    if(BCryptFinishHash(hash,output.data(),static_cast<ULONG>(output.size()),0)<0) {
      ready=false; return {};
    }
    static constexpr char digits[]="0123456789abcdef";
    std::string result; result.reserve(64);
    for(auto byte:output) { result+=digits[byte>>4]; result+=digits[byte&15]; }
    return result;
  }
 private:
  bool update(const unsigned char* data, ULONG size) {
    if(BCryptHashData(hash,const_cast<PUCHAR>(data),size,0)>=0) return true;
    ready=false; return false;
  }
  BCRYPT_ALG_HANDLE algorithm=nullptr;
  BCRYPT_HASH_HANDLE hash=nullptr;
  std::vector<unsigned char> storage;
  bool ready=false;
};

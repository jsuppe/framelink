#include "framelink.h"
#include <cstdio>
#include <windows.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "line %d: %s\n", __LINE__, #x);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main() {
  char name[80];
  snprintf(name, sizeof(name), "disconnect-full-%lu", GetCurrentProcessId());
  flChannel *c = nullptr, *p = nullptr;
  CHECK(flCreateChannel(name, 64, 32, FL_FORMAT_BGRA8, 1, &c) == FL_OK);
  for (int n = 0; n < 100; n++) {
    if (flOpenProducer(name, &p) == FL_OK)
      break;
    Sleep(10);
  }
  CHECK(p);
  flBuffer b{};
  CHECK(flAcquireBuffer(p, &b, 100) == FL_OK);
  CHECK(flSubmit(p, &b, 0) == FL_OK);
  // Consumer never acquires/releases the only occupied slot. Its exit must
  // still be observable by the producer, independently of the consumed fence.
  flClose(c);
  CHECK(flAcquireBuffer(p, &b, 500) == FL_DISCONNECTED);
  flClose(p);
  puts("full ring detects consumer disconnect without fence progress");
}

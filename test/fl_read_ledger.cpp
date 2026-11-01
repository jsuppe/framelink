#include "fl_read_ledger.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
static flFrame frame(unsigned slot, uint64_t v) {
    flFrame f{}; f.slot=slot; f.image=reinterpret_cast<flImage*>(uintptr_t(slot+1));
    f._readyValue=v; f.sequence=v; f.ptsNs=int64_t(v)*1000; return f;
}
int main() {
    fl::ReadLedger a,b; uint64_t x=0,y=0,z=0,w=0;
    CHECK(a.acquire(1,frame(0,1),x)==FL_OK);
    CHECK(a.retain(x,y)==FL_OK);
    CHECK(a.acquire(1,frame(1,3),z)==FL_OK); // Latest-wins may skip value 2.
    CHECK(a.complete(z)==FL_OK && a.retirement().value==0);
    CHECK(a.complete(x)==FL_OK && a.retirement().value==0);
    CHECK(a.complete(x)==FL_INVALID && a.retain(x,w)==FL_INVALID);
    CHECK(b.complete(y)==FL_INVALID); // foreign-channel token
    CHECK(a.acquire(2,frame(2,1),w)==FL_BUSY); // generation cannot replace live storage
    CHECK(a.acquire(1,frame(0,4),w)==FL_BUSY); // same allocation still retained
    flFrame got{}; uint64_t gen=0;
    CHECK(a.get(y,got,gen)==FL_OK && gen==1 && got.ptsNs==1000);
    CHECK(a.complete(y)==FL_OK && a.retirement().value==3);
    CHECK(a.retired({2,3})==FL_INVALID && a.size()==2);
    CHECK(a.retired({1,1})==FL_INVALID && a.size()==2);
    CHECK(a.retirement().value==3); // failed signal/ack remains retryable
    CHECK(a.retired({1,3})==FL_OK && a.empty());
    CHECK(a.acquire(1,frame(0,3),w)==FL_INVALID); // monotonic within generation
    CHECK(a.acquire(2,frame(0,1),w)==FL_OK && w!=x && w!=y);
    CHECK(a.complete(x)==FL_INVALID && a.complete(y)==FL_INVALID);
    CHECK(a.complete(w)==FL_OK && a.retired(a.retirement())==FL_OK);
    uint64_t leases[fl::ReadLedger::MaxReaders]{};
    CHECK(a.acquire(2,frame(0,2),leases[0])==FL_OK);
    for(size_t i=1;i<fl::ReadLedger::MaxReaders;++i) CHECK(a.retain(leases[0],leases[i])==FL_OK);
    w=987; CHECK(a.retain(leases[0],w)==FL_BUSY && w==987);
    CHECK(a.acquire(2,frame(1,3),w)==FL_BUSY && w==987 && a.size()==1);
    for(auto t:leases) CHECK(a.complete(t)==FL_OK);
    CHECK(a.retired(a.retirement())==FL_OK);
    uint64_t slots[FL_MAX_POOL_SIZE]{};
    for(unsigned i=0;i<FL_MAX_POOL_SIZE;++i) CHECK(a.acquire(3,frame(i,i+1),slots[i])==FL_OK);
    CHECK(a.acquire(3,frame(0,99),w)==FL_BUSY);
    for(unsigned i=FL_MAX_POOL_SIZE;i>0;--i) {
        CHECK(a.complete(slots[i-1])==FL_OK);
        CHECK(a.retirement().value==(i==1?FL_MAX_POOL_SIZE:0));
    }
    CHECK(a.retired(a.retirement())==FL_OK && a.empty());
    // Repeated wrap of fixed ring storage cannot revive stale tokens.
    for(unsigned i=0;i<10000;++i) {
        CHECK(a.acquire(4,frame(i%FL_MAX_POOL_SIZE,i+1),w)==FL_OK);
        CHECK(a.complete(slots[i%FL_MAX_POOL_SIZE])==FL_INVALID);
        CHECK(a.complete(w)==FL_OK && a.retired(a.retirement())==FL_OK);
    }
    CHECK(a.acquire(5,frame(0,1),w)==FL_OK && a.defer(w)==FL_OK);
    CHECK(a.defer(w)==FL_INVALID && a.complete(w)==FL_INVALID);
    CHECK(a.retain(w,x)==FL_INVALID && a.retirement().value==0);
    CHECK(a.finishDeferred(w)==FL_OK && a.finishDeferred(w)==FL_INVALID);
    CHECK(a.retired(a.retirement())==FL_OK);
    std::puts("read ledger: bounded ownership, out-of-order completion and stale-token checks passed");
}

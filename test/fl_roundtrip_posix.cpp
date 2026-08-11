// framelink POSIX round trip: one binary, both roles.
//
//   fl_roundtrip consume <channel> <frames>
//   fl_roundtrip produce <channel> <frames>
//
// Same verification as the Windows pair: the producer derives each frame's
// colour from its frame number and sends that number as the pts; the consumer
// recomputes the expected colour from the pts it receives. A mismatch means a
// frame's pixels were paired with another frame's metadata - slot aliasing, or
// reading a buffer that is still being written. "Some pixels arrived" would
// catch neither.
//
// Pixels are touched through flImageMap (FL_MAP_CPU), which is the path a
// v4l2loopback camera needs anyway. A GPU producer would import the dma-buf
// from flImageDmabuf instead and never map anything.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framelink.h"
#include "framelink_dmabuf.h"

namespace {

void colourFor(int i, unsigned char* b, unsigned char* g, unsigned char* r) {
    *b = (unsigned char)((i * 13) & 0xFF);
    *g = (unsigned char)((i * 7) & 0xFF);
    *r = (unsigned char)(i & 0xFF);
}

int runConsumer(const char* name, int want) {
    const uint32_t W = 256, H = 128;
    flChannel* ch = nullptr;
    flResult r = flCreateChannelEx(name, W, H, FL_FORMAT_BGRA8, 4, FL_MAP_CPU, &ch);
    if (r != FL_OK) {
        printf("consume: flCreateChannelEx -> %s\n", flResultString(r));
        return 1;
    }
    printf("consume: channel '%s' %ux%u pool 4 (dma-buf, CPU-mappable)\n", name, W, H);

    int verified = 0, mismatched = 0;
    uint64_t lastSeq = 0, skipped = 0;
    for (int guard = 0; verified + mismatched < want && guard < 4000; ++guard) {
        flFrame f{};
        r = flAcquireFrame(ch, &f, 250);
        if (r == FL_TIMEOUT) continue;
        if (r == FL_DISCONNECTED) {
            printf("consume: producer disconnected\n");
            break;
        }
        if (r != FL_OK) {
            printf("consume: flAcquireFrame -> %s\n", flResultString(r));
            break;
        }
        if (lastSeq && f.sequence > lastSeq + 1) skipped += f.sequence - lastSeq - 1;
        lastSeq = f.sequence;

        flChannelInfo now{};
        flQuery(ch, &now);
        if (now.width != W || now.height != H) {
            // Only report the transition once; the point is that the consumer
            // sees the producer's requested geometry take effect.
            static bool said = false;
            if (!said) {
                printf("consume: ring is now %ux%u (producer requested it)\n", now.width,
                       now.height);
                said = true;
            }
        }
        void* px = nullptr;
        uint64_t stride = 0;
        if (flImageMap(f.image, &px, &stride) == FL_OK && px) {
            const unsigned char* p = (const unsigned char*)px;
            unsigned char wb, wg, wr;
            colourFor((int)f.ptsNs, &wb, &wg, &wr);
            if (p[0] == wb && p[1] == wg && p[2] == wr) {
                ++verified;
            } else {
                ++mismatched;
                printf("consume: MISMATCH pts=%lld slot=%u got %u,%u,%u want %u,%u,%u\n",
                       (long long)f.ptsNs, f.slot, p[0], p[1], p[2], wb, wg, wr);
            }
        }
        flRelease(ch, &f);
    }
    printf("consume: %d verified, %d mismatched, %llu skipped\n", verified, mismatched,
           (unsigned long long)skipped);
    flClose(ch);
    const bool pass = verified > 0 && mismatched == 0;
    printf("consume: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int runProducer(const char* name, int frames) {
    flChannel* ch = nullptr;
    flResult r = FL_NOT_FOUND;
    // The consumer owns the channel and may not exist yet; keep asking.
    for (int attempt = 0; attempt < 100 && r == FL_NOT_FOUND; ++attempt) {
        r = flOpenProducer(name, &ch);
        if (r == FL_NOT_FOUND) usleep(100000);
    }
    if (r != FL_OK) {
        printf("produce: flOpenProducer -> %s\n", flResultString(r));
        return 1;
    }
    flChannelInfo info{};
    flQuery(ch, &info);
    printf("produce: attached to '%s' %ux%u pool %u\n", name, info.width, info.height,
           info.poolSize);

    // Ask for a size the consumer did NOT create. This is the case that
    // matters: real producers differ and cannot be predicted - a mirroring
    // iPad sends 752x1080 into the same slot a laptop uses for 1920x1080.
    // flQuery afterwards is the truth, because the consumer may decline.
    const uint64_t genBefore = info.generation;
    r = flRequestGeometry(ch, 320, 240, FL_FORMAT_BGRA8);
    printf("produce: flRequestGeometry(320x240) -> %s\n", flResultString(r));
    flQuery(ch, &info);
    printf("produce: ring is now %ux%u pool %u gen %llu\n", info.width, info.height,
           info.poolSize, (unsigned long long)info.generation);
    if (info.width != 320 || info.height != 240 || info.generation == genBefore) {
        printf("produce: FAIL - reallocation did not take\n");
        flClose(ch);
        return 1;
    }

    int submitted = 0;
    for (int i = 1; i <= frames; ++i) {
        flBuffer b{};
        if (flAcquireBuffer(ch, &b, 1000) != FL_OK) break;
        void* px = nullptr;
        uint64_t stride = 0;
        if (flImageMap(b.image, &px, &stride) == FL_OK && px) {
            unsigned char bb, gg, rr;
            colourFor(i, &bb, &gg, &rr);
            unsigned char* p = (unsigned char*)px;
            for (uint32_t y = 0; y < info.height; ++y) {
                unsigned char* row = p + (size_t)y * stride;
                for (uint32_t x = 0; x < info.width; ++x) {
                    row[x * 4 + 0] = bb;
                    row[x * 4 + 1] = gg;
                    row[x * 4 + 2] = rr;
                    row[x * 4 + 3] = 255;
                }
            }
        }
        if (flSubmit(ch, &b, (int64_t)i) != FL_OK) break;
        ++submitted;
        usleep(8000);
    }
    printf("produce: submitted %d frames\n", submitted);
    flClose(ch);
    return submitted > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        printf("usage: %s consume|produce [channel] [frames]\n", argv[0]);
        return 2;
    }
    const char* name = argc > 2 ? argv[2] : "flrt";
    const int n = argc > 3 ? atoi(argv[3]) : 20;
    if (!strcmp(argv[1], "consume")) return runConsumer(name, n);
    if (!strcmp(argv[1], "produce")) return runProducer(name, n);
    printf("unknown role '%s'\n", argv[1]);
    return 2;
}

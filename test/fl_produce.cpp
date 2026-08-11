// framelink spike producer: attach, draw a frame-derived colour, submit.
//
// The colour is derived from the frame number, and the SAME number is sent as
// the pts. The consumer recomputes the colour from the pts it receives, so the
// test fails if a frame's pixels are ever paired with another frame's metadata
// - i.e. it catches slot aliasing and missing fence waits, not merely "some
// pixels arrived".
//
//   fl_produce [channel] [frames] [sleepMs] [reattach]
//
// reattach=1 splits the run in half and detaches/reattaches in between, which
// is what a receiver does every time one presenter leaves and the next takes
// the slot.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>

#include "framelink.h"
#include "framelink_d3d11.h"

using Microsoft::WRL::ComPtr;

namespace {

// Attach with retries. A producer never creates a channel, so if the consumer
// has not started yet the honest answer is FL_NOT_FOUND - and the useful
// response is to keep asking rather than exit. Real producers want the same:
// vkrender's renderer retries once a second, indefinitely.
flChannel* attach(const char* name) {
    flChannel* ch = nullptr;
    flResult r = FL_NOT_FOUND;
    for (int attempt = 0; attempt < 100 && r == FL_NOT_FOUND; ++attempt) {
        r = flOpenProducer(name, &ch);
        if (r == FL_NOT_FOUND) Sleep(100);
    }
    if (r != FL_OK) {
        printf("produce: flOpenProducer('%s') -> %s\n", name, flResultString(r));
        return nullptr;
    }
    return ch;
}

// Returns the number submitted; `first`/`last` are inclusive pts values, and
// the pts doubles as the frame number the colour is derived from.
int run(flChannel* ch, uint32_t w, uint32_t h, int first, int last, int sleepMs, int* stalls) {
    ID3D11Device* dev = flChannelD3D11Device(ch);
    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    int submitted = 0;
    for (int i = first; i <= last; ++i) {
        flBuffer buf{};
        const DWORD ta = GetTickCount();
        flResult r = flAcquireBuffer(ch, &buf, 2000);
        // A wait here IS the back-pressure: the ring is full because the
        // consumer has not released anything yet.
        if (GetTickCount() - ta > 5) ++*stalls;
        if (r != FL_OK) {
            printf("produce: flAcquireBuffer -> %s\n", flResultString(r));
            break;
        }
        // Draw on the GPU rather than uploading from the CPU: this is what
        // makes the fence wait meaningful on the other side.
        ComPtr<ID3D11RenderTargetView> rtv;
        if (FAILED(dev->CreateRenderTargetView(flImageD3D11(buf.image), nullptr, &rtv))) {
            printf("produce: CreateRenderTargetView failed\n");
            break;
        }
        const float rgba[4] = {(float)(i & 0xFF) / 255.0f, (float)((i * 7) & 0xFF) / 255.0f,
                               (float)((i * 13) & 0xFF) / 255.0f, 1.0f};
        ctx->ClearRenderTargetView(rtv.Get(), rgba);

        r = flSubmit(ch, &buf, (int64_t)i); // pts carries the frame number
        if (r != FL_OK) {
            printf("produce: flSubmit -> %s\n", flResultString(r));
            break;
        }
        ++submitted;
        if (sleepMs) Sleep(sleepMs);
    }
    (void)w;
    (void)h;
    return submitted;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* name = argc > 1 ? argv[1] : "flspike";
    const int frames = argc > 2 ? atoi(argv[2]) : 60;
    const int sleepMs = argc > 3 ? atoi(argv[3]) : 8; // 0 = run flat out
    const bool reattach = argc > 4 && atoi(argv[4]) != 0;

    flChannel* ch = attach(name);
    if (!ch) return 1;

    flChannelInfo info{};
    flQuery(ch, &info);
    printf("produce: attached to '%s' %ux%u pool %u gen %llu (geometry is the consumer's)\n",
           name, info.width, info.height, info.poolSize,
           (unsigned long long)info.generation);

    // Ask for a size the consumer did NOT create. This is the case that
    // matters: real producers differ and cannot be predicted - a mirroring
    // iPad sends 752x1080 into the same slot a laptop uses for 1920x1080.
    // flQuery afterwards is the truth, because the consumer may decline.
    const uint64_t genBefore = info.generation;
    flResult r = flRequestGeometry(ch, 320, 240, FL_FORMAT_BGRA8);
    flQuery(ch, &info);
    printf("produce: flRequestGeometry(320x240) -> %s, ring is now %ux%u gen %llu\n",
           flResultString(r), info.width, info.height, (unsigned long long)info.generation);
    if (info.width != 320 || info.height != 240 || info.generation == genBefore) {
        printf("produce: FAIL - reallocation did not take\n");
        flClose(ch);
        return 1;
    }

    int stalls = 0;
    const DWORD t0 = GetTickCount();
    const int half = reattach ? frames / 2 : frames;
    int submitted = run(ch, info.width, info.height, 1, half, sleepMs, &stalls);

    if (reattach) {
        // Detach and come back, the way a receiver does when one presenter
        // leaves and another takes the slot. The channel outlives us: it
        // belongs to the consumer.
        const uint64_t gen1 = info.generation;
        flClose(ch);
        ch = attach(name);
        if (!ch) return 1;
        flQuery(ch, &info);
        printf("produce: reattached, ring is %ux%u gen %llu\n", info.width, info.height,
               (unsigned long long)info.generation);
        // The generation MUST have moved: a new producer means a fresh fence
        // pair, because our submit counter restarts at 1 and a fence still
        // sitting at the last producer's value would let every wait pass
        // instantly. A consumer that imported those fences has to re-import.
        if (info.generation == gen1) {
            printf("produce: FAIL - reattach did not reset the channel's sync\n");
            flClose(ch);
            return 1;
        }
        submitted += run(ch, info.width, info.height, half + 1, frames, sleepMs, &stalls);
    }

    const DWORD ms = GetTickCount() - t0;
    printf("produce: submitted %d frames in %lu ms (%.1f fps), %d stalls waiting for a buffer\n",
           submitted, ms, ms ? submitted * 1000.0 / ms : 0.0, stalls);
    flClose(ch);
    return submitted > 0 ? 0 : 1;
}

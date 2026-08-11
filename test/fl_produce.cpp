// framelink spike producer: attach, draw a frame-derived colour, submit.
//
// The colour is derived from the frame number, and the SAME number is sent as
// the pts. The consumer recomputes the colour from the pts it receives, so the
// test fails if a frame's pixels are ever paired with another frame's metadata
// - i.e. it catches slot aliasing and missing fence waits, not merely "some
// pixels arrived".
//
//   fl_produce [channel] [frames]

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

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* name = argc > 1 ? argv[1] : "flspike";
    const int frames = argc > 2 ? atoi(argv[2]) : 60;
    const int sleepMs = argc > 3 ? atoi(argv[3]) : 8; // 0 = run flat out

    // Attach with retries. A producer never creates a channel, so if the
    // consumer has not started yet the honest answer is FL_NOT_FOUND - and the
    // useful response is to keep asking rather than exit. Real producers want
    // the same: vkrender's renderer retries once a second, indefinitely.
    flChannel* ch = nullptr;
    flResult r = FL_NOT_FOUND;
    for (int attempt = 0; attempt < 100 && r == FL_NOT_FOUND; ++attempt) {
        r = flOpenProducer(name, &ch);
        if (r == FL_NOT_FOUND) Sleep(100);
    }
    if (r != FL_OK) {
        printf("produce: flOpenProducer('%s') -> %s\n", name, flResultString(r));
        return 1;
    }
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
    r = flRequestGeometry(ch, 320, 240, FL_FORMAT_BGRA8);
    flQuery(ch, &info);
    printf("produce: flRequestGeometry(320x240) -> %s, ring is now %ux%u gen %llu\n",
           flResultString(r), info.width, info.height, (unsigned long long)info.generation);
    if (info.width != 320 || info.height != 240 || info.generation == genBefore) {
        printf("produce: FAIL - reallocation did not take\n");
        flClose(ch);
        return 1;
    }

    ID3D11Device* dev = flChannelD3D11Device(ch);
    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);

    int submitted = 0, stalls = 0;
    const DWORD t0 = GetTickCount();
    for (int i = 1; i <= frames; ++i) {
        flBuffer buf{};
        const DWORD ta = GetTickCount();
        r = flAcquireBuffer(ch, &buf, 2000);
        // A wait here IS the back-pressure: the ring is full because the
        // consumer has not released anything yet.
        if (GetTickCount() - ta > 5) ++stalls;
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
    const DWORD ms = GetTickCount() - t0;
    printf("produce: submitted %d frames in %lu ms (%.1f fps), %d stalls waiting for a buffer\n",
           submitted, ms, ms ? submitted * 1000.0 / ms : 0.0, stalls);
    flClose(ch);
    return submitted > 0 ? 0 : 1;
}

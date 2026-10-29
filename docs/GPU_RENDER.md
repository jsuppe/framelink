# GPU-renderable channel allocation

`FL_GPU_RENDER` (0x8) is an opt-in `flCreateChannelEx` flag. It is independent of
`FL_GPU_SYNC` (consumer synchronization ownership) and `FL_MAP_CPU` (CPU mapping).
The consumer sample accepts `--gpu-render 1`.

On Android it requests `AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT` in addition to
sampling usage. `AHardwareBuffer_isSupported` checks the complete format/usage
combination first; unsupported combinations return `FL_FORMAT_UNSUPPORTED`.
There is no silent downgrade to CPU-only/native nonrenderable YUV storage.
Without the flag, existing allocation behavior is unchanged.

Desktop rings already request GPU-capable native storage; the flag does not
promise that every Vulkan format/modifier/usage import is supported. Producers
must still query their API/device capabilities. In particular, NV12 is not a
normal Vulkan color attachment. vkrender's Linux output uses compute conversion
and plane copies; Android YUV output uses external-format resolve where supported.

Verified with renderer GPU pixel/lifecycle tests: Linux RTX3090 and Android Mali
BGRA/RGBA/NV12/RGB10A2; Pi Android BGRA/RGBA. Pi NV12 renderable allocation and
RGB10A2 allocation are unsupported. A consumer can explicitly choose the existing
CPU-compatible NV12 path without this flag.

Android NV12 retains native YUV420 AHB storage plus a normalized NV12 CPU view;
a GPU consumer should import the AHB, not assume its native byte layout is NV12.
The renderer's GPU output performs no CPU pixel mapping or conversion. A CPU
consumer mapping those frames can still incur normalization/readback costs.

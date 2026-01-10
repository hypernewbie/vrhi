# Vrhi - Immediate Mode Vulkan RHI Interface For NVRHI

[![build](https://github.com/hypernewbie/vrhi/actions/workflows/ci.yml/badge.svg)](https://github.com/hypernewbie/vrhi/actions/workflows/ci.yml)
Vrhi is a Vulkan RHI interface for cross-platform graphics and compute rendering. It is inspired by the bgfx library, powered by the NVRHI library. It is currently in development and is not yet feature complete.

## Prerequisites

- **Vulkan SDK**: Install from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home) (Tested on version `1.4.335.0`).
- **MSVC**: Visual Studio 2022 or newer (uses C++23).
- **Python 3**: Python 3.12 or newer (for code generation).

## Build

Vrhi uses CMake for its build system. On Windows with MSVC, you can build the project using the following commands:

```powershell
cmake -S . -B build
cmake --build build -j --config Debug
cmake --build build -j --config Release
```

After building, you can run the tests using `ctest` or by executing the test binary directly:

```powershell
# Run tests via CTest
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --verbose

# Or execute the binary directly
.\build\Debug\vrhi_test.exe
```

## FAQ

### Is Vrhi written by AI?

Yes Vrhi is AI slop, albeit closely directed and reviewed by an ex-graphics driver engineer and ex-Khronos member.
This is intended to be an educational statement: someone please write something better than I can with vibecoding AI.

### Why make AI slop?

Simple. Because human Vulkan slop is often worse.

The goal is to inspire you, the reader, to write a better RHI than this.

### How is it better than human Vulkan slop?

Because it actually has a lot of the fundamentals in place:

* Does not impose "invented concepts". An RHI is an interface to expose the GPU's capabilities, not someone's ( often flawed ) abstract mental model. It is an RHI, not an API.
* Not a "common denominator" type feature set. ( It's 2026 man, even phones have bindless and RT cores )
* Actually profiled and performance tested
* Transient staging buffers ( No, do not allocate separate driver resources every texture or buffer upload )
* Raytracing support ( Yes really, guys it's been a decade, get with the times )
* Cached descriptor sets ( Please don't create them every frame )
* Cached PSOs ( Please don't create them every frame )
* Cached frame buffer objects ( Please don't create them every frame )
* Binding location based vertex layouts ( It's 2026, semantics aren't really a thing anymore )
* Separate samplers ( Yes really, it is 2026 )
* State caching with dirty bits
* Buffer sub-allocation support
* Compressed texture formats support with correct mipmap + size calculation
* All texture types supported. 1D, 2D, 3D, Cube, CubeArray, 2DArray.
* All buffer types supported. Typed, Structured, Raw, Constant, VolatileConstant, PushConstants.
* Deferred resource free
* Threaded RHI backend
* Proper renderdoc / capture support
* Lightweight error checking with draw-time validation you can turn off for production
* Transparent logging / error with shader binding mismatch to make it easy to debug
* Test Driven Development
* Continuous Integration on Win / Linux / macOS

While it is easy to get a high-level Vulkan / DX12 RHI layer working, it takes strong fundamental understanding to get it equal or faster than DX9 / DX11.
Please write one better than this, dear reader. Please write one that is equal or faster than DX11. Then this repo wouldn't need to exist, which would be great.
Does your Vulkan RHI have these above? If not, please read the code and add it, and even better , release it so I can use your code.

Vulkan is a graphics API from this decade. So please abstract it using concepts from this decade, using development practices from this decade.

### Should I use this?

No, you should not.

You should write a new Vulkan RHI that is better.
Please don't write one that is worse.


### What's missing?

Lots:
* Bindless support
* VR support
* VRS
* Async Compute
* Async Transfer

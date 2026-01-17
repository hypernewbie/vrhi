# Vrhi - Immediate Mode Vulkan RHI Interface For NVRHI

[![build](https://github.com/hypernewbie/vrhi/actions/workflows/ci.yml/badge.svg)](https://github.com/hypernewbie/vrhi/actions/workflows/ci.yml)

Vrhi is a high level Vulkan RHI interface for cross-platform graphics and compute rendering. It is inspired by the bgfx library, powered by the NVRHI library.
It aims to provide a DX11 / DX9 style immediate mode API on top of Vulkan, in a way that is not terribly slow.

It is currently in development and is not yet feature complete.

## Prerequisites

- **Vulkan SDK**: Install from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home) (Tested on version `1.4.335.0`).
- **CMake**: CMake 3.22 or newer.
- **Python 3**: Python 3.12 or newer.
- **Windows**: Visual Studio 2022 or newer (MSVC C++23).
- **Linux**: Clang 21 (`libc++-21-dev`, `libc++abi-21-dev`), Ninja, X11 libraries (`libx11-dev`, `libxrandr-dev`, `libxi-dev`, `libxcursor-dev`, `libxinerama-dev`, `libxext-dev`), `mesa-vulkan-drivers`.
- **macOS**: Clang 17 (`brew install llvm@17`), Ninja.

## Build

Vrhi uses CMake for its build system.

On Windows with MSVC, you can build the project using the following commands:
```powershell
cmake -S . -B build
cmake --build build -j --config Debug
cmake --build build -j --config Release
```

On Mac / Linux, it is the same but using Ninja:
```powershell
cmake -S . -B build -G Ninja
cmake --build build -j --config Debug
cmake --build build -j --config Release
```

After building, you can run the tests using `ctest` or by executing the test binary directly:

```powershell
# Run tests via CTest
ctest --test-dir build -C Debug --verbose
ctest --test-dir build -C Release --output-on-failure

# Or execute the binary directly
.\build\Debug\vrhi_test.exe
```

## Packages

VRHI provides self-contained release packages for easy integration into your projects.

Just add `vrhi_release/include` to your include path and link against:
- `vrhi_release/lib/vrhi.lib`
- `vrhi_release/lib/nvrhi.lib`
- `vrhi_release/lib/nvrhi_vk.lib`
- `vrhi_release/lib/vk-bootstrap.lib`
- `vrhi_release/lib/rtxmu.lib`
- And Vulkan libraries from SDK

You'll still need:
- Vulkan SDK (https://vulkan.lunarg.com/sdk/home)
- C++23 compiler
- Static C runtime (/MT for MSVC) (rebuild yourself if you want dynamic)

### Building Your Own Package

```powershell
# Debug package (includes .pdb files)
cmake --build build --config Debug --target package_vrhi

# Release package (no debug symbols)
cmake --build build --config Release --target package_vrhi
```

This creates:
- `build/vrhi_Debug/` - Debug package with symbols
- `build/vrhi_Release/` - Release package

### Testing Your Package

Validate that your package is complete and functional:

```powershell
# Test Debug package (validates headers, libraries, basic functionality)
cmake --build build --config Debug --target test_package_vrhi

# Test Release package
cmake --build build --config Release --target test_package_vrhi
```

The `test_package_vrhi` target will:
- Configure and build a standalone test using the packaged VRHI
- Run tests for initialisation, resource creation, and cleanup
- **FAIL the build** if tests fail (ensures package integrity)
- Report specific failure reasons (init, buffer, texture, shader)

### CMake Integration

```cmake
# Manual integration
target_include_directories(your_app PRIVATE path/to/vrhi_release/include)
target_link_directories(your_app PRIVATE path/to/vrhi_release/lib)
target_link_libraries(your_app
    vrhi
    nvrhi_vk
    nvrhi
    vk-bootstrap
    rtxmu
    Vulkan::Vulkan
)

# Use CMake's find_package (not supported yet)
find_package(vrhi REQUIRED)
target_link_libraries(your_app vrhi::vrhi)
```

## FAQ

### Is Vrhi written by AI?

Yes, Vrhi is AI slop, albeit closely directed and reviewed by a former graphics driver engineer and ex-Khronos member.
This is intended as an educational statement: someone please write something better than I can with vibecoding AI.

### Why make AI slop?

Simple. Because human Vulkan slop is often worse.

The goal is to inspire you, the reader, to write a better RHI than this.

### How is it better than human Vulkan slop?

Because it actually has a lot of the fundamentals in place:

* Does not impose "invented concepts". An RHI is an interface to expose the GPU's capabilities, not someone's (often flawed) abstract mental model.
* It is an RHI, not an API. The `RH` (Rendering Hardware) has already defined the `I` (Interface), and thus NVIDIA and AMD have already dictated the problem statement.
* Not a "common denominator" type feature set. (It's 2026, man—even phones have bindless and RT cores.)
* Actually profiled and performance tested
* Transient staging buffers (no, do not allocate separate driver resources for every texture or buffer upload)
* Raytracing support (yes, really, guys, it's been a decade, get with the times)
* Cached descriptor sets (please don't create them every frame)
* Cached PSOs (please don't create them every frame)
* Cached frame buffer objects (please don't create them every frame)
* Binding location-based vertex layouts (it's 2026, semantics aren't really a thing anymore)
* Separate samplers (yes, really, it is 2026)
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

While it is easy to get a high-level Vulkan/DX12 RHI layer working, it takes strong fundamental understanding to get it equal to or faster than DX9/DX11.
Please write one better than this, dear reader. Please write one that is equal to or faster than DX11. Then this repo wouldn't need to exist, which would be great.
Does your Vulkan RHI have the above? If not, please read the code and add it, and even better, release it so I can use your code.

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
* Multithreaded Cmdbuf
* MSAA
* ASTC Formats
* Avoiding allocations. Right now it makes A LOT, which is bad.
* About 30 years of optimisation by a team of 5 Principal Engineers
* Benchmark cheating

### Why is Vulkan so annoying?

I know, I agree it's a mess, and as a former Khronos member, I was partially responsible. I'm sorry.
It's actually part of the reason I left my graphics driver engineering career years ago. I didn't want to work on a graphics API that I didn't love.

Part of the argument at the time was that someone would write an "immediate mode"-like library on top of Vulkan and open source it on GitHub, and it would be awesome and easy to use and transparent, and quickly approach DX11 performance as it matures, and become an awesome open source standard.

Fast forward 10 years later, where is it?
This library still doesn't exist. People are still raw-dogging Vulkan, often badly.

### License

It is MIT licensed, because MIT license makes you display the license if you use it, which forces people to publicly shame themselves for using this Vrhi AI slop of a library instead of writing a better RHI.

```
MIT License

Copyright 2026 UAA Software

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

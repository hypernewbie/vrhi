# Vrhi - Immediate Mode Vulkan RHI Interface For NVRHI

Vrhi is a Vulkan RHI interface for cross-platform graphics and compute rendering. It is inspired by the bgfx library, powered by the NVRHI library. It is currently in development and is not yet feature complete.

## Prerequisites

- **Vulkan SDK**: Install from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home) (Tested on version `1.4.335.0`).
- **MSVC**: Visual Studio 2022 or newer (uses C++23).
- **Python 3**: Python 3.12 or newer (for code generation).

## Build

Vrhi uses CMake for its build system. On Windows with MSVC, you can build the project using the following commands:

```powershell
cmake -S . -B build
cmake --build build --config Debug
cmake --build build --config Release
```

After building, you can run the tests using `ctest` or by executing the test binary directly:

```powershell
# Run tests via CTest
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --verbose

# Or execute the binary directly
.\build\Debug\vrhi_test.exe
```


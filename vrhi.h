/*
    -- Vrhi --

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
*/

#pragma once

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES // Define this if you have these in PCH already.
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

#include <nvrhi/vulkan.h>
#include "vrhi_defines.h"

#define VRHI_INVALID_HANDLE 0xFFFFFFFF
#define VRHI_MIPMAP_COMPLETE -1

struct vhInitData
{
    std::string appName = "VRHI_APP";
    bool debug = false;
    int deviceIndex = -1; // -1 for auto-selection (Discrete > Integrated > CPU)
    glm::ivec2 resolution = glm::ivec2( 1280, 720 );
    std::function<void( bool error, const std::string& )> fnLogCallback = nullptr;
};

typedef uint32_t vhTexture;
extern vhInitData g_vhInit;
extern nvrhi::DeviceHandle g_vhDevice;
extern std::atomic<int32_t> g_vhErrorCounter;

// --------------------------------------------------------------------------
// Interface
// --------------------------------------------------------------------------

// Manually Regenerate this with py vidl.py vrhi.h vrhi_generated.h.
// Cmake should automatically do this already.

// ------------ Device ------------

void vhInit();

void vhShutdown();

std::string vhGetDeviceInfo();

void vhFlush();
void vhFinish();

// ------------ Texture ------------

vhTexture vhAllocTexture();

struct vhFormatInfo
{
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    const char* name = nullptr;
    int32_t elementSize = 0;
    int32_t compressionBlockWidth = 0;
    int32_t compressionBlockHeight = 0;
};

inline vhFormatInfo vhGetFormat( nvrhi::Format format )
{
    const nvrhi::FormatInfo& info = nvrhi::getFormatInfo( format );
    vhFormatInfo out =
    {
        .format = format,
        .name = info.name,
        .elementSize = ( int32_t ) info.bytesPerBlock,
        .compressionBlockWidth = ( int32_t ) info.blockSize,
        .compressionBlockHeight = ( int32_t ) info.blockSize
    };
    return out;
}

// VIDL_GENERATE
void vhDestroyTexture( vhTexture texture );

// VIDL_GENERATE
void vhCreateTexture(
    vhTexture texture,
    nvrhi::TextureDimension target,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
);

inline void vhCreateTexture2D(
    vhTexture texture,
    glm::ivec2 dimensions,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::Texture2D, glm::ivec3( dimensions, 1 ), numMips, 1, format, flag, data );
}

inline void vhCreateTexture3D(
    vhTexture texture,
    glm::ivec3 dimensions,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::Texture3D, dimensions, numMips, 1, format, flag, data );
}

inline void vhCreateTextureCube(
    vhTexture texture,
    int dimension,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::TextureCube, glm::ivec3( dimension, dimension, 1 ), numMips, 6, format, flag, data );
}

inline void vhCreateTexture2DArray(
    vhTexture texture,
    glm::ivec2 dimensions,
    int numLayers,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::Texture2DArray, glm::ivec3( dimensions, 1 ), numMips, numLayers, format, flag, data );
}

inline void vhCreateTextureCubeArray(
    vhTexture texture,
    int dimension,
    int numLayers,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const std::vector<uint8_t> *data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::TextureCubeArray, glm::ivec3( dimension, dimension, 1 ), numMips, numLayers, format, flag, data );
}

// --------------------------------------------------------------------------
// Implementation
// --------------------------------------------------------------------------

#ifdef VRHI_IMPLEMENTATION

// VIDL_GENERATE
void vhFlushInternal( std::atomic<bool>* fence, bool waitForGPU = false );

// In header-only mode, we want definitions.
#define VRHI_IMPL_DEFINITIONS
#include "vrhi_impl.h"

#endif // VRHI_IMPLEMENTATION

/*
 * ============================================================================
 * VRHI Build Architecture
 * ============================================================================
 *
 * UNITY/BUILD ( Header-Only Library )
 * ----------------------------------
 * Single translation unit compiles everything via VRHI_IMPLEMENTATION.
 *
 *   test.cpp
 *       |
 *       | #define VRHI_IMPLEMENTATION
 *       v
 *   vrhi.h ──────────────────────────────────────────┐
 *       |                                            |
 *       | #define VRHI_IMPL_DEFINITIONS              |
 *       v                                            v
 *   vrhi_impl.h ─────────────> [globals defined]     |
 *       |                                            |
 *       | #ifdef VRHI_IMPLEMENTATION                 |
 *       +───────────────────────────┬────────────────+
 *       v                           v                v
 *   vrhi_impl_backend.h    vrhi_impl_device.h    vrhi_impl_texture.h
 *
 *
 * SHARDED BUILD ( Traditional Library )
 * ---------------------
 * Multiple translation units for faster incremental builds.
 * Touching one module only recompiles that shard.
 *
 *   test.cpp                        test_impl_definitions.cpp
 *       |                                   |
 *       | (no VRHI_IMPLEMENTATION)          | #define VRHI_IMPL_DEFINITIONS
 *       v                                   v
 *   vrhi.h (API only)               vrhi_impl.h ──> [globals defined]
 *                                           |
 *                                           | (submodule includes SKIPPED)
 *                                           x
 *
 *   test_impl_backend.cpp     test_impl_device.cpp     test_impl_texture.cpp
 *           |                         |                         |
 *           v                         v                         v
 *   vrhi_impl_backend.h       vrhi_impl_device.h        vrhi_impl_texture.h
 *           |                         |                         |
 *           v                         v                         v
 *   [g_vhCmdBackendState]     [vhInit, vhShutdown]      [vhCreateTexture]
 *
 * Key insight: vrhi_impl.h guards submodule includes with #ifdef VRHI_IMPLEMENTATION.
 * In sharded builds, each shard includes its module directly, bypassing the hub.
 * ============================================================================
 */

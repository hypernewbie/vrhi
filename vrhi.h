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

#if !defined(_WIN32) && !defined(VK_USE_PLATFORM_XLIB_KHR)
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES // Define this if you have these in PCH already.
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

#ifdef VK_USE_PLATFORM_XLIB_KHR
// We need to proactively include vulkan on Linux / mac and undefine the stupid common name macro defines from X11.
#include <vulkan/vulkan.h>
#ifdef None
    #undef None
#endif
    #ifdef Always
    #undef Always
#endif
    #ifdef TileShape
    #undef TileShape
#endif
#endif // #if VK_USE_PLATFORM_XLIB_KHR

#include <nvrhi/vulkan.h>
#include "vrhi_defines.h"

#define VRHI_INVALID_HANDLE 0xFFFFFFFF
#define VRHI_MIPMAP_COMPLETE -1

struct vhInitData
{
    std::string appName = "VRHI_APP";
    std::string engineName = "VRHI_ENGINE";
    bool debug = false;
    int deviceIndex = -1; // -1 for auto-selection (Discrete > Integrated > CPU)
    bool raytracing = true;
    glm::ivec2 resolution = glm::ivec2( 1280, 720 );
    std::function<void( bool error, const std::string& )> fnLogCallback = nullptr;
};

typedef uint32_t vhTexture;
typedef uint32_t vhBuffer;
typedef std::vector<uint8_t> vhMem;
extern vhInitData g_vhInit;
extern nvrhi::DeviceHandle g_vhDevice;
extern std::atomic<int32_t> g_vhErrorCounter;

// --------------------------------------------------------------------------
// Interface
// --------------------------------------------------------------------------

// Manually Regenerate this with py vidl.py vrhi.h vrhi_generated.h
// Cmake should automatically do this already.

// ------------ Device ------------

// Initializes the Vulkan RHI and starts the backend command thread.
//
// Must be called before any other RHI functions. Uses |g_vhInit| for configuration.
void vhInit( bool quiet = false );

// Shuts down the Vulkan RHI and stops the backend command thread.
//
// Cleans up all resources and waits for the GPU to finish.
void vhShutdown( bool quiet = false );

// Returns a string containing information about the selected physical device and queues.
std::string vhGetDeviceInfo();

// Blocks until all commands currently in the queue have been processed by the backend.
//
// This does not wait for the GPU to finish execution.
void vhFlush();

// Blocks until all commands have been processed and the GPU has reached an idle state.
void vhFinish();

// Helper to allocate memory for data upload or download.
// The caller is responsible for allocating data to feed into vh* API functions, but not responsible for freeing it.
// This is freed by the backend every flush when the commands are processed.
inline vhMem* vhAllocMem( uint64_t size )
{
    return new vhMem( size );
}

// Helper to allocate memory for data upload or download, copying the data from the provided vector.
// The caller is responsible for allocating data to feed into vh* API functions, but not responsible for freeing it.
// This is freed by the backend every flush when the commands are processed.
inline vhMem* vhAllocMem( const std::vector< uint8_t >& data )
{
    return new vhMem( data );
}

// ------------ Texture ------------

// Allocates a unique texture handle.
//
// Returns a valid |vhTexture| handle, or |VRHI_INVALID_HANDLE| on failure.
vhTexture vhAllocTexture();

// VIDL_GENERATE
void vhResetTexture( vhTexture texture );

// Allocates a unique buffer handle.
//
// Returns a valid |vhBuffer| handle, or |VRHI_INVALID_HANDLE| on failure.
vhBuffer vhAllocBuffer();

// VIDL_GENERATE
void vhResetBuffer( vhBuffer buffer );

struct vhFormatInfo
{
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    const char* name = nullptr;
    int32_t elementSize = 0;
    int32_t compressionBlockWidth = 0;
    int32_t compressionBlockHeight = 0;
};

// Returns metadata for the specified |format|.
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

// Enqueues a command to destroy the texture associated with |texture|.
//
// |texture| is the handle to the texture to be destroyed.
// VIDL_GENERATE
void vhDestroyTexture( vhTexture texture );

// Enqueues a command to create a texture with the specified parameters.
//
// |texture| must be a handle allocated via |vhAllocTexture|.
// |target| specifies the texture dimensionality.
// |dimensions| specifies width, height, and depth.
// |numMips| and |numLayers| specify mip count and array size.
// |format| is the pixel format.
// |flag| specifies usage and sampling options.
// |data| is optional initial pixel data. Takes ownership of the memory.
// VIDL_GENERATE
void vhCreateTexture(
    vhTexture texture,
    nvrhi::TextureDimension target,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
);

// Helper to create a 2D texture.
inline void vhCreateTexture2D(
    vhTexture texture,
    glm::ivec2 dimensions,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::Texture2D, glm::ivec3( dimensions, 1 ), numMips, 1, format, flag, data );
}

// Helper to create a 3D texture.
inline void vhCreateTexture3D(
    vhTexture texture,
    glm::ivec3 dimensions,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::Texture3D, dimensions, numMips, 1, format, flag, data );
}

// Helper to create a Cube texture.
inline void vhCreateTextureCube(
    vhTexture texture,
    int dimension,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::TextureCube, glm::ivec3( dimension, dimension, 1 ), numMips, 6, format, flag, data );
}

// Helper to create a 2D texture array.
inline void vhCreateTexture2DArray(
    vhTexture texture,
    glm::ivec2 dimensions,
    int numLayers,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::Texture2DArray, glm::ivec3( dimensions, 1 ), numMips, numLayers, format, flag, data );
}

// Helper to create a Cube texture array.
inline void vhCreateTextureCubeArray(
    vhTexture texture,
    int dimension,
    int numLayers,
    int numMips,
    nvrhi::Format format,
    uint64_t flag = VRHI_TEXTURE_NONE | VRHI_SAMPLER_NONE,
    const vhMem* data = nullptr
)
{
    vhCreateTexture( texture, nvrhi::TextureDimension::TextureCubeArray, glm::ivec3( dimension, dimension, 1 ), numMips, numLayers, format, flag, data );
}

// Enqueues a command to update a subresource range of a texture.
//
// |texture| is the handle to the texture to update.
// |startMips| and |startLayers| define the beginning of the range.
// |numMips| and |numLayers| define the size of the range.
// |data| contains the pixel data for the entire texture. Takes ownership of the memory.
// VIDL_GENERATE
void vhUpdateTexture(
    vhTexture texture,
    int startMips = 0, int startLayers = 0,
    int numMips = 1, int numLayers = 1,
    const vhMem* data = nullptr
);

// Enqueues a command to read a subresource range of a texture.
// WARNING: This is a slow path operation, generally for debugging or screenshot purposes.
//
// |texture| is the handle to the texture to read.
// |mip| and |layer| define the subresource to read.
// |outData| is the destination for the pixel data. DOES NOT take ownership of the memory.
// VIDL_GENERATE
void vhReadTextureSlow(
    vhTexture texture,
    int mip = 0, int layer = 0,
    vhMem* outData = nullptr
);

// Enqueues a command to blit (copy/resize/convert) a region from one texture to another.
//
// |dst| and |src| are the destination and source texture handles.
// |dstMip| and |srcMip| specify the mip levels to use.
// |dstLayer| and |srcLayer| specify the array layers to use.
// |dstOffset| and |srcOffset| are the starting coordinates in the respective textures.
// |extent| is the size of the region to blit. If width or height are <= 0, the full source mip size is used.
// Note: Destination texture must have been created with the |VRHI_TEXTURE_BLIT_DST| flag.
// VIDL_GENERATE
void vhBlitTexture(
    vhTexture dst, vhTexture src,
    int dstMip = 0, int srcMip = 0,
    int dstLayer = 0, int srcLayer = 0,
    glm::ivec3 dstOffset = glm::ivec3( 0 ),
    glm::ivec3 srcOffset = glm::ivec3( 0 ),
    glm::ivec3 extent = glm::ivec3( 0 )
);

// ------------ Buffer ------------

// Vertex layouts are defines as standard strings.
// Supported base types: float, half, int, uint, short, ushort, byte, ubyte
// Supported suffixes: 2, 3, 4
// Example: "float3 POSITION half4 NORMAL half4 TANGENT half4 BINORMAL half4 TEXCOORD half4 COLOR";
//
typedef std::string vhVertexLayout;

// Validates a vertex layout string.
bool vhValidateVertexLayout( const vhVertexLayout& layout );


// Allocates a unique buffer handle.
//
// Returns a valid |vhBuffer| handle, or |VRHI_INVALID_HANDLE| on failure.
vhBuffer vhAllocBuffer();

// |numVerts| is the number of vertices in the buffer. It is ignored if |data| is not null.
// VIDL_GENERATE
void vhCreateVertexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    const vhVertexLayout layout,
    uint64_t numVerts = 0,
    uint16_t flags = VRHI_BUFFER_NONE
);

// Enqueues a command to update a buffer with the specified data.
//
// |buffer| is the handle to the buffer to update.
// |data| is the source data. Takes ownership of the memory.
// |offset| is the byte offset within the buffer to start writing.
// |numVerts| is the number of vertices in the buffer. It is ignored if |data| is not null.
// VIDL_GENERATE
void vhUpdateVertexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset = 0,
    uint64_t numVerts = 0
);

// VIDL_GENERATE
void vhCreateIndexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t numIndices = 0,
    uint16_t flags = VRHI_BUFFER_NONE
);

// VIDL_GENERATE
void vhUpdateIndexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset = 0,
    uint64_t numIndices = 0
);

// Enqueues a command to destroy the buffer associated with |buffer|.
//
// |buffer| is the handle to the buffer to be destroyed.
// VIDL_GENERATE
void vhDestroyBuffer( vhBuffer buffer );

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

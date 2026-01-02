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

#if !defined(_WIN32) && !defined(VK_USE_PLATFORM_XLIB_KHR) && !defined(__APPLE__)
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

#if defined(__APPLE__) && !defined(VK_USE_PLATFORM_METAL_EXT)
    #define VK_USE_PLATFORM_METAL_EXT
#endif

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES // Define this if you have these in PCH already.
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <fstream>
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
    std::function<void() > fnThreadInitCallback = nullptr;

#ifdef VRHI_SHADER_COMPILER
    std::string shaderCompileTempDir = "./tmp/shader_cache/";
    std::string shaderMakePath = "./tools/linux_release";
    std::string shaderMakeSlangPath = "./tools/linux_release";
    bool forceShaderRecompile = false;
#endif // VRHI_SHADER_COMPILER
};

typedef uint32_t vhTexture;
typedef uint32_t vhBuffer;
typedef uint32_t vhShader;
typedef std::vector< uint8_t > vhMem;
typedef std::vector< vhShader > vhProgram;

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

struct vhTextureMipInfo
{
    glm::ivec3 dimensions;
    int64_t size;
    int64_t offset;
    int64_t slice_size;
    int32_t pitch;
};

struct vhTexInfo
{
    nvrhi::TextureDimension target = nvrhi::TextureDimension::Texture2D;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    glm::ivec3 dimensions = glm::ivec3( 0, 0, 0 );
    int32_t arrayLayers = 0;
    int32_t mipLevels = 0;
    int32_t samples = 0;
};

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

// Returns base texture info. Optional outMipInfo for detailed layout.
vhTexInfo vhGetTextureInfo( vhTexture texture, std::vector< vhTextureMipInfo >* outMipInfo = nullptr );

// Returns the raw NVRHI handle (nvrhi::ITexture*).
void* vhGetTextureNvrhiHandle( vhTexture texture );

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
// |offsetVerts| is the vertex offset within the buffer to start writing (in vertices, not bytes).
// |numVerts| is the number of vertices in the buffer. It is ignored if |data| is not null.
// VIDL_GENERATE
void vhUpdateVertexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offsetVerts = 0,
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

// Enqueues a command to update an index buffer with the specified data.
//
// |buffer| is the handle to the buffer to update.
// |data| is the source data. Takes ownership of the memory.
// |offsetIndices| is the index offset within the buffer to start writing (in indices, not bytes).
// |numIndices| is the number of indices in the buffer. It is ignored if |data| is not null.
// VIDL_GENERATE
void vhUpdateIndexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offsetIndices = 0,
    uint64_t numIndices = 0
);

// Enqueues a command to create a uniform buffer with the specified parameters.
//
// |buffer| must be a handle allocated via |vhAllocBuffer|.
// |name| is an optional debug name for the buffer.
// |data| is optional initial data. Takes ownership of the memory.
// |size| is the size in bytes. It is ignored if |data| is not null.
// |flags| specifies usage options.
// VIDL_GENERATE
void vhCreateUniformBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size = 0,
    uint16_t flags = VRHI_BUFFER_NONE
);

// Enqueues a command to update a uniform buffer with the specified data.
//
// |buffer| is the handle to the buffer to update.
// |data| is the source data. Takes ownership of the memory.
// |offset| is the byte offset within the buffer to start writing.
// |size| is the size in bytes. It is ignored if |data| is not null.
// Note: While byte-level access is supported, using 4-byte aligned offsets/sizes is recommended for performance (hits the fast-path).
// VIDL_GENERATE
void vhUpdateUniformBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset = 0,
    uint64_t size = 0
);

// Enqueues a command to create a storage buffer with the specified parameters.
//
// |buffer| must be a handle allocated via |vhAllocBuffer|.
// |name| is an optional debug name for the buffer.
// |data| is optional initial data. Takes ownership of the memory.
// |size| is the size in bytes. It is ignored if |data| is not null.
// |flags| specifies usage options.
// VIDL_GENERATE
void vhCreateStorageBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size = 0,
    uint16_t flags = VRHI_BUFFER_COMPUTE_READ_WRITE
);

// Enqueues a command to update a storage buffer with the specified data.
//
// |buffer| is the handle to the buffer to update.
// |data| is the source data. Takes ownership of the memory.
// |offset| is the byte offset within the buffer to start writing.
// |size| is the size in bytes. It is ignored if |data| is not null.
// Note: While byte-level access is supported, using 4-byte aligned offsets/sizes is recommended for performance (hits the fast-path).
// VIDL_GENERATE
void vhUpdateStorageBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset = 0,
    uint64_t size = 0
);

// Enqueues a command to destroy the buffer associated with |buffer|.
//
// |buffer| is the handle to the buffer to be destroyed.
// VIDL_GENERATE
void vhDestroyBuffer( vhBuffer buffer );

// Returns buffer size in bytes. 
// Options: outStride (structure stride), outFlags (usage flags).
uint64_t vhGetBufferInfo( vhBuffer buffer, uint32_t* outStride = nullptr, uint64_t* outFlags = nullptr );

// Returns the raw NVRHI handle (nvrhi::IBuffer*).
void* vhGetBufferNvrhiHandle( vhBuffer buffer );

// ------------ Shaders ------------

vhShader vhAllocShader();

struct vhShaderReflectionResource
{
    std::string name;
    uint32_t slot;
    uint32_t set;
    nvrhi::ResourceType type;
    uint32_t arraySize;
    uint32_t sizeInBytes; // Validation
};

struct vhPushConstantRange { uint32_t offset; uint32_t size; std::string name; };
struct vhSpecConstant { uint32_t id; std::string name; };

// Returns thread group size (for Compute/Mesh/Amp) via out pointer.
// Optional out pointers to populate full reflection data.
void vhGetShaderInfo(
    vhShader shader,
    glm::uvec3* outGroupSize = nullptr,
    std::vector< vhShaderReflectionResource >* outResources = nullptr,
    std::vector< vhPushConstantRange >* outPushConstants = nullptr,
    std::vector< vhSpecConstant >* outSpecConstants = nullptr
);

// Returns the raw NVRHI handle (nvrhi::IShader*).
void* vhGetShaderNvrhiHandle( vhShader shader );

#ifdef VRHI_SHADER_COMPILER
bool vhCompileShader(
    const char* name,
    const char* source,
    uint64_t flags,
    std::vector< uint32_t >& outSpirv,
    const char* entry = "main",
    const std::vector< std::string >& defines = {},
    const std::vector< std::string >& includes = {},
    std::string* outError = nullptr
);
#endif // VRHI_SHADER_COMPILER

// Enqueues a command to create a shader from SPIR-V bytecode.
//
// |flags| is one of VRHI_SHADER_STAGE_*.
// VIDL_GENERATE
void vhCreateShader(
    vhShader shader,
    const char* name,
    uint64_t flags,
    const std::vector< uint32_t >& spirv,
    const char* entry = "main"
);

// Destroy shader.
// VIDL_GENERATE
void vhDestroyShader( vhShader shader );


// Graphics: Standard (Vertex + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader pixelShader )
{
    return { vertexShader, pixelShader };
}

// Graphics: Geometry (Vertex + Geometry + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader geometryShader, vhShader pixelShader )
{
    return { vertexShader, geometryShader, pixelShader };
}

// Graphics: Tessellation (Vertex + Hull + Domain + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader hullShader, vhShader domainShader, vhShader pixelShader )
{
    return { vertexShader, hullShader, domainShader, pixelShader };
}

// Graphics: Full Pipeline (Vertex + Hull + Domain + Geometry + Pixel)
inline vhProgram vhCreateGfxProgram( vhShader vertexShader, vhShader hullShader, vhShader domainShader, vhShader geometryShader, vhShader pixelShader )
{
    return { vertexShader, hullShader, domainShader, geometryShader, pixelShader };
}

// Compute
inline vhProgram vhCreateComputeProgram( vhShader computeShader )
{
    return { computeShader };
}

// Mesh Shading: Basic (Mesh + Pixel)
inline vhProgram vhCreateMeshProgram( vhShader meshShader, vhShader pixelShader )
{
    return { meshShader, pixelShader };
}

// Mesh Shading: Amplified (Amplification + Mesh + Pixel)
inline vhProgram vhCreateMeshProgram( vhShader amplificationShader, vhShader meshShader, vhShader pixelShader )
{
    return { amplificationShader, meshShader, pixelShader };
}

// Raytracing: Simple (RayGen + Miss + ClosestHit)
inline vhProgram vhCreateRTProgram( vhShader rayGen, vhShader miss, vhShader closestHit )
{
    return { rayGen, miss, closestHit };
}

// Raytracing: With AnyHit (RayGen + Miss + ClosestHit + AnyHit)
inline vhProgram vhCreateRTProgram( vhShader rayGen, vhShader miss, vhShader closestHit, vhShader anyHit )
{
    return { rayGen, miss, closestHit, anyHit };
}

// Raytracing: Full Hit Group (RayGen + Miss + ClosestHit + AnyHit + Intersection)
inline vhProgram vhCreateRTProgram( vhShader rayGen, vhShader miss, vhShader closestHit, vhShader anyHit, vhShader intersection )
{
    return { rayGen, miss, closestHit, anyHit, intersection };
}

// ------------ State ------------


typedef uint64_t vhStateId;
typedef uint64_t vhFramebuffer;

// This represents the entire draw state.
// You can submit multiple multiple draw calls or compute dispatches with the same state.
struct vhState
{
    glm::vec4 viewRect;
    glm::vec4 viewScissor;
    vhFramebuffer viewFramebuffer;
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    std::vector< glm::mat4 > worldMatrix;
    uint64_t stateFlags = 0;
    uint64_t dirty = 0;
    
    uint16_t clearFlags = 0;
    uint32_t clearRgba = 0;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;

    uint32_t frontStencil = 0;
    uint32_t backStencil = 0;

    struct VertexBinding
    {
        vhBuffer buffer;
        uint8_t stream = 0;
        uint32_t startVertex = 0;
        uint32_t numVertices = UINT32_MAX;
    };
    std::vector< VertexBinding > vertexBindings;

    struct IndexBinding
    {
        vhBuffer buffer;
        uint32_t firstIndex = 0;
        uint32_t numIndices = UINT32_MAX;
    };
    IndexBinding indexBinding;

    vhState& SetViewRect( const glm::vec4& rect ) { viewRect = rect; dirty |= VRHI_DIRTY_VIEWPORT; return *this; }
    vhState& SetViewScissor( const glm::vec4& scissor ) { viewScissor = scissor; dirty |= VRHI_DIRTY_VIEWPORT; return *this; }
    vhState& SetViewClear( uint16_t clearFlags_, uint32_t rgba = 0, float depth = 1.0f, uint8_t stencil = 0 )
    {
        clearFlags = clearFlags_;
        clearRgba = rgba;
        clearDepth = depth;
        clearStencil = stencil;
        dirty |= VRHI_DIRTY_PIPELINE;
        return *this;
    }
    vhState& SetViewFramebuffer( vhFramebuffer fb ) { viewFramebuffer = fb; dirty |= VRHI_DIRTY_FRAMEBUFFER; return *this; }
    vhState& SetViewTransform( const glm::mat4& view, const glm::mat4& proj )
    {
        viewMatrix = view;
        projMatrix = proj;
        dirty |= VRHI_DIRTY_CAMERA;
        return *this;
    }
    vhState& SetWorldTransform( const glm::mat4& mtx, uint16_t num = 1 )
    {
        worldMatrix.assign( num, mtx );
        dirty |= VRHI_DIRTY_WORLD;
        return *this;
    }
    vhState& SetStateFlags( uint64_t flags ) { stateFlags = flags; dirty |= VRHI_DIRTY_PIPELINE; return *this; }
    vhState& SetStencil( uint32_t front, uint32_t back = 0 )
    {
        frontStencil = front;
        backStencil = back;
        dirty |= VRHI_DIRTY_PIPELINE;
        return *this;
    }
    vhState& SetVertexBuffer( vhBuffer buffer, uint8_t stream, uint32_t startVertex = 0, uint32_t numVertices = UINT32_MAX )
    {
        if ( stream >= vertexBindings.size() ) vertexBindings.resize( stream + 1 );
        vertexBindings[stream] = { buffer, stream, startVertex, numVertices };
        dirty |= VRHI_DIRTY_BINDINGS;
        return *this;
    }
    vhState& SetIndexBuffer( vhBuffer buffer, uint32_t firstIndex = 0, uint32_t numIndices = UINT32_MAX )
    {
        indexBinding = { buffer, firstIndex, numIndices };
        dirty |= VRHI_DIRTY_BINDINGS;
        return *this;
    }
    vhState& dirtyAll()
    {
        dirty = VRHI_DIRTY_ALL;
        return *this;
    }
};

// Query state from backend.
bool vhGetState( vhStateId id, vhState& outState );

// Set state on backend.
bool vhSetState( vhStateId id, vhState& state, uint64_t dirtyForceMask = 0 );

/// TODO: More state functions here.

// --------------------------------------------------------------------------
// Implementation
// --------------------------------------------------------------------------

#ifdef VRHI_IMPLEMENTATION

// VIDL_GENERATE
void vhFlushInternal( std::atomic<bool>* fence, bool waitForGPU = false );

// VIDL_GENERATE
void vhCmdSetStateViewRect( vhStateId id, glm::vec4 rect );
// VIDL_GENERATE
void vhCmdSetStateViewScissor( vhStateId id, glm::vec4 scissor );
// VIDL_GENERATE
void vhCmdSetStateViewClear( vhStateId id, uint16_t flags, uint32_t rgba, float depth, uint8_t stencil );
// VIDL_GENERATE
void vhCmdSetStateViewFramebuffer( vhStateId id, vhFramebuffer fb );
// VIDL_GENERATE
void vhCmdSetStateViewTransform( vhStateId id, glm::mat4 view, glm::mat4 proj );
// VIDL_GENERATE
void vhCmdSetStateWorldTransform( vhStateId id, std::vector< glm::mat4 > matrices );
// VIDL_GENERATE
void vhCmdSetStateFlags( vhStateId id, uint64_t flags );
// VIDL_GENERATE
void vhCmdSetStateStencil( vhStateId id, uint32_t front, uint32_t back );
// VIDL_GENERATE
void vhCmdSetStateVertexBuffer( vhStateId id, uint8_t stream, vhBuffer buffer, uint32_t start, uint32_t num );
// VIDL_GENERATE
void vhCmdSetStateIndexBuffer( vhStateId id, vhBuffer buffer, uint32_t first, uint32_t num );

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

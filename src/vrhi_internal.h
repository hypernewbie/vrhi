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

// --------------------------------------------------------------------------
// Common Includes & Logic
// --------------------------------------------------------------------------

#if defined(_WIN32)
    #define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
    #define VK_USE_PLATFORM_METAL_EXT
#else
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

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
    #ifdef Success
        #undef Success
    #endif
    #ifdef Status
        #undef Status
    #endif
#endif // #if VK_USE_PLATFORM_XLIB_KHR

#include <nvrhi/vulkan.h>

#ifdef VK_USE_PLATFORM_WIN32_KHR
    #if !defined( WIN32_LEAN_AND_MEAN ) || !defined( NOMINMAX )
        #error "VRHI requires WIN32_LEAN_AND_MEAN and NOMINMAX to be defined before including windows.h"
    #endif
    #include <windows.h>
    #include <vulkan/vulkan_win32.h>
#endif

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <climits>
#include <string>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <filesystem>
#include <concurrentqueue/blockingconcurrentqueue.h>

// Required by NVRHI Vulkan backend
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "vrhi.h"
#include "vrhi_generated.h"
#include "vrhi_utils.h"

// --------------------------------------------------------------------------
// Internal Declarations
// --------------------------------------------------------------------------

// Triple buffering is the max we can support.
#define VRHI_MAX_FRAMES_INFLIGHT 3
#define VRHI_CBUF_ALIGN 256

// Internal Vulkan State
extern VkInstance g_vulkanInstance;
extern VkPhysicalDevice g_vulkanPhysicalDevice;
extern VkDevice g_vulkanDevice;
extern VkDebugUtilsMessengerEXT g_vulkanDebugMessenger;
extern uint32_t g_vulkanEnabledExtensionCount;
extern std::mutex g_nvRHIStateMutex;

// Queue Handles
extern VkQueue g_vulkanGraphicsQueue;
extern VkQueue g_vulkanComputeQueue;
extern VkQueue g_vulkanTransferQueue;

// Queue Families
extern uint32_t g_QueueFamilyGraphics;
extern uint32_t g_QueueFamilyCompute;
extern uint32_t g_QueueFamilyTransfer;

// Resource State
extern vhAllocatorObjectFreeList g_vhTextureIDList;
extern std::unordered_map< vhTexture, bool > g_vhTextureIDValid;
extern std::mutex g_vhTextureIDListMutex;
extern vhAllocatorObjectFreeList g_vhBufferIDList;
extern std::unordered_map< vhBuffer, bool > g_vhBufferIDValid;
extern std::mutex g_vhBufferIDListMutex;

// Shader state
extern vhAllocatorObjectFreeList g_vhShaderIDList;
extern std::unordered_map< vhShader, bool > g_vhShaderIDValid;
extern std::mutex g_vhShaderIDListMutex;

extern bool g_vhRayTracingEnabled;

// Command Queue
extern moodycamel::BlockingConcurrentQueue< void* > g_vhCmds;
extern std::atomic<bool> g_vhCmdsQuit;
extern std::thread g_vhCmdThread;
extern std::atomic<bool> g_vhCmdThreadReady;
extern std::vector< std::vector<uint8_t>* > g_vhMemList;
extern std::mutex g_vhMemListMutex;
extern uint64_t g_vhCmdListTransferSizeHeuristic;

// Backend State
class vhCmdBackendState;
extern vhCmdBackendState g_vhCmdBackendState;
void vhBackendInit();
void vhBackendShutdown();
void vhBackendThreadEntry( std::function<void()> initCallback );
vhTexInfo vhBackendQueryTextureInfo( vhTexture texture, std::vector< vhTextureMipInfo >* outMipInfo );
void* vhBackendQueryTextureHandle( vhTexture texture );
uint64_t vhBackendQueryBufferInfo( vhBuffer buffer, uint32_t* outStride, uint64_t* outFlags );
void* vhBackendQueryBufferHandle( vhBuffer buffer );
void vhBackendQueryShaderInfo( vhShader shader, glm::uvec3* outGroupSize, std::vector< vhShaderReflectionResource >* outResources, std::vector< vhPushConstantRange >* outPushConstants, std::vector< vhSpecConstant >* outSpecConstants );
void* vhBackendQueryShaderHandle( vhShader shader );
bool vhBackendQueryState( vhStateId id, vhState& outState );

// Dummy Resources
void vhInitDummyResources();
void vhShutdownDummyResources();
nvrhi::BindingSetItem vhGetDummyBindingItem( const nvrhi::BindingLayoutItem& layoutItem, nvrhi::Format expectedFormat = nvrhi::Format::UNKNOWN, nvrhi::TextureDimension expectedDim = nvrhi::TextureDimension::Texture2D );


// Logging helper
void vhLog( bool error, const char* fmt, ... );
#define VRHI_LOG( fmt, ... ) vhLog( false, fmt, ##__VA_ARGS__ )
#define VRHI_ERR( fmt, ... ) vhLog( true, fmt, ##__VA_ARGS__ )

// Command function templates
template< typename T, typename... Args >
T* vhCmdAlloc( Args&&... args ) { return new T( std::forward<Args>( args )... ); }

template< typename T >
void vhCmdRelease( T* cmd ) { if ( cmd ) delete cmd; }

void vhCmdEnqueue( void* cmd );
void vhCmdListFlushAll();
void vhCmdListFlushTransferIfNeeded();

// Command Lists
extern nvrhi::CommandListHandle g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Count];
nvrhi::CommandListHandle vhCmdListGet( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics );
void vhCmdListFlush( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics ); // This will automatically flush the dependent queues.

struct vhVertexLayoutDef
{
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    int location = 0;
    int offset = 0;
};
const std::vector< vhVertexLayoutDef >* vhBackendQueryBufferLayout( vhBuffer buffer );
nvrhi::VertexAttributeDesc vhTranslateVertexAttribute( const vhVertexLayoutDef& def, uint32_t bufferIndex );
bool vhParseVertexLayoutInternal( const vhVertexLayout& layout, std::vector< vhVertexLayoutDef >& outDefs );
int vhVertexLayoutDefSize( const vhVertexLayoutDef& def );
int vhVertexLayoutDefSize( const std::vector< vhVertexLayoutDef >& def );
int64_t vhGetRegionDataSize( const vhFormatInfo& info, glm::ivec3 extent, int mipLevel = 0 );
bool vhVerifyRegionInTexture( const vhFormatInfo& fmt, glm::ivec3 mipDimensions, glm::ivec3 offset, glm::ivec3 extent, const char* debugName );

nvrhi::SamplerDesc vhGetSamplerDesc( uint64_t samplerFlags );
nvrhi::SamplerHandle vhGetSamplerHandle( uint64_t samplerFlags );
void vhSamplerCacheShutdown();
nvrhi::ComputePipelineHandle vhPSOCacheGet( const nvrhi::ComputePipelineDesc& desc );
nvrhi::GraphicsPipelineHandle vhPSOCacheGet( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& fbInfo );
void vhBindingSetCacheClear();
nvrhi::BindingSetHandle vhGetBindingSet( const nvrhi::BindingSetDesc& desc, nvrhi::BindingLayoutHandle layout );
nvrhi::FramebufferHandle vhFBOCacheGet( const nvrhi::FramebufferDesc& desc );
void vhFBOCacheReset();

struct vhTransientBuffer
{
    int64_t size = 0;
    nvrhi::BufferHandle handle[VRHI_MAX_FRAMES_INFLIGHT];
    uint32_t frameIdx = 0;
    int64_t offset = 0;
    uint8_t* ptr = nullptr; // For storing mapping.

    void Reset();
    int64_t Alloc( int64_t bytes );
    void Step();
    void Init_DeviceStateLocked( const nvrhi::BufferDesc& desc );
    uint8_t* Map_DeviceStateLocked();
    void Unmap_DeviceStateLocked();
    void Shutdown_DeviceStateLocked();
    int64_t Write( const void* data, size_t bytes );
};

struct vhGlobalUniform
{
    glm::vec4 u_viewRect;
    glm::vec4 u_viewTexel;
    glm::mat4 u_view;
    glm::mat4 u_invView;
    glm::mat4 u_proj;
    glm::mat4 u_invProj;
    glm::mat4 u_viewProj;
    glm::mat4 u_invViewProj;
    glm::vec4 u_alphaRef4;
    glm::vec4 u_global[21];
};
static_assert( sizeof( vhGlobalUniform ) < 16384, "vhGlobalUniform must be smaller than 16KB" );
static_assert( sizeof( vhGlobalUniform ) == 768, "vhGlobalUniform packing mismatch" );

struct vhWorldUniform
{
    glm::mat4 u_world[4];
    glm::mat4 u_worldView;
    glm::mat4 u_worldViewProj;
    glm::vec4 _pad[8];
};
static_assert( sizeof( vhWorldUniform ) < 16384, "vhWorldUniform must be smaller than 16KB" );
static_assert( sizeof( vhWorldUniform ) == 512, "vhWorldUniform packing mismatch" );

bool vhReflectSpirv(
    const std::vector< uint32_t >& spirvBlob,
    nvrhi::BindingLayoutDesc& outDesc,
    std::vector< vhShaderReflectionResource >& outResources,
    glm::uvec3& outGroupSize,
    std::vector< vhPushConstantRange >& outPushConstants,
    std::vector< vhVertexLayoutDef >* outInputLayout = nullptr
);

bool vhShaderValidateBinding( const vhShaderReflectionResource& reflection, const nvrhi::BindingLayoutItem& binding, bool logError );
bool vhDebugLayoutDiffCheck( const nvrhi::BindingLayoutVector& layouts, const nvrhi::BindingSetVector& bindings );
uint64_t vhHashGraphicsPipeline( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& fbInfo );
uint64_t vhHashComputePipeline( const nvrhi::ComputePipelineDesc& desc );
uint64_t vhHashBindingLayout( const nvrhi::BindingLayoutDesc& desc );
uint64_t vhHashBindingSet( const nvrhi::BindingSetDesc& desc, nvrhi::BindingLayoutHandle layout );
uint64_t vhHashShaderDebugName( nvrhi::ShaderHandle shader );
uint64_t vhHashShaderSPIRV( const std::vector< uint32_t >& spirv );
uint64_t vhHashInputLayout( nvrhi::InputLayoutHandle layout );
uint64_t vhHashSamplerDesc( const nvrhi::SamplerDesc& desc );
uint64_t vhHashGlobalUniform( const vhGlobalUniform& u );
uint64_t vhHashWorldUniform( const vhWorldUniform& u );
uint64_t vhHashFrameBuffer( const nvrhi::FramebufferDesc& desc );
void vhWriteStateToGlobalUniform( const vhState& state, vhGlobalUniform& out );
void vhWriteStateToWorldUniform( const vhState& state, vhWorldUniform& out );
nvrhi::PrimitiveType vhTranslatePrimitiveType( uint64_t stateFlags );
nvrhi::BlendState vhTranslateBlendState( uint64_t stateFlags );
nvrhi::DepthStencilState vhTranslateDepthStencilState( uint64_t stateFlags, uint32_t frontStencil, uint32_t backStencil );
nvrhi::RasterState vhTranslateRasterState( uint64_t stateFlags );




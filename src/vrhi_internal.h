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
    #ifdef Bool
        #undef Bool
    #endif
#endif // #if VK_USE_PLATFORM_XLIB_KHR

#include <nvrhi/vulkan.h>

#ifdef VK_USE_PLATFORM_WIN32_KHR
    #if !defined( WIN32_LEAN_AND_MEAN ) || !defined( NOMINMAX )
        #error "VRHI requires WIN32_LEAN_AND_MEAN and NOMINMAX to be defined before including windows.h"
    #endif
    #include <malloc.h>
    #include <windows.h>
    #include <vulkan/vulkan_win32.h>
#endif

#include <cstddef>
#include <cstdlib>
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
extern nvrhi::vulkan::IDevice* g_vhVulkanDevice;
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

// Swapchain State
extern VkSurfaceKHR g_vhSurface;
extern VkSwapchainKHR g_vhSwapchain;
extern std::vector< VkImage > g_vhSwapchainImages;
extern std::vector< VkImageView > g_vhSwapchainImageViews;
extern std::vector< vhTexture > g_vhSwapchainTextures;
extern std::vector< nvrhi::TextureHandle > g_vhSwapchainNVRHIHandles;
extern uint32_t g_vhCurrentSwapchainIndex;
extern glm::uvec2 g_vhWindowSize;

// Semaphores for frame synchronisation
extern std::vector< VkSemaphore > g_vhAcquireSemaphores;
extern std::vector< VkSemaphore > g_vhPresentSemaphores;
extern std::vector< uint64_t > g_vhFrameInstances;
extern uint32_t g_vhFrameIndex;
extern int g_vhFramesInFlight;

// Pending Queue Sync (Protected by g_nvRHIStateMutex)
extern std::vector< VkSemaphore > g_vhPendingWaitSemaphores[3]; // Indexed by nvrhi::CommandQueue
extern std::vector< VkSemaphore > g_vhPendingSignalSemaphores[3];

// Resource State
extern vhAllocatorObjectFreeList g_vhTextureIDList;
extern std::unordered_map< vhTexture, bool > g_vhTextureIDValid;
extern std::mutex g_vhTextureIDListMutex;
extern vhAllocatorObjectFreeList g_vhBufferIDList;
extern std::unordered_map< vhBuffer, bool > g_vhBufferIDValid;
extern std::mutex g_vhBufferIDListMutex;
extern vhAllocatorObjectFreeList g_vhHeapIDList;
extern std::unordered_map< vhHeap, bool > g_vhHeapIDValid;
extern std::mutex g_vhHeapIDListMutex;

// Shader state
extern vhAllocatorObjectFreeList g_vhShaderIDList;
extern std::unordered_map< vhShader, bool > g_vhShaderIDValid;
extern std::mutex g_vhShaderIDListMutex;

// Raytracing state
extern vhAllocatorObjectFreeList g_vhAccelStructIDList;
extern std::unordered_map< vhAccelStruct, bool > g_vhAccelStructIDValid;
extern std::mutex g_vhAccelStructIDListMutex;
extern vhAllocatorObjectFreeList g_vhRTPipelineIDList;
extern std::unordered_map< vhRTPipeline, bool > g_vhRTPipelineIDValid;
extern std::mutex g_vhRTPipelineIDListMutex;
extern vhAllocatorObjectFreeList g_vhShaderTableIDList;
extern std::unordered_map< vhShaderTable, bool > g_vhShaderTableIDValid;
extern std::mutex g_vhShaderTableIDListMutex;

extern bool g_vhRayTracingEnabled;
extern bool g_vhMemoryBudgetEnabled;
extern bool g_vhNullMode;

#include <renderdoc_app.h>
extern RENDERDOC_API_1_1_2* g_vhRenderDoc;

// Command Queue
extern moodycamel::BlockingConcurrentQueue< void* > g_vhCmds;
extern std::atomic<bool> g_vhCmdsQuit;
extern std::thread g_vhCmdThread;
extern std::atomic<bool> g_vhCmdThreadReady;
// Backend-thread only (no mutex). Drained at end of Handle_vhFlushInternal.
extern std::vector< std::vector<uint8_t>* > g_vhMemList;
extern uint64_t g_vhCmdListTransferSizeHeuristic;

// Backend State
class vhCmdBackendState;
extern vhCmdBackendState g_vhCmdBackendState;
void vhBackendInit();
void vhBackendShutdown();
void vhBackendThreadEntry( std::function<void()> initCallback );
vhTexInfo vhBackendQueryTextureInfo( vhTexture texture, std::vector< vhTextureMipInfo >* outMipInfo );
nvrhi::TextureHandle vhBackendQueryTextureHandle( vhTexture texture );
uint64_t vhBackendQueryBufferInfo( vhBuffer buffer, uint32_t* outStride, uint64_t* outFlags );
nvrhi::BufferHandle vhBackendQueryBufferHandle( vhBuffer buffer );
void vhBackendQueryShaderInfo( vhShader shader, glm::uvec3* outGroupSize, std::vector< vhShaderReflectionResource >* outResources, std::vector< vhPushConstantRange >* outPushConstants, std::vector< vhSpecConstant >* outSpecConstants );
nvrhi::ShaderHandle vhBackendQueryShaderHandle( vhShader shader );
bool vhBackendQueryState( vhStateId id, vhState& outState );
float vhBackendQueryTimer( vhTimerID timerID );
nvrhi::rt::AccelStructHandle vhBackendQueryAccelStructHandle( vhAccelStruct as );
nvrhi::rt::PipelineHandle vhBackendQueryRTPipelineHandle( vhRTPipeline pipeline );
nvrhi::rt::ShaderTableHandle vhBackendQueryShaderTableHandle( vhShaderTable table );
glm::u64vec2 vhBackendQueryTextureMemoryRequirements( vhTexture texture );
glm::u64vec2 vhBackendAllocTextureMemory( vhHeap heap, uint64_t size, uint64_t alignment );
void vhBackendFreeTextureMemory( vhHeap heap, uint64_t offset );
glm::u64vec2 vhBackendQueryBufferMemoryRequirements( vhBuffer buffer );

// Dummy Resources
void vhInitDummyResources();
void vhShutdownDummyResources();
nvrhi::BindingSetItem vhGetDummyBindingItem( const nvrhi::BindingLayoutItem& layoutItem, nvrhi::Format expectedFormat = nvrhi::Format::UNKNOWN, nvrhi::TextureDimension expectedDim = nvrhi::TextureDimension::Texture2D );

// Internal query functions that assume g_nvRHIStateMutex is already held
bool vhQueryFeatureSupport_Internal( nvrhi::Feature feature, void* pInfo = nullptr, size_t infoSize = 0 );
nvrhi::FormatSupport vhQueryFormatSupport_Internal( nvrhi::Format format );

void vhSwapchainCreate_Internal( int width, int height );


// Logging helper
void vhLog( bool error, const char* fmt, ... );
#define VRHI_LOG( fmt, ... ) vhLog( false, fmt, ##__VA_ARGS__ )
#define VRHI_ERR( fmt, ... ) vhLog( true, fmt, ##__VA_ARGS__ )

// Inlined so the no-callback hot path is a single load + branch.
inline void vhProfile( const char* name, bool begin )
{
    if ( g_vhInit.fnProfileCallback ) g_vhInit.fnProfileCallback( name, begin );
}

struct vhProfileScope
{
    const char* name;
    vhProfileScope( const char* name_ ) : name( name_ ) { vhProfile( name, true ); }
    ~vhProfileScope() { vhProfile( name, false ); }
};

#define VRHI_PROFILE_SCOPE( name ) vhProfileScope _vh_profile_scope( name )
#if defined(_MSC_VER)
#define VRHI_PROFILE_FUNCTION() VRHI_PROFILE_SCOPE( __FUNCTION__ )
#else
#define VRHI_PROFILE_FUNCTION() VRHI_PROFILE_SCOPE( __func__ )
#endif

class vhCommandArena
{
    static constexpr int kNumBuffers = 3;
    static constexpr size_t kBufferSize = 4 * 1024 * 1024;
    static constexpr size_t kBufferAlignment = 64;

    struct Buffer
    {
        uint8_t* memory = nullptr;
        std::atomic< size_t > offset = 0;
    };

    Buffer m_buffers[kNumBuffers];
    std::atomic< int > m_writeIndex = 0;
    bool m_initialised = false;

    // m_mutex only protects Init/Shutdown and the overflow vectors.
    // Allocate fast path is lock-free via atomic offset bump.
    mutable std::mutex m_mutex;

    // Overflow malloc'd commands. Freed one cycle later (after the backend has consumed them).
    std::vector< void* > m_overflowAllocations;
    std::vector< void* > m_overflowAllocationsRecycle;

    static void* AllocAligned( size_t size, size_t alignment )
    {
        alignment = std::max( alignment, alignof( std::max_align_t ) );

#if defined( _WIN32 )
        return _aligned_malloc( size, alignment );
#else
        void* mem = nullptr;
        if ( posix_memalign( &mem, alignment, size ) != 0 ) return nullptr;
        return mem;
#endif
    }

    static void FreeAligned( void* memory )
    {
#if defined( _WIN32 )
        _aligned_free( memory );
#else
        free( memory );
#endif
    }

public:
    void Init()
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        if ( m_initialised ) return;

        for ( int i = 0; i < kNumBuffers; i++ )
        {
            m_buffers[i].memory = static_cast< uint8_t* >( AllocAligned( kBufferSize, kBufferAlignment ) );
            assert( m_buffers[i].memory );
            m_buffers[i].offset.store( 0, std::memory_order_relaxed );
        }
        m_writeIndex.store( 0, std::memory_order_relaxed );
        m_initialised = true;
    }

    void Shutdown()
    {
        std::lock_guard< std::mutex > lock( m_mutex );
        if ( !m_initialised ) return;

        m_initialised = false;
        for ( int i = 0; i < kNumBuffers; i++ )
        {
            FreeAligned( m_buffers[i].memory );
            m_buffers[i].memory = nullptr;
            m_buffers[i].offset.store( 0, std::memory_order_relaxed );
        }
        for ( void* p : m_overflowAllocations ) FreeAligned( p );
        for ( void* p : m_overflowAllocationsRecycle ) FreeAligned( p );
        m_overflowAllocations.clear();
        m_overflowAllocationsRecycle.clear();
        m_writeIndex.store( 0, std::memory_order_relaxed );
    }

    void* Allocate( size_t size, size_t alignment )
    {
        if ( !m_initialised ) return nullptr;

        const int idx = m_writeIndex.load( std::memory_order_acquire );
        Buffer& current = m_buffers[idx];
        if ( !current.memory ) return nullptr;

        const size_t alignMask = alignment - 1;
        size_t prev = current.offset.load( std::memory_order_relaxed );
        for (;;)
        {
            const size_t alignedOffset = ( prev + alignMask ) & ~alignMask;
            const size_t newOffset = alignedOffset + size;
            if ( newOffset > kBufferSize ) break;
            if ( current.offset.compare_exchange_weak( prev, newOffset, std::memory_order_relaxed, std::memory_order_relaxed ) )
                return current.memory + alignedOffset;
        }

        // Overflow: take the lock only to track the malloc'd block for later free.
        g_vhPerf.arenaOverflows.fetch_add( 1, std::memory_order_relaxed );
        g_vhPerf.arenaMallocBytes.fetch_add( size, std::memory_order_relaxed );
        void* mem = AllocAligned( size, alignment );
        {
            std::lock_guard< std::mutex > lock( m_mutex );
            m_overflowAllocations.push_back( mem );
        }
        return mem;
    }

    void Rotate()
    {
        // Defer overflow frees by one cycle so commands enqueued this cycle remain valid for the backend.
        std::vector< void* > toFree;
        {
            std::lock_guard< std::mutex > lock( m_mutex );
            if ( !m_initialised ) return;

            const int idx = m_writeIndex.load( std::memory_order_relaxed );
            const int oldIndex = ( idx + 1 ) % kNumBuffers;
            m_buffers[oldIndex].offset.store( 0, std::memory_order_relaxed );
            m_writeIndex.store( ( idx + 1 ) % kNumBuffers, std::memory_order_release );

            toFree.swap( m_overflowAllocationsRecycle );
            m_overflowAllocationsRecycle.swap( m_overflowAllocations );
        }
        for ( void* p : toFree ) FreeAligned( p );
    }
};

extern vhCommandArena g_vhCmdArena;

template< typename T, typename... Args >
T* vhCmdAlloc( Args&&... args )
{
    void* mem = g_vhCmdArena.Allocate( sizeof( T ), alignof( T ) );
    assert( mem );
    return new ( mem ) T( std::forward<Args>( args )... );
}

template< typename T >
void vhCmdRelease( T* cmd )
{
    if ( cmd ) cmd->~T();
}

// Non-owning view into command-arena memory. Valid until the next arena rotation.
template< typename T >
struct vhArenaSpan
{
    T*       ptr   = nullptr;
    uint32_t count = 0;

    vhArenaSpan() = default;
    vhArenaSpan( T* p, uint32_t n ) : ptr( p ), count( n ) {}

    bool     empty() const { return count == 0; }
    uint32_t size()  const { return count; }
    T*       data()        { return ptr; }
    const T* data()  const { return ptr; }

    T&       operator[]( size_t i )       { return ptr[i]; }
    const T& operator[]( size_t i ) const { return ptr[i]; }

    T*       begin()       { return ptr; }
    T*       end()         { return count ? ptr + count : ptr; }
    const T* begin() const { return ptr; }
    const T* end()   const { return count ? ptr + count : ptr; }
};

// Distinct types so the VIDL Set{Constants,Uniforms} commands cannot be cross-assigned.
struct vhArenaConstantValue
{
    const char*      name;
    const glm::vec4* data;
    uint32_t         dataCount;
};

struct vhArenaUniformValue
{
    const char*      name;
    const glm::vec4* data;
    uint32_t         dataCount;
};

inline size_t vhArenaAlignUp( size_t off, size_t a ) { return ( off + a - 1 ) & ~( a - 1 ); }

template< typename T >
vhArenaSpan< T > vhArenaCopySpan( const std::vector< T >& src )
{
    static_assert( std::is_trivially_copyable_v< T >, "vhArenaCopySpan: T must be trivially copyable" );
    const uint32_t n = ( uint32_t ) src.size();
    if ( !n ) return {};
    void* mem = g_vhCmdArena.Allocate( ( size_t ) n * sizeof( T ), alignof( T ) );
    assert( mem );
    memcpy( mem, src.data(), ( size_t ) n * sizeof( T ) );
    return vhArenaSpan< T >( ( T* ) mem, n );
}

template< typename T, typename U >
void vhAssignFromSpan( std::vector< T >& dst, const vhArenaSpan< U >& src )
{
    if ( src.count == 0 )
    {
        dst.clear();
        return;
    }
    dst.assign( src.ptr, src.ptr + src.count );
}

// Pack {CmdT header}{mat4 * n} into one arena chunk; cmd's span field points at the tail.
template< typename CmdT >
CmdT* vhCmdAllocMat4Span( vhStateId id, const std::vector< glm::mat4 >& matrices )
{
    const uint32_t n = ( uint32_t ) matrices.size();
    const size_t headerEnd = vhArenaAlignUp( sizeof( CmdT ), alignof( glm::mat4 ) );
    const size_t totalBytes = headerEnd + ( size_t ) n * sizeof( glm::mat4 );
    const size_t maxAlign = std::max( alignof( CmdT ), alignof( glm::mat4 ) );

    void* mem = g_vhCmdArena.Allocate( totalBytes, maxAlign );
    assert( mem );

    glm::mat4* dataPtr = n ? ( glm::mat4* ) ( ( char* ) mem + headerEnd ) : nullptr;
    if ( n ) memcpy( dataPtr, matrices.data(), ( size_t ) n * sizeof( glm::mat4 ) );

    return new ( mem ) CmdT( id, vhArenaSpan< glm::mat4 >( dataPtr, n ) );
}

// Pack {CmdT header}{ArenaValueT * n}{vec4 * sum-of-data-counts} into one arena chunk.
template< typename CmdT, typename ArenaValueT, typename SrcValueT >
CmdT* vhCmdAllocNamedVec4Span( vhStateId id, const std::vector< SrcValueT >& src )
{
    const uint32_t n = ( uint32_t ) src.size();
    uint32_t totalDataCount = 0;
    for ( const auto& v : src ) totalDataCount += ( uint32_t ) v.data.size();

    const size_t valuesOff = vhArenaAlignUp( sizeof( CmdT ), alignof( ArenaValueT ) );
    const size_t dataOff = vhArenaAlignUp( valuesOff + ( size_t ) n * sizeof( ArenaValueT ), alignof( glm::vec4 ) );
    const size_t totalBytes = dataOff + ( size_t ) totalDataCount * sizeof( glm::vec4 );
    const size_t maxAlign = std::max( { alignof( CmdT ), alignof( ArenaValueT ), alignof( glm::vec4 ) } );

    void* mem = g_vhCmdArena.Allocate( totalBytes, maxAlign );
    assert( mem );

    ArenaValueT* values = n ? ( ArenaValueT* ) ( ( char* ) mem + valuesOff ) : nullptr;
    glm::vec4*   dataBuf = totalDataCount ? ( glm::vec4* ) ( ( char* ) mem + dataOff ) : nullptr;

    uint32_t cursor = 0;
    for ( uint32_t i = 0; i < n; i++ )
    {
        const auto& s = src[i];
        const uint32_t dn = ( uint32_t ) s.data.size();
        values[i].name      = s.name;
        values[i].data      = dn ? dataBuf + cursor : nullptr;
        values[i].dataCount = dn;
        if ( dn ) memcpy( dataBuf + cursor, s.data.data(), ( size_t ) dn * sizeof( glm::vec4 ) );
        cursor += dn;
    }

    return new ( mem ) CmdT( id, vhArenaSpan< ArenaValueT >( values, n ) );
}

void vhCmdEnqueue( void* cmd, bool wait = true );
void vhCmdListFlushAll();
void vhCmdListFlushTransferIfNeeded();

// Heap Management
vhHeap vhAllocHeap();
bool vhCreateHeap( vhHeap heap, uint64_t size, const char* name );
void vhDestroyHeap( vhHeap heap );
void vhBindTextureMemory( vhTexture texture, vhHeap heap, uint64_t offset );
glm::u64vec2 vhGetTextureMemoryRequirements( vhTexture texture );
glm::u64vec2 vhHeapAlloc( vhHeap heap, uint64_t size, uint64_t alignment );
void vhHeapFree( vhHeap heap, uint64_t offset );
glm::u64vec2 vhAllocBindTextureMemory( vhTexture texture, vhHeap heap );

// Command Lists
extern nvrhi::CommandListHandle g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Count];
nvrhi::CommandListHandle vhCmdListGet( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics );
uint64_t vhCmdListFlush( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics ); // This will automatically flush the dependent queues.

struct vhVertexLayoutDef
{
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    int location = 0;
    int offset = 0;
    bool isInstanced = false;
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

uint32_t vhPatchSpirvDescriptorSet0( std::vector< uint32_t >& spirv, uint32_t targetSet );

void vhPackUserGlobals(
    const std::vector< vhState::UniformBufferValue >& uniforms,
    const std::vector< vhReflectionMember >& members,
    uint8_t* outData,
    uint64_t dataSize
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
uint64_t vhHashVertexAttributeDesc( int location, const nvrhi::VertexAttributeDesc& attr, uint64_t h );
uint64_t vhHashSamplerDesc( const nvrhi::SamplerDesc& desc );
uint64_t vhHashGlobalUniform( const vhGlobalUniform& u );
uint64_t vhHashWorldUniform( const vhWorldUniform& u );
uint64_t vhHashReflectionMembers( const std::vector< vhReflectionMember >& members );
uint64_t vhHashFrameBuffer( const nvrhi::FramebufferDesc& desc );
void vhWriteStateToGlobalUniform( const vhState& state, vhGlobalUniform& out );
void vhWriteStateToWorldUniform( const vhState& state, vhWorldUniform& out );
nvrhi::PrimitiveType vhTranslatePrimitiveType( uint64_t stateFlags );
nvrhi::BlendState vhTranslateBlendState( uint64_t stateFlags );
nvrhi::DepthStencilState vhTranslateDepthStencilState( uint64_t stateFlags, uint64_t stencilState );
nvrhi::RasterState vhTranslateRasterState( uint64_t stateFlags );
nvrhi::VariableShadingRate vhTranslateShadingRate( uint32_t rate );
nvrhi::ShadingRateCombiner vhTranslateShadingRateCombiner( uint32_t combiner );
void vhSetPushConstant_DeviceStateLocked( nvrhi::CommandListHandle cmdList, const vhState& state, size_t pipelineSizeBytes );
size_t vhPushConstantSize( const std::vector< vhPushConstantRange >& ranges );

// Calls fn(paddedSrc) where paddedSrc has at least 3 readable bytes past the data tail.
// NVRHI rounds source read sizes up to 4 bytes; the padding ensures the over-read doesn't fault.
template < typename Fn >
inline void vhWithPaddedBuffer4( const void* src, size_t size, Fn&& fn )
{
    const size_t paddedSize = ( size + 3 ) & ~size_t( 3 );
    if ( paddedSize == size )
    {
        fn( src );
        return;
    }
    constexpr size_t stackCapacity = 256;
    uint8_t stackBuf[ stackCapacity ];
    uint8_t* heapBuf = ( paddedSize > stackCapacity ) ? new uint8_t[ paddedSize ] : nullptr;
    uint8_t* dst = heapBuf ? heapBuf : stackBuf;
    memcpy( dst, src, size );
    memset( dst + size, 0, paddedSize - size );
    fn( ( const void* ) dst );
    delete[] heapBuf;
}

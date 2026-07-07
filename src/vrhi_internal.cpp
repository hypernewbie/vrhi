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

#include "vrhi_internal.h"
#include "vrhi_utils.h"

bool vhBackendQueryState( vhStateId id, vhState& outState );

// WARNING: This must be locked before ANY access to ANY Vulkan state!
std::mutex g_nvRHIStateMutex;

// # Device and Queues

vhInitData g_vhInit;
nvrhi::DeviceHandle g_vhDevice = nullptr;
std::atomic<int32_t> g_vhErrorCounter = 0;
std::atomic<int32_t> g_vhPSOCompileCounter = 0;

VkInstance g_vulkanInstance = VK_NULL_HANDLE;
VkPhysicalDevice g_vulkanPhysicalDevice = VK_NULL_HANDLE;
VkDevice g_vulkanDevice = VK_NULL_HANDLE;
nvrhi::vulkan::IDevice* g_vhVulkanDevice = nullptr;
VkDebugUtilsMessengerEXT g_vulkanDebugMessenger = VK_NULL_HANDLE;
uint32_t g_vulkanEnabledExtensionCount = 0;

VkQueue g_vulkanGraphicsQueue = VK_NULL_HANDLE;
VkQueue g_vulkanComputeQueue = VK_NULL_HANDLE;
VkQueue g_vulkanTransferQueue = VK_NULL_HANDLE;

uint32_t g_QueueFamilyGraphics = ( uint32_t ) -1;
uint32_t g_QueueFamilyCompute = ( uint32_t ) -1;
uint32_t g_QueueFamilyTransfer = ( uint32_t ) -1;

// # Graphics Resource Objects

vhAllocatorObjectFreeList g_vhTextureIDList( 16 * 1024 );
std::unordered_map< vhTexture, bool > g_vhTextureIDValid;
std::mutex g_vhTextureIDListMutex;

vhAllocatorObjectFreeList g_vhBufferIDList( 16 * 1024 );
std::unordered_map< vhBuffer, bool > g_vhBufferIDValid;
std::mutex g_vhBufferIDListMutex;

vhAllocatorObjectFreeList g_vhHeapIDList( 4 * 1024 );
std::unordered_map< vhHeap, bool > g_vhHeapIDValid;
std::mutex g_vhHeapIDListMutex;

// Shader
vhAllocatorObjectFreeList g_vhShaderIDList( 16 * 1024 );
std::unordered_map< vhShader, bool > g_vhShaderIDValid;
std::mutex g_vhShaderIDListMutex;

// Raytracing
vhAllocatorObjectFreeList g_vhAccelStructIDList( 4 * 1024 );
std::unordered_map< vhAccelStruct, bool > g_vhAccelStructIDValid;
std::mutex g_vhAccelStructIDListMutex;
vhAllocatorObjectFreeList g_vhRTPipelineIDList( 1 * 1024 );
std::unordered_map< vhRTPipeline, bool > g_vhRTPipelineIDValid;
std::mutex g_vhRTPipelineIDListMutex;
vhAllocatorObjectFreeList g_vhShaderTableIDList( 4 * 1024 );
std::unordered_map< vhShaderTable, bool > g_vhShaderTableIDValid;
std::mutex g_vhShaderTableIDListMutex;

vhAllocatorObjectFreeList g_vhDescriptorTableIDList( 4 * 1024 );
std::unordered_map< vhDescriptorTable, bool > g_vhDescriptorTableIDValid;
std::mutex g_vhDescriptorTableIDListMutex;

bool g_vhRayTracingEnabled = false;
bool g_vhNullMode = false;

// # Backend Command List Thread

moodycamel::BlockingConcurrentQueue< void* > g_vhCmds( 32 * 1024 );
std::atomic<bool> g_vhCmdsQuit = false;
std::thread g_vhCmdThread;
std::atomic<bool> g_vhCmdThreadReady = false;
std::vector< vhMem* > g_vhMemList;

vhCommandArena g_vhCmdArena;

// Vulkan HPP Storage
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// Implementations

void vhLog( bool error, const char* fmt, ... )
{
    if ( error ) g_vhErrorCounter++;
    char buffer[2048];
    va_list args;
    va_start( args, fmt );
    vsnprintf( buffer, sizeof( buffer ), fmt, args );
    va_end( args );

    if ( g_vhInit.fnLogCallback )
        g_vhInit.fnLogCallback( error, std::string( buffer ) );
    else
        printf( "%s", buffer );
}

vhPerfCounters g_vhPerf;

void vhPerfCheck( bool reset )
{
    auto load = []( std::atomic< uint64_t >& a ) -> uint64_t { return a.load( std::memory_order_relaxed ); };
    uint64_t arenaOver       = load( g_vhPerf.arenaOverflows );
    uint64_t arenaMalloc     = load( g_vhPerf.arenaMallocBytes );
    uint64_t yields          = load( g_vhPerf.enqueueYields );
    uint64_t retryFloor      = load( g_vhPerf.enqueueRetryFloor );
    uint64_t resolveRebuilds = load( g_vhPerf.resolveCacheRebuilds );
    uint64_t resolveHits     = load( g_vhPerf.resolveCacheHits );
    uint64_t psoHits         = load( g_vhPerf.psoCacheHits );
    uint64_t psoMisses       = load( g_vhPerf.psoCacheMisses );
    uint64_t any = arenaOver | arenaMalloc | yields | retryFloor | resolveRebuilds | resolveHits | psoHits | psoMisses;
    if ( !any ) return;

    VRHI_LOG( "vhPerfCheck:\n" );
    VRHI_LOG( "    arenaOverflows = %llu (malloc bytes %llu)\n", arenaOver, arenaMalloc );
    VRHI_LOG( "    enqueueYields  = %llu (retryFloor %llu)\n", yields, retryFloor );
    VRHI_LOG( "    resolveCache   = %llu hits, %llu rebuilds\n", resolveHits, resolveRebuilds );
    VRHI_LOG( "    psoCache       = %llu hits, %llu misses\n", psoHits, psoMisses );

    if ( reset )
    {
        g_vhPerf.arenaOverflows.store( 0, std::memory_order_relaxed );
        g_vhPerf.arenaMallocBytes.store( 0, std::memory_order_relaxed );
        g_vhPerf.enqueueYields.store( 0, std::memory_order_relaxed );
        g_vhPerf.enqueueRetryFloor.store( 0, std::memory_order_relaxed );
        g_vhPerf.resolveCacheRebuilds.store( 0, std::memory_order_relaxed );
        g_vhPerf.resolveCacheHits.store( 0, std::memory_order_relaxed );
        g_vhPerf.psoCacheHits.store( 0, std::memory_order_relaxed );
        g_vhPerf.psoCacheMisses.store( 0, std::memory_order_relaxed );
    }
}

void vhCmdEnqueue( void* cmd, bool wait )
{
    for ( int i = 0; i < 16; i++ )
    {
        if ( g_vhCmds.try_enqueue( cmd ) )
        {
            if ( wait && g_vhInit.debugBlockWaitForBackend ) vhFlush();
            return;
        }
    }
    for ( int i = 0; i < 8; i++ )
    {
        if ( g_vhCmds.try_enqueue( cmd ) )
        {
            if ( wait && g_vhInit.debugBlockWaitForBackend ) vhFlush();
            return;
        }
        std::this_thread::yield();
        g_vhPerf.enqueueYields.fetch_add( 1, std::memory_order_relaxed );
    }
    g_vhPerf.enqueueRetryFloor.fetch_add( 1, std::memory_order_relaxed );
    g_vhCmds.enqueue( cmd );
    if ( wait && g_vhInit.debugBlockWaitForBackend ) vhFlush();
}

nvrhi::CommandListHandle g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Count] = { nullptr, nullptr, nullptr };
bool g_vhCmdListOpen[( uint64_t ) nvrhi::CommandQueue::Count] = { false, false, false };
uint64_t g_vhCmdListTransferSizeHeuristic = 0;

// Persistent: handle is created once and re-opened after each execute to keep the upload pool alive.
nvrhi::CommandListHandle vhCmdListGet( nvrhi::CommandQueue type )
{
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    auto typeIdx = ( uint64_t ) type;
    if ( !g_vhCmdLists[typeIdx] )
    {
        nvrhi::CommandListParameters params = { .queueType = ( nvrhi::CommandQueue ) type };
        g_vhCmdLists[typeIdx] = g_vhDevice->createCommandList( params );
    }
    if ( !g_vhCmdListOpen[typeIdx] )
    {
        g_vhCmdLists[typeIdx]->open();
        g_vhCmdListOpen[typeIdx] = true;
    }
    return g_vhCmdLists[typeIdx];
}

// Returns the instance ID of the executed command list.
// Automatically inserts semaphore waits for downstream queues:
// - Copy feeds Compute and Graphics
// - Compute feeds Graphics
uint64_t vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( nvrhi::CommandQueue type )
{
    // WARNING: Lock g_nvRHIStateMutex before calling this.
    auto typeIdx = ( uint64_t ) type;
    uint64_t instance = 0;

    if ( g_vhCmdLists[typeIdx] && g_vhCmdListOpen[typeIdx] )
    {
        g_vhCmdLists[typeIdx]->close();

        // Handle is intentionally retained; the next vhCmdListGet() will re-open it.
        instance = g_vhDevice->executeCommandList( g_vhCmdLists[typeIdx], type );
        g_vhCmdListOpen[typeIdx] = false;

        // Automatic Synchronisation
        if ( instance )
        {
            if ( type == nvrhi::CommandQueue::Copy )
            {
                // Copy feeds Compute and Graphics
                g_vhDevice->queueWaitForCommandList( nvrhi::CommandQueue::Compute, nvrhi::CommandQueue::Copy, instance );
                g_vhDevice->queueWaitForCommandList( nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Copy, instance );
            }
            else if ( type == nvrhi::CommandQueue::Compute )
            {
                // Compute feeds Graphics
                g_vhDevice->queueWaitForCommandList( nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Compute, instance );
            }
        }
    }

    return instance;
}

// Shutdown only. Lock g_nvRHIStateMutex before calling.
void vhCmdListReleaseAll_DeviceStateLocked()
{
    for ( uint64_t i = 0; i < ( uint64_t ) nvrhi::CommandQueue::Count; i++ )
    {
        g_vhCmdLists[i] = nullptr;
        g_vhCmdListOpen[i] = false;
    }
}

// Semaphores for frame synchronisation
std::vector< VkSemaphore > g_vhAcquireSemaphores;
std::vector< VkSemaphore > g_vhPresentSemaphores;
std::vector< uint64_t > g_vhFrameInstances;
uint32_t g_vhFrameIndex = 0;
int g_vhFramesInFlight = 2;

// Pending Queue Sync (Protected by g_nvRHIStateMutex)
std::vector< VkSemaphore > g_vhPendingWaitSemaphores[3]; // Indexed by nvrhi::CommandQueue
std::vector< VkSemaphore > g_vhPendingSignalSemaphores[3];

uint64_t vhCmdListFlush( nvrhi::CommandQueue type )
{
    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );

    vhProfile( "vhFlush", true );

    // Both queues depend on copy; flush copy first
    if ( type == nvrhi::CommandQueue::Graphics || type == nvrhi::CommandQueue::Compute )
    {
        vhProfile( "vhFlush_Copy", true );
        vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( nvrhi::CommandQueue::Copy );
        vhProfile( "vhFlush_Copy", false );
    }

    // Graphics depends on compute; flush compute first
    if ( type == nvrhi::CommandQueue::Graphics )
    {
        vhProfile( "vhFlush_Compute", true );
        vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( nvrhi::CommandQueue::Compute );
        vhProfile( "vhFlush_Compute", false );
    }

    // Flush the requested queue
    uint64_t instance = vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( type );

    vhProfile( "vhFlush", false );
    return instance;
}

void vhCmdListFlushTransferIfNeeded()
{
    const uint64_t transferSizeThreshold = 1024 * 1024 * 16; // 16 MB
    if ( g_vhCmdListTransferSizeHeuristic > transferSizeThreshold )
    {
        vhCmdListFlush( nvrhi::CommandQueue::Copy );
        g_vhCmdListTransferSizeHeuristic = 0;
    }
}

// ------------ Heap Management ------------

vhHeap vhAllocHeap()
{
    std::lock_guard< std::mutex > lock( g_vhHeapIDListMutex );
    uint32_t id = g_vhHeapIDList.alloc();
    g_vhHeapIDValid[id] = true;
    return id;
}

bool vhCreateHeap( vhHeap heap, uint64_t size, const char* name )
{
    if ( heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateHeap(): Invalid heap handle\n" );
        return false;
    }
    if ( size == 0 )
    {
        VRHI_ERR( "vhCreateHeap(): Invalid size 0\n" );
        return false;
    }

    auto cmd = vhCmdAlloc<VIDL_vhCreateHeap>( heap, size, name );
    assert( cmd );
    vhCmdEnqueue( cmd );
    return true;
}

void vhDestroyHeap( vhHeap heap )
{
    if ( heap == VRHI_INVALID_HANDLE ) return;

    std::lock_guard< std::mutex > lock( g_vhHeapIDListMutex );
    if ( g_vhHeapIDValid.find( heap ) == g_vhHeapIDValid.end() )
    {
        // Invalid heap handle
        return;
    }

    g_vhHeapIDValid.erase( heap );
    g_vhHeapIDList.release( heap );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyHeap>( heap );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhBindTextureMemory( vhTexture texture, vhHeap heap, uint64_t offset )
{
    if ( texture == VRHI_INVALID_HANDLE || heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhBindTextureMemory(): Invalid texture or heap handle\n" );
        return;
    }

    auto cmd = vhCmdAlloc<VIDL_vhBindTextureMemory>( texture, heap, offset );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

glm::u64vec2 vhGetTextureMemoryRequirements( vhTexture texture )
{
    if ( texture == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhGetTextureMemoryRequirements(): Invalid texture handle\n" );
        return glm::u64vec2( 0 );
    }
    return vhBackendQueryTextureMemoryRequirements( texture );
}

glm::u64vec2 vhHeapAlloc( vhHeap heap, uint64_t size, uint64_t alignment )
{
    if ( heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhHeapAlloc(): Invalid heap handle\n" );
        return glm::u64vec2( 0 );
    }
    if ( size == 0 )
    {
        VRHI_ERR( "vhHeapAlloc(): Invalid size 0\n" );
        return glm::u64vec2( 0 );
    }
    if ( alignment == 0 )
    {
        VRHI_ERR( "vhHeapAlloc(): Invalid alignment 0\n" );
        return glm::u64vec2( 0 );
    }
    return vhBackendAllocTextureMemory( heap, size, alignment );
}

void vhHeapFree( vhHeap heap, uint64_t offset )
{
    if ( heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhHeapFree(): Invalid heap handle\n" );
        return;
    }
    vhBackendFreeTextureMemory( heap, offset );
}

glm::u64vec2 vhAllocBindTextureMemory( vhTexture texture, vhHeap heap )
{
    if ( texture == VRHI_INVALID_HANDLE || heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhAllocBindTextureMemory(): Invalid texture or heap handle\n" );
        return glm::u64vec2( 0 );
    }

    glm::u64vec2 requirements = vhGetTextureMemoryRequirements( texture );
    if ( requirements.y == 0 )
    {
        VRHI_ERR( "vhAllocBindTextureMemory(): Invalid texture memory requirements\n" );
        return glm::u64vec2( 0 );
    }

    glm::u64vec2 allocation = vhHeapAlloc( heap, requirements.x, requirements.y );
    if ( allocation.y == 0 )
    {
        return allocation;
    }

    vhBindTextureMemory( texture, heap, allocation.x );
    return allocation;
}

glm::u64vec2 vhGetBufferMemoryRequirements( vhBuffer buffer )
{
    if ( buffer == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhGetBufferMemoryRequirements(): Invalid buffer handle\n" );
        return glm::u64vec2( 0 );
    }
    return vhBackendQueryBufferMemoryRequirements( buffer );
}

glm::u64vec2 vhGetAccelStructMemoryRequirements( vhAccelStruct as )
{
    if ( as == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhGetAccelStructMemoryRequirements(): Invalid accel struct handle\n" );
        return glm::u64vec2( 0 );
    }
    return vhBackendQueryAccelStructMemoryRequirements( as );
}

void vhBindBufferMemory( vhBuffer buffer, vhHeap heap, uint64_t offset )
{
    if ( buffer == VRHI_INVALID_HANDLE || heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhBindBufferMemory(): Invalid buffer or heap handle\n" );
        return;
    }

    auto cmd = vhCmdAlloc<VIDL_vhBindBufferMemory>( buffer, heap, offset );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

glm::u64vec2 vhAllocBindBufferMemory( vhBuffer buffer, vhHeap heap )
{
    if ( buffer == VRHI_INVALID_HANDLE || heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhAllocBindBufferMemory(): Invalid buffer or heap handle\n" );
        return glm::u64vec2( 0 );
    }

    glm::u64vec2 requirements = vhGetBufferMemoryRequirements( buffer );
    if ( requirements.y == 0 )
    {
        VRHI_ERR( "vhAllocBindBufferMemory(): Invalid buffer memory requirements\n" );
        return glm::u64vec2( 0 );
    }

    glm::u64vec2 allocation = vhHeapAlloc( heap, requirements.x, requirements.y );
    if ( allocation.y == 0 )
    {
        return allocation;
    }

    vhBindBufferMemory( buffer, heap, allocation.x );
    return allocation;
}

void vhCmdListFlushAll_DeviceStateLocked()
{
    // WARNING: Lock g_nvRHIStateMutex before calling this.
    // The order here matters slightly for efficiency ( Flush upsteam first ),
    // but the actual dependency correctness is handled by the waits inserted inside vhCmdListFlush.
    vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( nvrhi::CommandQueue::Copy );
    vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( nvrhi::CommandQueue::Compute );
    vhCmdListFlush_SingleQueueInternal_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
}

void vhCmdListFlushAll()
{
    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
    vhCmdListFlushAll_DeviceStateLocked();
}

// Keep in sync with the literal "..." passed to vhProfile()/VRHI_PROFILE_SCOPE() across src/.
// VRHI_PROFILE_FUNCTION() scopes use __FUNCTION__ and are intentionally omitted.
static const char* const g_vhProfileScopeNames[] =
{
    "vhFlush",
    "vhFlush_Copy",
    "vhFlush_Compute",
    "vhFlush_Wait",
    "vhFinish_Wait",
    "vhFrame_Present",
    "vhFrame_WaitSemaphore",
    "vhFrame_AcquireNextImage",
    "Handle_vhFlushInternal_WaitForGPU",
    "Handle_vhFlushInternal_GC",
    "BE_UpdateTexture_Calc",
    "BE_UpdateTexture_Write",
    "BE_BlitTexture_SliceSetup",
    "BE_BlitTexture_Execute",
    "BE_ReadTextureSlow_StagingCreate",
    "BE_ReadTextureSlow_Copy",
    "BE_ReadTextureSlow_Map",
    "BE_ReadTextureSlow_CopyCPU",
    "BE_ReadTextureSlow_Unmap",
    "BE_ResizeBuffer_Create",
    "BE_ResizeBuffer_Copy",
    "BE_UpdateBuffer_ResizeCheck",
    "BE_UpdateBuffer_Write",
    "BE_PresubmitCommon_PipelineDesc_ShaderHandles",
    "BE_PresubmitCommon_PipelineDesc_RenderState",
    "BE_PresubmitCommon_PipelineDesc_VertexLayout",
    "BE_PreSubmitCommon_ResolveStateCache_Textures",
    "BE_PreSubmitCommon_ResolveStateCache_Shaders",
    "BE_PreSubmitCommon_ResolveStateCache_Buffers",
    "BE_PreSubmitCommon_ResolveStateCache_AccelStructs",
    "BE_PreSubmitCommon_State_PSOLayoutHash",
    "BE_PreSubmitCommon_State_ShaderLayoutMatch",
    "BE_PreSubmitCommon_State_ResolveCache",
    "BE_PreSubmitCommon_State_BindingSetBuild",
    "BE_PreSubmitCommon_State_GraphicsStateSetup",
    "BE_Dispatch_PipelineDesc",
    "BE_Dispatch_PSOCache",
    "BE_Dispatch_StateSetup",
    "BE_Dispatch_PushConstants",
    "BE_Dispatch_Execute",
    "BE_DispatchIndirect_Validation",
    "BE_DispatchIndirect_PipelineDesc",
    "BE_DispatchIndirect_PSOCache",
    "BE_DispatchIndirect_StateSetup",
    "BE_DispatchIndirect_SetParams",
    "BE_DispatchIndirect_PushConstants",
    "BE_DispatchIndirect_Execute",
    "BE_Submit_PipelineDesc",
    "BE_Submit_GetFramebuffer",
    "BE_Submit_PSOCache",
    "BE_Submit_StateSetup",
    "BE_Submit_PushConstants",
    "BE_Submit_Execute",
    "BE_BlitBuffer_Execute",
    "BE_DispatchRays_ShaderSetup",
    "BE_DispatchRays_StateSetup",
    "BE_DispatchRays_Execute",
};

void vhEnumerateProfileScopes( void (*cb)( const char* name, void* user ), void* user )
{
    if ( !cb ) return;
    for ( const char* name : g_vhProfileScopeNames ) cb( name, user );
}

// Global states for user convenience.
vhState g_state0;
vhState g_state1;

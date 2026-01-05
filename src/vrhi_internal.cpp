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

VkInstance g_vulkanInstance = VK_NULL_HANDLE;
VkPhysicalDevice g_vulkanPhysicalDevice = VK_NULL_HANDLE;
VkDevice g_vulkanDevice = VK_NULL_HANDLE;
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

// Shader
vhAllocatorObjectFreeList g_vhShaderIDList( 16 * 1024 );
std::unordered_map< vhShader, bool > g_vhShaderIDValid;
std::mutex g_vhShaderIDListMutex;


bool g_vhRayTracingEnabled = false;

// # Backend Command List Thread

moodycamel::BlockingConcurrentQueue< void* > g_vhCmds( 32 * 1024 );
std::atomic<bool> g_vhCmdsQuit = false;
std::thread g_vhCmdThread;
std::atomic<bool> g_vhCmdThreadReady = false;
std::vector< vhMem* > g_vhMemList;
std::mutex g_vhMemListMutex;

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

void vhCmdEnqueue( void* cmd )
{
    for ( int i = 0; i < 128; i++ )
    {
        if ( g_vhCmds.try_enqueue( cmd ) ) return;
        std::this_thread::yield();
    }
    g_vhCmds.enqueue( cmd );
}

nvrhi::CommandListHandle g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Count] = { nullptr, nullptr, nullptr };
uint64_t g_vhCmdListTransferSizeHeuristic = 0;

nvrhi::CommandListHandle vhCmdListGet( nvrhi::CommandQueue type )
{
    auto typeIdx = ( uint64_t ) type;
    if ( !g_vhCmdLists[typeIdx] )
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        nvrhi::CommandListParameters params = { .queueType = ( nvrhi::CommandQueue ) type };
        g_vhCmdLists[typeIdx] = g_vhDevice->createCommandList( params );
        g_vhCmdLists[typeIdx]->open();
    }
    return g_vhCmdLists[typeIdx];
}

// Returns the instance ID of the executed command list.
// Automatically inserts semaphore waits for downstream queues:
// - Copy feeds Compute and Graphics
// - Compute feeds Graphics
//
void vhCmdListFlush_SingleQueueInternal( nvrhi::CommandQueue type )
{
    auto typeIdx = ( uint64_t ) type;
    uint64_t instance = 0;

    if ( g_vhCmdLists[typeIdx] )
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        g_vhCmdLists[typeIdx]->close();

        // Execute and get the instance ID for synchronisation
        instance = g_vhDevice->executeCommandList( g_vhCmdLists[typeIdx], type );
        g_vhCmdLists[typeIdx] = nullptr;

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
}

void vhCmdListFlush( nvrhi::CommandQueue type )
{
    // Both queues depend on copy; flush copy first
    if ( type == nvrhi::CommandQueue::Graphics || type == nvrhi::CommandQueue::Compute )
    {
        vhCmdListFlush_SingleQueueInternal( nvrhi::CommandQueue::Copy );
    }

    // Graphics depends on compute; flush compute first
    if ( type == nvrhi::CommandQueue::Graphics )
    {
        vhCmdListFlush_SingleQueueInternal( nvrhi::CommandQueue::Compute );
    }

    // Flush the requested queue
    vhCmdListFlush_SingleQueueInternal( type );
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

void vhCmdListFlushAll()
{
    // The order here matters slightly for efficiency ( Flush upsteam first ),
    // but the actual dependency correctness is handled by the waits inserted inside vhCmdListFlush.
    vhCmdListFlush_SingleQueueInternal( nvrhi::CommandQueue::Copy );
    vhCmdListFlush_SingleQueueInternal( nvrhi::CommandQueue::Compute );
    vhCmdListFlush_SingleQueueInternal( nvrhi::CommandQueue::Graphics );
}

// Global states for user convenience.
vhState g_state0;
vhState g_state1;

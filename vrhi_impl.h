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
    #ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
        #include <windows.h>
    #endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_win32.h>
#else
    #include <vulkan/vulkan.h>
#endif

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
#include <vector>
#include <cstring>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <climits>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <concurrentqueue/blockingconcurrentqueue.h>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

// Required by NVRHI Vulkan backend
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "vrhi.h"
#include "vrhi_generated.h"
#include "vrhi_utils.h"

// --------------------------------------------------------------------------
// Internal Declarations
// --------------------------------------------------------------------------

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

// Command Queue
extern moodycamel::BlockingConcurrentQueue< void* > g_vhCmds;
extern std::atomic<bool> g_vhCmdsQuit;
extern std::thread g_vhCmdThread;
extern std::atomic<bool> g_vhCmdThreadReady;
extern std::vector< std::vector<uint8_t>* > g_vhMemList;
extern std::mutex g_vhMemListMutex;
extern uint64_t g_vhCmdListTransferSizeHeuristic;

// Backend state struct forward declaration
struct vhCmdBackendState;
// Backend state - visible here because the RHI thread needs it
extern vhCmdBackendState g_vhCmdBackendState; 


// Logging helper
void vhLog( bool error, const char* fmt, ... );
#define VRHI_LOG( fmt, ... ) vhLog( false, fmt, ##__VA_ARGS__ )
#define VRHI_ERR( fmt, ... ) vhLog( true, fmt, ##__VA_ARGS__ )

// Command function templates
template< typename T, typename... Args >
T* vhCmdAlloc( Args&&... args ) { return new T( std::forward<Args>(args)... ); }

template< typename T >
void vhCmdRelease( T* cmd ) { if (cmd) delete cmd; }

void vhCmdEnqueue( void* cmd );
void vhCmdListFlushAll();
void vhCmdListFlushTransferIfNeeded();

// Command Lists
extern nvrhi::CommandListHandle g_vhCmdLists[(uint64_t) nvrhi::CommandQueue::Count];
nvrhi::CommandListHandle vhCmdListGet( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics );
void vhCmdListFlush( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics );

struct vhVertexLayoutDef
{
    std::string semantic;
    std::string type;
    int semanticIndex = 0;
    int componentCount = 0;
    int offset = 0;
};
bool vhParseVertexLayoutInternal( const vhVertexLayout& layout, std::vector< vhVertexLayoutDef >& outDefs );
int vhVertexLayoutDefSize( const vhVertexLayoutDef& def );
int vhVertexLayoutDefSize( const std::vector< vhVertexLayoutDef >& def );


// --------------------------------------------------------------------------
// Submodule Includes
// --------------------------------------------------------------------------

#ifdef VRHI_IMPLEMENTATION
#include "vrhi_impl_backend.h"
#include "vrhi_impl_device.h"
#include "vrhi_impl_texture.h"
#include "vrhi_impl_buffer.h"
#endif


// --------------------------------------------------------------------------
// Definitions
// --------------------------------------------------------------------------

#ifdef VRHI_IMPL_DEFINITIONS

// Global Variable Definitions
vhInitData g_vhInit;
nvrhi::DeviceHandle g_vhDevice = nullptr;
std::atomic<int32_t> g_vhErrorCounter = 0;

VkInstance g_vulkanInstance = VK_NULL_HANDLE;
VkPhysicalDevice g_vulkanPhysicalDevice = VK_NULL_HANDLE;
VkDevice g_vulkanDevice = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT g_vulkanDebugMessenger = VK_NULL_HANDLE;
uint32_t g_vulkanEnabledExtensionCount = 0;
std::mutex g_nvRHIStateMutex;

VkQueue g_vulkanGraphicsQueue = VK_NULL_HANDLE;
VkQueue g_vulkanComputeQueue = VK_NULL_HANDLE;
VkQueue g_vulkanTransferQueue = VK_NULL_HANDLE;

uint32_t g_QueueFamilyGraphics = (uint32_t)-1;
uint32_t g_QueueFamilyCompute = (uint32_t)-1;
uint32_t g_QueueFamilyTransfer = (uint32_t)-1;

vhAllocatorObjectFreeList g_vhTextureIDList( 256 );
std::unordered_map< vhTexture, bool > g_vhTextureIDValid;
std::mutex g_vhTextureIDListMutex;

moodycamel::BlockingConcurrentQueue< void* > g_vhCmds( 32 * 1024 );
std::atomic<bool> g_vhCmdsQuit = false;
std::thread g_vhCmdThread;
std::atomic<bool> g_vhCmdThreadReady = false;
std::vector< vhMem* > g_vhMemList;
std::mutex g_vhMemListMutex;

#ifndef VRHI_SHARDED_BUILD
vhCmdBackendState g_vhCmdBackendState;
#endif

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

nvrhi::CommandListHandle g_vhCmdLists[(uint64_t) nvrhi::CommandQueue::Count] = { nullptr, nullptr, nullptr };
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
void vhCmdListFlush( nvrhi::CommandQueue type )
{
    auto typeIdx = ( uint64_t ) type;
    uint64_t instance = 0;

    if ( g_vhCmdLists[typeIdx] )
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        g_vhCmdLists[typeIdx]->close();
        
        // Execute and get the instance ID for synchronization
        instance = g_vhDevice->executeCommandList( g_vhCmdLists[typeIdx], type );
        g_vhCmdLists[typeIdx] = nullptr;
        
        // Automatic Synchronization
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
    
    vhCmdListFlush( nvrhi::CommandQueue::Copy );
    vhCmdListFlush( nvrhi::CommandQueue::Compute );
    vhCmdListFlush( nvrhi::CommandQueue::Graphics );
}

#endif // VRHI_IMPL_DEFINITIONS

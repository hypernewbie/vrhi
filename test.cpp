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

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif
#include <chrono>
#define _CRT_SECURE_NO_WARNINGS
#if !defined(VRHI_SHARDED_BUILD)
    #define VRHI_IMPLEMENTATION
#endif
#include "utest.h"

#include "vrhi.h"
#include "vrhi_utils.h"

// Internal benchmark functions from vrhi_impl.h


UTEST( Vrhi, Dummy )
{
    ASSERT_TRUE( true );
}

static bool g_testInit = false;

extern int vhVKRatePhysicalDeviceProps_Internal( const VkPhysicalDeviceProperties& props );
extern uint32_t vhVKFindDedicatedQueue_Internal( uint32_t qCount, const VkQueueFamilyProperties* qProps, VkQueueFlags required, VkQueueFlags avoid );
extern std::string vhGetDeviceInfo();

UTEST(RHI, DeviceRating) {
    VkPhysicalDeviceProperties props = {};
    props.apiVersion = VK_API_VERSION_1_3;
    
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    EXPECT_EQ( vhVKRatePhysicalDeviceProps_Internal( props ), 3 );

    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    EXPECT_EQ( vhVKRatePhysicalDeviceProps_Internal( props ), 2 );

    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    EXPECT_EQ( vhVKRatePhysicalDeviceProps_Internal( props ), 0 );

    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    EXPECT_EQ( vhVKRatePhysicalDeviceProps_Internal( props ), 1 );

    // Test API version gate (require 1.1+)
    props.apiVersion = VK_MAKE_VERSION( 1, 0, 0 );
    props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    EXPECT_EQ( vhVKRatePhysicalDeviceProps_Internal( props ), 0 );

    props.apiVersion = VK_MAKE_VERSION( 1, 1, 0 );
    EXPECT_EQ( vhVKRatePhysicalDeviceProps_Internal( props ), 3 );
}

UTEST( RHI, FindQueue )
{
    struct MockQueueFamily
    {
        VkQueueFlags flags;
        uint32_t count;
    };
    std::vector<VkQueueFamilyProperties> qProps;

    auto SetupMockQueues = [&]( const std::vector<MockQueueFamily>& families )
    {
        qProps.clear();
        for ( const auto& f : families )
        {
            VkQueueFamilyProperties p = {};
            p.queueFlags = f.flags;
            p.queueCount = f.count;
            qProps.push_back( p );
        }
    };

    auto FindQueue = [&]( VkQueueFlags required, VkQueueFlags avoid ) -> uint32_t
    {
        return vhVKFindDedicatedQueue_Internal( ( uint32_t ) qProps.size(), qProps.data(), required, avoid );
    };

    {
        // SCOPED_TRACE( "NVIDIA-like" );
        SetupMockQueues( {
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 16 },
            { VK_QUEUE_TRANSFER_BIT, 2 }
        } );
        EXPECT_EQ( FindQueue( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0 ), 0 );
        EXPECT_EQ( FindQueue( VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT ), ( uint32_t ) -1 );
        EXPECT_EQ( FindQueue( VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ), 1 );
    }

    {
        // SCOPED_TRACE( "AMD-like" );
        SetupMockQueues( {
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 },
            { VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 4 },
            { VK_QUEUE_TRANSFER_BIT, 2 }
        } );
        EXPECT_EQ( FindQueue( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0 ), 0 );
        EXPECT_EQ( FindQueue( VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT ), 1 );
        EXPECT_EQ( FindQueue( VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ), 2 );
    }

    {
        // SCOPED_TRACE( "Intel iGPU-like" );
        SetupMockQueues( {
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 }
        } );
        EXPECT_EQ( FindQueue( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0 ), 0 );
        EXPECT_EQ( FindQueue( VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT ), ( uint32_t ) -1 );
        EXPECT_EQ( FindQueue( VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ), ( uint32_t ) -1 );
    }

    {
        // SCOPED_TRACE( "MoltenVK-like" );
        SetupMockQueues( {
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 },
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 },
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 },
            { VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 }
        } );
        EXPECT_EQ( FindQueue( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0 ), 0 );
        EXPECT_EQ( FindQueue( VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT ), ( uint32_t ) -1 );
    }

    {
        // SCOPED_TRACE( "Negative: No graphics" );
        SetupMockQueues( {
            { VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 4 },
            { VK_QUEUE_TRANSFER_BIT, 2 }
        } );
        EXPECT_EQ( FindQueue( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0 ), ( uint32_t ) -1 );
    }

    {
        // SCOPED_TRACE( "Negative: Empty queues" );
        SetupMockQueues( {} );
        EXPECT_EQ( FindQueue( VK_QUEUE_GRAPHICS_BIT, 0 ), ( uint32_t ) -1 );
    }

    {
        // SCOPED_TRACE( "Edge: Prefer most dedicated" );
        SetupMockQueues( {
            { VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT, 1 },
            { VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 },
            { VK_QUEUE_COMPUTE_BIT, 1 }
        } );
        EXPECT_EQ( FindQueue( VK_QUEUE_COMPUTE_BIT, 0 ), 2 );
    }
}

UTEST( RHI, Init )
{
    // If global init is active, shut it down to test clean init
    if ( g_testInit )
    {
        vhShutdown();
        g_testInit = false;
    }

    // Test init
    g_vhInit.debug = true;
    vhInit();

    // Verify globals
    EXPECT_NE( g_vhDevice.Get(), nullptr );

    // Test GetInfo returns something
    std::string info = vhGetDeviceInfo();
    EXPECT_FALSE( info.empty() );
    EXPECT_TRUE( info.find( "Device:" ) != std::string::npos );

    // Test shutdown
    vhShutdown();
    EXPECT_EQ( g_vhDevice.Get(), nullptr );

    // Test GetInfo after shutdown
    info = vhGetDeviceInfo();
    EXPECT_TRUE( info.find( "not initialized" ) != std::string::npos );
}

extern std::atomic<int32_t> g_vhErrorCounter;

UTEST( RHI, LogCallback )
{
    // Ensure clean state
    if ( g_testInit )
    {
        vhShutdown();
        g_testInit = false;
    }

    std::vector<std::string> logs;
    int errorCount = 0;

    g_vhInit.debug = true;
    g_vhInit.fnLogCallback = [&]( bool err, const std::string& msg ) {
        if ( err ) errorCount++;
        logs.push_back( msg );
    };

    vhInit();

    // Verify we captured logs
    EXPECT_GT( logs.size(), 0 ); // "Initialising Vulkan RHI ..." etc
    
    // Check if we captured expected startup messages
    bool foundInit = false;
    for ( const auto& l : logs )
    {
        if ( l.find( "Initialising Vulkan RHI" ) != std::string::npos ) foundInit = true;
    }
    EXPECT_TRUE( foundInit );

    // Verify error counting (should be 0 on clean init)
    EXPECT_EQ( errorCount, 0 );
    EXPECT_EQ( g_vhErrorCounter.load(), 0 );


    vhShutdown();
    
    // Verify shutdown logs
    bool foundShutdown = false;
    for ( const auto& l : logs )
    {
        if ( l.find( "Shutdown Vulkan RHI" ) != std::string::npos ) foundShutdown = true;
    }
    EXPECT_TRUE( foundShutdown );

    // Cleanup callback
    g_vhInit.fnLogCallback = nullptr;
}

UTEST( Texture, CreateDestroyError )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    // Create a texture with INVALID dimensions (-1 implies invalid)
    vhCreateTexture(
        tex,
        nvrhi::TextureDimension::Texture2D,
        glm::ivec3( -1, -5, 1 ), // Invalid size
        1, 1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB,
        nullptr
    );

    vhFlush();

    // Verify error was incremented
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    vhDestroyTexture( tex );
}

UTEST( Texture, CreateHelpers )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    // 2D
    vhTexture tex2D = vhAllocTexture();
    vhCreateTexture2D( tex2D, glm::ivec2( 128, 128 ), 1, nvrhi::Format::RGBA8_UNORM );

    // 3D
    vhTexture tex3D = vhAllocTexture();
    vhCreateTexture3D( tex3D, glm::ivec3( 32, 32, 32 ), 1, nvrhi::Format::RGBA8_UNORM );

    // Cube
    vhTexture texCube = vhAllocTexture();
    vhCreateTextureCube( texCube, 128, 1, nvrhi::Format::RGBA8_UNORM );

    // 2D Array
    vhTexture tex2DArray = vhAllocTexture();
    vhCreateTexture2DArray( tex2DArray, glm::ivec2( 128, 128 ), 4, 1, nvrhi::Format::RGBA8_UNORM );

    // Cube Array
    vhTexture texCubeArray = vhAllocTexture();
    vhCreateTextureCubeArray( texCubeArray, 128, 12, 1, nvrhi::Format::RGBA8_UNORM );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyTexture( tex2D );
    vhDestroyTexture( tex3D );
    vhDestroyTexture( texCube );
    vhDestroyTexture( tex2DArray );
    vhDestroyTexture( texCubeArray );
}

UTEST( Allocator, FreeList )
{
    vhAllocatorObjectFreeList allocator( 10 );

    // Test initial allocations
    EXPECT_EQ( allocator.alloc(), 0 );
    EXPECT_EQ( allocator.alloc(), 1 );
    EXPECT_EQ( allocator.alloc(), 2 );

    // Test release and reuse
    allocator.release( 1 );
    EXPECT_EQ( allocator.alloc(), 1 ); // Should reuse 1

    // Test sequential fill
    for ( int i = 3; i < 10; ++i )
    {
        EXPECT_EQ( allocator.alloc(), i );
    }

    // Test overflow
    EXPECT_EQ( allocator.alloc(), -1 );

    // Test release and re-fill
    allocator.release( 5 );
    allocator.release( 0 );
    EXPECT_EQ( allocator.alloc(), 0 ); // LIFO usually (stack behavior in implementation: push_back/pop_back)
    EXPECT_EQ( allocator.alloc(), 5 );

    // Test purge
    allocator.purge();
    EXPECT_EQ( allocator.alloc(), 0 );

    // Test invalid inputs
    EXPECT_EQ( allocator.alloc( 0 ), -1 );
    EXPECT_EQ( allocator.alloc( 2 ), -1 ); // Only size 1 supported
    EXPECT_EQ( allocator.alloc( 1, 1 ), -1 ); // Alignment not supported
}

UTEST( Texture, CreateDestroy )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    vhCreateTexture(
        tex,
        nvrhi::TextureDimension::Texture2D,
        glm::ivec3( 256, 256, 1 ),
        1, 1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB, // Some default flag
        nullptr // No data
    );

    vhDestroyTexture( tex );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

UTEST( Texture, CreateDestroyStressTest )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    // Create a predictable RNG for determinism
    std::srand( 12345 );

    const int kNumTextures = 127;
    std::vector<vhTexture> textures;

    // Allocate & Create
    for ( int i = 0; i < kNumTextures; ++i )
    {
        vhTexture tex = vhAllocTexture();
        EXPECT_NE( tex, VRHI_INVALID_HANDLE );
        textures.push_back( tex );

        // Random 8..1024
        int w = 8 + ( std::rand() % 1017 );
        int h = 8 + ( std::rand() % 1017 );

        vhCreateTexture(
            tex,
            nvrhi::TextureDimension::Texture2D,
            glm::ivec3( w, h, 1 ),
            1, 1,
            nvrhi::Format::RGBA8_UNORM,
            VRHI_TEXTURE_SRGB,
            nullptr
        );
    }

    // Destroy in random order ideally, but linear is fine for basic stress
    for ( vhTexture t : textures )
    {
        vhDestroyTexture( t );
    }

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

UTEST_STATE();

int main( int argc, const char* const argv[] )
{
    int result = utest_main( argc, argv );

    if ( g_testInit )
    {
        vhShutdown();
        g_testInit = false;
    }

    return result;
}

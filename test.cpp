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
#include "utest.h"

#define VRHI_IMPLEMENTATION
#include "vrhi.h"

UTEST( Vrhi, Dummy )
{
    ASSERT_TRUE( true );
}

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

UTEST_MAIN()

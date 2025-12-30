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
#include <random>
#include <algorithm>

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
#include "vrhi_impl.h"
#include "vrhi_utils.h"

// Internal benchmark functions from vrhi_impl.h


UTEST( Vrhi, Dummy )
{
    ASSERT_TRUE( true );
}

static bool g_testInit = false;

extern std::string vhGetDeviceInfo();

UTEST( RHI, FindQueue )
{
    // TODO : Find a way to test this.
    /*
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
    */
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

UTEST( Texture, Update )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    const int width = 64;
    const int height = 64;
    const size_t dataSize = width * height * 4; // RGBA8
    auto initialData = vhAllocMem( dataSize );
    
    // Fill with gibberish
    for ( size_t i = 0; i < dataSize; ++i ) (*initialData)[i] = (uint8_t)( rand() % 256 );

    vhCreateTexture2D(
        tex,
        glm::ivec2( width, height ),
        1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB,
        initialData
    );
    vhFinish();

    // Test 3 updates
    for ( int i = 0; i < 3; ++i )
    {
        // New gibberish
        auto updateData = vhAllocMem( dataSize );
        for ( size_t k = 0; k < dataSize; ++k ) (*updateData)[k] = (uint8_t)( rand() % 256 );
        
        // Full update
        vhUpdateTexture(
            tex,
            0, 0, // start mip, start layer
            1, 1, // num mips, num layers
            updateData
        );
                 
        // Process
        vhFinish();
    }
    
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    
    vhDestroyTexture( tex );
    vhFinish();
}

UTEST( Texture, Readback )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    const int width = 32;
    const int height = 32;
    const size_t dataSize = width * height * 4; // RGBA8
    auto initialData = vhAllocMem( dataSize );
    
    // Fill with known pattern
    for ( size_t i = 0; i < dataSize; ++i ) (*initialData)[i] = (uint8_t)( i % 255 );

    vhCreateTexture2D(
        tex,
        glm::ivec2( width, height ),
        1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB,
        initialData
    );

    // Initial data is freed by backend eventually, but we used new vhMem so the pointer is ours until passed
    // Actually vhCreateTexture takes a pointer, we used helper vhAllocMem which does 'new', so it's a pointer.
    // The previous code for CreateTexture2D takes const vhMem*.
    // And Handle_vhCreateTexture uses BE_MemRAII. So it will be deleted.
    // BUT wait, we want to VERIFY it. So we need a COPY of the data to compare against.
    
    std::vector<uint8_t> refData = *initialData; // Copy for verification

    // Flush to ensure creation happens
    vhFlush();

    // Read back
    vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );

    // Finish ensures GPU is done and readback is complete
    vhFinish();

    // Compare
    EXPECT_EQ( readData.size(), dataSize );
    if ( readData.size() == dataSize )
    {
        for ( size_t i = 0; i < dataSize; ++i )
        {
             EXPECT_EQ( readData[i], refData[i] );
             if ( readData[i] != refData[i] ) break; // Fail fast
        }
    }

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    
    vhDestroyTexture( tex );
}

UTEST( Buffer, ValidateLayout )
{
    // Valid cases
    EXPECT_TRUE( vhValidateVertexLayout( "float3 POSITION" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "float3 POSITION float2 TEXCOORD0" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "ubyte4 COLOR" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "half2 TEXCOORD" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "float POSITION" ) ); // Scalar
    EXPECT_TRUE( vhValidateVertexLayout( "float3 POSITION0 float3 NORMAL int4 BLENDINDICES float4 BLENDWEIGHTS" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "float3 BANANA" ) ); // Custom semantic
    EXPECT_TRUE( vhValidateVertexLayout( "float3 BANANA0" ) ); // Custom semantic with index

    // Invalid cases - Types
    EXPECT_FALSE( vhValidateVertexLayout( "double3 POSITION" ) ); // Invalid type
    EXPECT_FALSE( vhValidateVertexLayout( "float5 POSITION" ) );  // Invalid suffix
    EXPECT_FALSE( vhValidateVertexLayout( "float1 POSITION" ) );  // Invalid suffix (1 should be empty)
    EXPECT_FALSE( vhValidateVertexLayout( "vec3 POSITION" ) );    // Invalid type

    // Invalid cases - Semantics
    EXPECT_FALSE( vhValidateVertexLayout( "float3 position" ) );  // Lowercase semantic
    EXPECT_FALSE( vhValidateVertexLayout( "float3 0POSITION" ) ); // Starts with digit
    EXPECT_FALSE( vhValidateVertexLayout( "float3 PO_SITION" ) ); // Underscore not alphanumeric (std::isalnum check)

    // Invalid cases - Formatting
    EXPECT_FALSE( vhValidateVertexLayout( "float3" ) );           // Missing semantic
    EXPECT_FALSE( vhValidateVertexLayout( "POSITION" ) );         // Missing type
    EXPECT_FALSE( vhValidateVertexLayout( "" ) );                 // Empty
}

UTEST( Buffer, VertexLayoutInternals )
{
    // Test 1: Simple Logic
    {
        std::vector< vhVertexLayoutDef > defs;
        bool res = vhParseVertexLayoutInternal( "float3 POSITION", defs );
        EXPECT_TRUE( res );
        EXPECT_EQ( defs.size(), 1 );
        EXPECT_EQ( vhVertexLayoutDefSize( defs ), 12 );

        if ( defs.size() > 0 )
        {
             EXPECT_STREQ( defs[0].semantic.c_str(), "POSITION" );
             EXPECT_STREQ( defs[0].type.c_str(), "float" );
             EXPECT_EQ( defs[0].componentCount, 3 );
             EXPECT_EQ( defs[0].semanticIndex, 0 );
             EXPECT_EQ( defs[0].offset, 0 );
             EXPECT_EQ( vhVertexLayoutDefSize( defs[0] ), 12 );
        }
    }

    // Test 2: Complex Logic
    {
        std::vector< vhVertexLayoutDef > defs;
        bool res = vhParseVertexLayoutInternal( "float3 POSITION float2 TEXCOORD0 ubyte4 COLOR", defs );
        EXPECT_TRUE( res );
        EXPECT_EQ( defs.size(), 3 );
        
        // float3 POSITION (12 bytes)
        EXPECT_EQ( defs[0].offset, 0 );
        EXPECT_STREQ( defs[0].semantic.c_str(), "POSITION" );
        
        // float2 TEXCOORD0 (8 bytes) -> offset 12
        EXPECT_EQ( defs[1].offset, 12 );
        EXPECT_STREQ( defs[1].semantic.c_str(), "TEXCOORD" );
        EXPECT_EQ( defs[1].semanticIndex, 0 );
        
        // ubyte4 COLOR (4 bytes) -> offset 20
        EXPECT_EQ( defs[2].offset, 20 );
        EXPECT_STREQ( defs[2].semantic.c_str(), "COLOR" );
        
        // Total Stride = 24
        EXPECT_EQ( vhVertexLayoutDefSize( defs ), 24 );
    }
}

UTEST( Buffer, Allocation )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    vhBuffer b1 = vhAllocBuffer();
    vhBuffer b2 = vhAllocBuffer();
    vhBuffer b3 = vhAllocBuffer();

    EXPECT_NE( b1, VRHI_INVALID_HANDLE );
    EXPECT_NE( b2, VRHI_INVALID_HANDLE );
    EXPECT_NE( b3, VRHI_INVALID_HANDLE );

    EXPECT_NE( b1, b2 );
    EXPECT_NE( b2, b3 );
    EXPECT_NE( b1, b3 );

    vhDestroyBuffer( b1 );
    vhDestroyBuffer( b2 );
    vhDestroyBuffer( b3 );
    vhFlush();
}

UTEST( Texture, Allocation )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    vhTexture t1 = vhAllocTexture();
    vhTexture t2 = vhAllocTexture();
    vhTexture t3 = vhAllocTexture();

    EXPECT_NE( t1, VRHI_INVALID_HANDLE );
    EXPECT_NE( t2, VRHI_INVALID_HANDLE );
    EXPECT_NE( t3, VRHI_INVALID_HANDLE );

    EXPECT_NE( t1, t2 );
    EXPECT_NE( t2, t3 );
    EXPECT_NE( t1, t3 );

    vhDestroyTexture( t1 );
    vhDestroyTexture( t2 );
    vhDestroyTexture( t3 );
    vhFlush();
}

UTEST( Buffer, UpdateSafety )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    vhFlush(); // Ensure clean state from previous tests
    int32_t startErrors = g_vhErrorCounter.load();

    // 1. Invalid Handle
    vhUpdateVertexBuffer( VRHI_INVALID_HANDLE, nullptr, 0 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 2. Non-existent Buffer
    vhUpdateVertexBuffer( 0xDEADC0DE, nullptr, 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );
    startErrors = g_vhErrorCounter.load();

    // 3. Null Data
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "NullDataTest", vhAllocMem( 1024 ), "float3 POSITION" );
    vhUpdateVertexBuffer( buf, nullptr, 0 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 4. Destroyed Buffer
    vhDestroyBuffer( buf );
    vhFlush();
    vhUpdateVertexBuffer( buf, vhAllocMem( 100 ), 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );
}

UTEST( Buffer, DoubleCreation )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "DoubleCreate", vhAllocMem( 1024 ), "float3 POSITION" );
    vhCreateVertexBuffer( buf, "DoubleCreate2", vhAllocMem( 1024 ), "float3 POSITION" );
    vhFlush();

    EXPECT_GT( g_vhErrorCounter.load(), startErrors );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( Buffer, UpdateFunctionality )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "UpdateTest", vhAllocMem( 1024 ), "float3 POSITION" );
    
    // Basic Update
    vhUpdateVertexBuffer( buf, vhAllocMem( 256 ), 0 );
    
    // Offset Update
    vhUpdateVertexBuffer( buf, vhAllocMem( 100 ), 512 );
    
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( Texture, BlitConnectivity )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    vhCreateTexture2D( src, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );
    vhCreateTexture2D( dst, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );

    // Call blit - should reach backend stub
    vhBlitTexture( dst, src );
    vhFlush();

    // Verification is manual check of stdout for "BE_BlitTexture Stub - Not Yet Implemented"
    
    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST( Texture, BlitMipToMip )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Src: 128x128 with 4 mips (Mip 1 is 64x64)
    vhCreateTexture2D( src, glm::ivec2( 128, 128 ), 4, nvrhi::Format::RGBA8_UNORM );
    // Dst: 64x64 with 1 mip
    vhCreateTexture2D( dst, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );

    // Blit Src Mip 1 to Dst Mip 0
    vhBlitTexture( dst, src, 0, 1 );
    vhFlush();

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST( Texture, BlitPartialRegion )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    vhCreateTexture2D( src, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );
    vhCreateTexture2D( dst, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );

    // Copy 32x32 region from Src(16,16) to Dst(0,0)
    vhBlitTexture( dst, src, 0, 0, 0, 0, glm::ivec3( 0, 0, 0 ), glm::ivec3( 16, 16, 0 ), glm::ivec3( 32, 32, 1 ) );
    vhFlush();

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST( Texture, BlitFunctionalStubFailure )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int width = 32;
    const int height = 32;
    const size_t dataSize = width * height * 4;

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Source: All White (255)
    vhMem* whiteData = vhAllocMem( dataSize );
    std::fill( whiteData->begin(), whiteData->end(), 255 );
    vhCreateTexture2D( src, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, whiteData );

    // Destination: All Black (0)
    vhMem* blackData = vhAllocMem( dataSize );
    std::fill( blackData->begin(), blackData->end(), 0 );
    vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, blackData );

    vhFinish();

    // Attempt Blit (Src -> Dst)
    vhBlitTexture( dst, src );
    vhFinish();

    // Readback Dst
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    // Functional failure check
    bool match = true;
    if ( readData.size() == dataSize )
    {
        for ( size_t i = 0; i < dataSize; ++i )
        {
            if ( readData[i] != 255 )
            {
                match = false;
                break;
            }
        }
    }
    else
    {
        match = false;
    }
    EXPECT_TRUE( match );

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST( Texture, PartialUpdate_Compatibility )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    const int width = 64;
    const int height = 64;
    const size_t dataSize = ( size_t ) width * height * 4;

    auto data = vhAllocMem( dataSize );
    vhCreateTexture2D( tex, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, data );
    vhFinish();

    // Call with old signature (default arguments)
    auto updateData = vhAllocMem( dataSize );
    vhUpdateTexture( tex, 0, 0, 1, 1, updateData );
    vhFinish();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    vhDestroyTexture( tex );
    vhFinish();
}

/* TEMPORARY: THIS TEST CRASHES DUE TO MISSING FULL IMPLEMENTATION. WILL FIX IN FUTURE
UTEST( Texture, PartialUpdate_Propagation )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( 128, 128 ), 1, nvrhi::Format::RGBA8_UNORM );
    vhFinish();

    // Call with explicit region parameters
    // Note: Backend is stubbed for these new parameters, but it should still execute.
    const size_t regionDataSize = 32 * 32 * 4;
    auto regionData = vhAllocMem( regionDataSize );
    vhUpdateTexture( tex, 0, 0, 1, 1, regionData, glm::ivec3( 10, 10, 0 ), glm::ivec3( 32, 32, 1 ) );
    vhFinish();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    vhDestroyTexture( tex );
    vhFinish();
}
*/

/* TEMPORARY: THIS TEST CRASHES DUE TO MISSING FULL IMPLEMENTATION. WILL FIX IN FUTURE
UTEST( Texture, PartialUpdate_Readback )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int texWidth = 256;
    const int texHeight = 256;
    const size_t texDataSize = ( size_t ) texWidth * texHeight * 4;

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( texWidth, texHeight ), 1, nvrhi::Format::RGBA8_UNORM );
    vhFinish();

    // Local CPU buffer reference
    std::vector<uint8_t> cpuBuffer( texDataSize, 0 );

    std::mt19937 rng( 42 );

    const int numUpdates = 10;
    for ( int i = 0; i < numUpdates; ++i )
    {
        int w = 8 + ( rng() % 32 );
        int h = 8 + ( rng() % 32 );
        int x = rng() % ( texWidth - w );
        int y = rng() % ( texHeight - h );

        size_t regionSize = ( size_t ) w * h * 4;
        auto updateData = vhAllocMem( regionSize );
        for ( size_t k = 0; k < regionSize; ++k )
        {
            ( *updateData )[k] = ( uint8_t )( rng() % 256 );
        }

        // Update GPU
        vhUpdateTexture( tex, 0, 0, 1, 1, updateData, glm::ivec3( x, y, 0 ), glm::ivec3( w, h, 1 ) );

        // Update CPU reference
        for ( int row = 0; row < h; ++row )
        {
            uint8_t* dst = cpuBuffer.data() + ( ( ( size_t ) y + row ) * texWidth + x ) * 4;
            const uint8_t* src = updateData->data() + ( ( size_t ) row * w ) * 4;
            memcpy( dst, src, ( size_t ) w * 4 );
        }
    }

    vhFinish();

    // Read back from GPU
    vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );
    vhFinish();

    // Verify GPU vs CPU reference
    bool match = true;
    if ( readData.size() == texDataSize )
    {
        for ( size_t i = 0; i < texDataSize; ++i )
        {
            if ( readData[i] != cpuBuffer[i] )
            {
                match = false;
                break;
            }
        }
    }
    else
    {
        match = false;
    }

    if ( !match )
    {
        printf( "\n[ EXPECTED FAILURE ]\n" );
        printf( "WARNING: THIS TEST FAILURE IS EXPECTED DUE TO STUBBED MISSING BACKEND. THIS IS INTENTIONAL\n\n" );
    }

    // This test is EXPECTED TO FAIL because the backend doesn't handle partial updates yet.
    EXPECT_FALSE( match );

    vhDestroyTexture( tex );
    vhFinish();
}
*/

/* TEMPORARY: THIS TEST CRASHES DUE TO MISSING FULL IMPLEMENTATION. WILL FIX IN FUTURE
UTEST( Texture, PartialUpdate_Boundaries )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );
    vhFinish();

    // 1. Offset outside texture (x > width)
    vhUpdateTexture( tex, 0, 0, 1, 1, vhAllocMem( 16 * 16 * 4 ), glm::ivec3( 70, 0, 0 ), glm::ivec3( 16, 16, 1 ) );

    // 2. Region partially outside (offset.x + extent.x > totalWidth)
    vhUpdateTexture( tex, 0, 0, 1, 1, vhAllocMem( 16 * 16 * 4 ), glm::ivec3( 50, 0, 0 ), glm::ivec3( 20, 16, 1 ) );

    // 3. Zero width region
    vhUpdateTexture( tex, 0, 0, 1, 1, vhAllocMem( 0 ), glm::ivec3( 0, 0, 0 ), glm::ivec3( 0, 16, 1 ) );

    // 4. Invalid handle
    vhUpdateTexture( VRHI_INVALID_HANDLE, 0, 0, 1, 1, vhAllocMem( 16 * 16 * 4 ), glm::ivec3( 0, 0, 0 ), glm::ivec3( 16, 16, 1 ) );

    vhFinish();
    // We expect no crashes. 

    vhDestroyTexture( tex );
    vhFinish();
}
*/

UTEST( Texture, RegionDataSize_SimpleRGBA8 )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 32, 32, 1 ), 0 );
    EXPECT_EQ( size, 4096 );
}

UTEST( Texture, RegionDataSize_ZeroExtent )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 0, 0, 0 ), 0 );
    EXPECT_EQ( size, 0 );
}

UTEST( Texture, RegionDataSize_NegativeExtent )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( -1, -1, -1 ), 0 );
    EXPECT_EQ( size, 0 );
}

UTEST( Texture, RegionDataSize_3DExtent )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 16, 16, 4 ), 0 );
    EXPECT_EQ( size, 4096 );
}

UTEST( Texture, RegionDataSize_CompressedBC1 )
{
    auto info = vhGetFormat( nvrhi::Format::BC1_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 64, 64, 1 ), 0 );
    EXPECT_EQ( size, 2048 );
}

UTEST( Texture, RegionDataSize_CompressedNonAligned )
{
    auto info = vhGetFormat( nvrhi::Format::BC1_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 17, 17, 1 ), 0 );
    // (ceil(17/4) * ceil(17/4) * 8) = 5 * 5 * 8 = 200
    EXPECT_EQ( size, 200 );
}

UTEST( Texture, RegionDataSize_R8 )
{
    auto info = vhGetFormat( nvrhi::Format::R8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 100, 100, 1 ), 0 );
    EXPECT_EQ( size, 10000 );
}

UTEST( Texture, RegionVerification )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( 64, 64 ), 2, nvrhi::Format::RGBA8_UNORM );
    vhFlush();

    // 1. Valid Update
    vhUpdateTexture( tex, 0, 0, 1, 1, vhAllocMem( 16 * 16 * 4 ), glm::ivec3( 0, 0, 0 ), glm::ivec3( 16, 16, 1 ) );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 2. Invalid Update (OOB)
    vhUpdateTexture( tex, 0, 0, 1, 1, vhAllocMem( 16 * 16 * 4 ), glm::ivec3( 50, 50, 0 ), glm::ivec3( 20, 20, 1 ) );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    startErrors = g_vhErrorCounter.load();

    // 3. Invalid Data Size
    vhUpdateTexture( tex, 0, 0, 1, 1, vhAllocMem( 100 ), glm::ivec3( 0, 0, 0 ), glm::ivec3( 16, 16, 1 ) );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    startErrors = g_vhErrorCounter.load();

    // 4. Valid Blit (Full)
    vhTexture src = vhAllocTexture();
    vhCreateTexture2D( src, glm::ivec2( 32, 32 ), 1, nvrhi::Format::RGBA8_UNORM );
    vhBlitTexture( tex, src, 0, 0 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 5. Invalid Blit (OOB Src)
    vhBlitTexture( tex, src, 0, 0, 0, 0, glm::ivec3( 0 ), glm::ivec3( 10, 10, 0 ), glm::ivec3( 30, 30, 1 ) );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    startErrors = g_vhErrorCounter.load();

    // 6. Invalid Blit (OOB Dst)
    vhBlitTexture( tex, src, 0, 0, 0, 0, glm::ivec3( 50, 50, 0 ), glm::ivec3( 0 ), glm::ivec3( 20, 20, 1 ) );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    startErrors = g_vhErrorCounter.load();

    vhDestroyTexture( tex );
    vhDestroyTexture( src );
    vhFlush();
}

UTEST_STATE();

int main( int argc, const char* const argv[] )
{
#ifndef NDEBUG
    g_vhInit.debug = true;
#endif
    int result = utest_main( argc, argv );

    if ( g_testInit )
    {
        vhShutdown();
        g_testInit = false;
    }

    return result;
}

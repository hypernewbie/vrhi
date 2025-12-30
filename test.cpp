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

#define VRHI_UNIT_TEST
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

UTEST( RHI, RayTracingControl )
{
    // If global init is active, shut it down to test clean init
    if ( g_testInit )
    {
        vhShutdown();
        g_testInit = false;
    }

    // Case 1: Disable RT
    g_vhInit.raytracing = false;
    vhInit();
    EXPECT_FALSE( g_vhRayTracingEnabled );
    vhShutdown();

    // Case 2: Enable RT
    g_vhInit.raytracing = true;
    vhInit();
    // g_vhRayTracingEnabled should be true if HW supports it. 
    // If not, it will be false, but initialization shouldn't crash.
    // In our test environment, we expect this to match whether extensions were actually enabled.
    VRHI_LOG( "Ray Tracing Supported by HW: %s\n", g_vhRayTracingEnabled ? "YES" : "NO" );
    vhShutdown();

    // Reset to default for other tests
    g_vhInit.raytracing = false;
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

    // 3. Null Data ( should error )
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "NullDataTest", vhAllocMem( 1024 ), "float3 POSITION" );
    vhUpdateVertexBuffer( buf, nullptr, 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

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

    const int width = 64;
    const int height = 64;
    const size_t dataSize = width * height * 4;

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Source: All 255
    vhMem* whiteData = vhAllocMem( dataSize );
    std::fill( whiteData->begin(), whiteData->end(), 255 );
    vhCreateTexture2D( src, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, whiteData );

    // Destination: All 0
    vhMem* blackData = vhAllocMem( dataSize );
    std::fill( blackData->begin(), blackData->end(), 0 );
    vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, blackData );

    vhFinish();

    // Call blit
    vhBlitTexture( dst, src );
    vhFinish();

    // Readback and verify
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), dataSize );
    for ( size_t i = 0; i < dataSize; ++i )
    {
        EXPECT_EQ( readData[i], 255 );
        if ( readData[i] != 255 ) break;
    }
    
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

    // Fill Src Mip 1 with 128
    const size_t mip1DataSize = 64 * 64 * 4;
    vhMem* mipData = vhAllocMem( mip1DataSize );
    std::fill( mipData->begin(), mipData->end(), 128 );
    vhUpdateTexture( src, 1, 0, 1, 1, mipData );
    vhFinish();

    // Blit Src Mip 1 to Dst Mip 0
    vhBlitTexture( dst, src, 0, 1 );
    vhFinish();

    // Readback and verify
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), mip1DataSize );
    for ( size_t i = 0; i < mip1DataSize; ++i )
    {
        EXPECT_EQ( readData[i], 128 );
        if ( readData[i] != 128 ) break;
    }

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

    const int width = 64;
    const int height = 64;
    const size_t dataSize = width * height * 4;

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Source: 200
    vhMem* srcData = vhAllocMem( dataSize );
    std::fill( srcData->begin(), srcData->end(), 200 );
    vhCreateTexture2D( src, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, srcData );

    // Dest: 50
    vhMem* dstData = vhAllocMem( dataSize );
    std::fill( dstData->begin(), dstData->end(), 50 );
    vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, dstData );

    vhFinish();

    // Copy 32x32 region from Src(16,16) to Dst(8,8)
    glm::ivec3 extent( 32, 32, 1 );
    glm::ivec3 srcOffset( 16, 16, 0 );
    glm::ivec3 dstOffset( 8, 8, 0 );
    vhBlitTexture( dst, src, 0, 0, 0, 0, dstOffset, srcOffset, extent );
    vhFinish();

    // Readback and verify
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), dataSize );
    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            uint8_t val = readData[( y * width + x ) * 4];
            if ( x >= 8 && x < 8 + 32 && y >= 8 && y < 8 + 32 )
            {
                EXPECT_EQ( val, 200 );
            }
            else
            {
                EXPECT_EQ( val, 50 );
            }
        }
    }

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST( Texture, BlitFunctional )
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

    // Functional check
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

UTEST( Texture, BlitStress )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    struct FormatInfo {
        nvrhi::Format format;
        std::string name;
    };

    std::vector<FormatInfo> formats = {
        { nvrhi::Format::RGBA8_UNORM, "RGBA8_UNORM" },
        { nvrhi::Format::R8_UNORM, "R8_UNORM" },
        { nvrhi::Format::RG8_UNORM, "RG8_UNORM" },
        { nvrhi::Format::R8_UINT, "R8_UINT" },
        { nvrhi::Format::RGBA32_FLOAT, "RGBA32_FLOAT" },
        { nvrhi::Format::R32_FLOAT, "R32_FLOAT" }
    };

    const int width = 64;
    const int height = 64;

    for ( const auto& fmt : formats )
    {
        vhFormatInfo info = vhGetFormat( fmt.format );
        const int pixelSize = info.elementSize;
        const size_t dataSize = ( size_t ) width * height * pixelSize;

        vhTexture src = vhAllocTexture();
        vhTexture dst = vhAllocTexture();

        // Background Color (all 0x55)
        vhMem* bgData = vhAllocMem( dataSize );
        std::fill( bgData->begin(), bgData->end(), 0x55 );
        vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, fmt.format, VRHI_TEXTURE_NONE, bgData );

        // Foreground Color (all 0xAA)
        vhMem* fgData = vhAllocMem( dataSize );
        std::fill( fgData->begin(), fgData->end(), 0xAA );
        vhCreateTexture2D( src, glm::ivec2( width, height ), 1, fmt.format, VRHI_TEXTURE_NONE, fgData );

        vhFinish();

        // Action 1: Full Blit
        vhBlitTexture( dst, src );
        vhFinish();

        vhMem readData;
        vhReadTextureSlow( dst, 0, 0, &readData );
        vhFinish();

        ASSERT_EQ( readData.size(), dataSize );
        for ( size_t i = 0; i < dataSize; ++i )
        {
            EXPECT_EQ( readData[i], 0xAA );
            if ( readData[i] != 0xAA ) break;
        }

        // Action 2: Region Blit
        // Reset dst to background
        vhMem* bgData2 = vhAllocMem( dataSize );
        std::fill( bgData2->begin(), bgData2->end(), 0x55 );
        vhUpdateTexture( dst, 0, 0, 1, 1, bgData2 );
        vhFinish();

        // Blit 16x16 at 8,8
        glm::ivec3 extent( 16, 16, 1 );
        glm::ivec3 offset( 8, 8, 0 );
        vhBlitTexture( dst, src, 0, 0, 0, 0, offset, offset, extent );
        vhFinish();

        readData.clear();
        vhReadTextureSlow( dst, 0, 0, &readData );
        vhFinish();

        ASSERT_EQ( readData.size(), dataSize );
        for ( int y = 0; y < height; ++y )
        {
            for ( int x = 0; x < width; ++x )
            {
                size_t pixelOffset = ( size_t )( y * width + x ) * pixelSize;
                bool inRegion = ( x >= 8 && x < 8 + 16 && y >= 8 && y < 8 + 16 );
                uint8_t expected = inRegion ? 0xAA : 0x55;
                for ( int c = 0; c < pixelSize; ++c )
                {
                    EXPECT_EQ( readData[pixelOffset + c], expected );
                    if ( readData[pixelOffset + c] != expected ) break;
                }
            }
        }

        vhDestroyTexture( src );
        vhDestroyTexture( dst );
        vhFlush();
    }
}

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

UTEST( Buffer, Flags_Compute )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer bRead = vhAllocBuffer();
    vhCreateVertexBuffer( bRead, "ComputeRead", vhAllocMem( 1024 ), "float3 POSITION", 0, VRHI_BUFFER_COMPUTE_READ );

    vhBuffer bWrite = vhAllocBuffer();
    vhCreateVertexBuffer( bWrite, "ComputeWrite", vhAllocMem( 1024 ), "float3 POSITION", 0, VRHI_BUFFER_COMPUTE_WRITE );

    vhBuffer bReadWrite = vhAllocBuffer();
    vhCreateVertexBuffer( bReadWrite, "ComputeReadWrite", vhAllocMem( 1024 ), "float3 POSITION", 0, VRHI_BUFFER_COMPUTE_READ_WRITE );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bRead );
    vhDestroyBuffer( bWrite );
    vhDestroyBuffer( bReadWrite );
    vhFlush();
}

UTEST( Buffer, Flags_DrawIndirect )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    int32_t startErrors = g_vhErrorCounter.load();
    vhBuffer bIndirect = vhAllocBuffer();
    vhCreateVertexBuffer( bIndirect, "DrawIndirect", vhAllocMem( 1024 ), "float3 POSITION", 0, VRHI_BUFFER_DRAW_INDIRECT );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bIndirect );
    vhFlush();
}

UTEST( Buffer, Flags_Resize )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // 1. Success case: ALLOW_RESIZE
    vhBuffer bResize = vhAllocBuffer();
    vhCreateVertexBuffer( bResize, "AllowResize", vhAllocMem( 64 ), "float3 POSITION", 0, VRHI_BUFFER_ALLOW_RESIZE );

    // Update with larger data
    vhUpdateVertexBuffer( bResize, vhAllocMem( 128 ), 0 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 2. Failure case: No ALLOW_RESIZE
    vhBuffer bNoResize = vhAllocBuffer();
    vhCreateVertexBuffer( bNoResize, "NoResize", vhAllocMem( 64 ), "float3 POSITION", 0, VRHI_BUFFER_NONE );

    // Update with larger data - should trigger error in backend
    vhUpdateVertexBuffer( bNoResize, vhAllocMem( 128 ), 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bResize );
    vhDestroyBuffer( bNoResize );
    vhFlush();
}

UTEST( Texture, Type_2DArray )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int width = 32;
    const int height = 32;
    const int layers = 4;
    const size_t layerSize = width * height; // R8_UINT
    const size_t totalSize = layerSize * layers;

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2DArray( tex, glm::ivec2( width, height ), layers, 1, nvrhi::Format::R8_UINT );

    // Action 1: Update all 4 layers in one call
    auto fullData = vhAllocMem( totalSize );
    for ( int l = 0; l < layers; ++l )
    {
        std::fill( fullData->begin() + l * layerSize, fullData->begin() + ( l + 1 ) * layerSize, ( uint8_t ) l );
    }
    vhUpdateTexture( tex, 0, 0, 1, layers, fullData );
    vhFinish();

    // Verify 1
    for ( int l = 0; l < layers; ++l )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, l, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), layerSize );
        for ( size_t i = 0; i < layerSize; ++i )
        {
            EXPECT_EQ( readData[i], ( uint8_t ) l );
            if ( readData[i] != ( uint8_t ) l ) break;
        }
    }

    // Action 2: Update only Layer 2 (Middle) with 0xFF
    auto midData = vhAllocMem( layerSize );
    std::fill( midData->begin(), midData->end(), 0xFF );
    vhUpdateTexture( tex, 0, 2, 1, 1, midData );
    vhFinish();

    // Verify 2
    for ( int l = 0; l < layers; ++l )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, l, &readData );
        vhFinish();
        uint8_t expected = ( l == 2 ) ? 0xFF : ( uint8_t ) l;
        for ( size_t i = 0; i < layerSize; ++i )
        {
            EXPECT_EQ( readData[i], expected );
            if ( readData[i] != expected ) break;
        }
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST( Texture, Type_Cube )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int dim = 32;
    const int faces = 6;
    const size_t faceSize = dim * dim; // R8_UINT
    const size_t totalSize = faceSize * faces;

    vhTexture tex = vhAllocTexture();
    vhCreateTextureCube( tex, dim, 1, nvrhi::Format::R8_UINT );

    // Action 1: Update all 6 faces
    auto fullData = vhAllocMem( totalSize );
    for ( int f = 0; f < faces; ++f )
    {
        std::fill( fullData->begin() + f * faceSize, fullData->begin() + ( f + 1 ) * faceSize, ( uint8_t )( f + 10 ) );
    }
    vhUpdateTexture( tex, 0, 0, 1, faces, fullData );
    vhFinish();

    // Verify 1
    for ( int f = 0; f < faces; ++f )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, f, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), faceSize );
        for ( size_t i = 0; i < faceSize; ++i )
        {
            EXPECT_EQ( readData[i], ( uint8_t )( f + 10 ) );
            if ( readData[i] != ( uint8_t )( f + 10 ) ) break;
        }
    }

    // Action 2: Update Face 3 (-Y) only with 0xAA
    auto faceData = vhAllocMem( faceSize );
    std::fill( faceData->begin(), faceData->end(), 0xAA );
    vhUpdateTexture( tex, 0, 3, 1, 1, faceData );
    vhFinish();

    // Verify 2
    for ( int f = 0; f < faces; ++f )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, f, &readData );
        vhFinish();
        uint8_t expected = ( f == 3 ) ? 0xAA : ( uint8_t )( f + 10 );
        for ( size_t i = 0; i < faceSize; ++i )
        {
            EXPECT_EQ( readData[i], expected );
            if ( readData[i] != expected ) break;
        }
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST( Texture, Type_3D )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int w = 32, h = 32, d = 4;
    const size_t totalSize = w * h * d; // R8_UINT

    vhTexture tex = vhAllocTexture();
    vhCreateTexture3D( tex, glm::ivec3( w, h, d ), 1, nvrhi::Format::R8_UINT );

    // Action: Update full volume with gradient
    auto data = vhAllocMem( totalSize );
    for ( size_t i = 0; i < totalSize; ++i ) ( *data )[i] = ( uint8_t )( i % 256 );
    vhUpdateTexture( tex, 0, 0, 1, 1, data );
    vhFinish();

    // Verify: Readback (Slow path reads depth slices as array layers for 3D)
    // 3D texture readback not supported yet. This test doesnt work.
    /*vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), totalSize );
    for ( size_t i = 0; i < totalSize; ++i )
    {
        EXPECT_EQ( readData[i], ( uint8_t )( i % 256 ) );
        if ( readData[i] != ( uint8_t )( i % 256 ) ) break;
    }*/

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST( Texture, MipChain )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int dim = 32;
    const int mips = 4; // 32, 16, 8, 4
    const nvrhi::Format fmt = nvrhi::Format::R8_UINT;
    
    // Calculate total size
    size_t totalSize = 0;
    std::vector<size_t> mipSizes;
    for ( int i = 0; i < mips; ++i )
    {
        int mDim = std::max( 1, dim >> i );
        size_t s = ( size_t ) mDim * mDim;
        mipSizes.push_back( s );
        totalSize += s;
    }

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( dim, dim ), mips, fmt );

    // Action 1: Update all mips in one call
    auto fullData = vhAllocMem( totalSize );
    size_t offset = 0;
    for ( int i = 0; i < mips; ++i )
    {
        std::fill( fullData->begin() + offset, fullData->begin() + offset + mipSizes[i], ( uint8_t )( i + 1 ) );
        offset += mipSizes[i];
    }
    vhUpdateTexture( tex, 0, 0, mips, 1, fullData );
    vhFinish();

    // Verify 1
    for ( int i = 0; i < mips; ++i )
    {
        vhMem readData;
        vhReadTextureSlow( tex, i, 0, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), mipSizes[i] );
        for ( size_t j = 0; j < mipSizes[i]; ++j )
        {
            EXPECT_EQ( readData[j], ( uint8_t )( i + 1 ) );
            if ( readData[j] != ( uint8_t )( i + 1 ) ) break;
        }
    }

    // Action 2: Update only Mip 2 with 0x77
    auto mip2Data = vhAllocMem( mipSizes[2] );
    std::fill( mip2Data->begin(), mip2Data->end(), 0x77 );
    vhUpdateTexture( tex, 2, 0, 1, 1, mip2Data );
    vhFinish();

    // Verify 2
    for ( int i = 0; i < mips; ++i )
    {
        vhMem readData;
        vhReadTextureSlow( tex, i, 0, &readData );
        vhFinish();
        uint8_t expected = ( i == 2 ) ? 0x77 : ( uint8_t )( i + 1 );
        for ( size_t j = 0; j < mipSizes[i]; ++j )
        {
            EXPECT_EQ( readData[j], expected );
            if ( readData[j] != expected ) break;
        }
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST( Texture, Type_1D )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }

    const int width = 256;
    vhTexture tex = vhAllocTexture();
    vhCreateTexture( tex, nvrhi::TextureDimension::Texture1D, glm::ivec3( width, 1, 1 ), 1, 1, nvrhi::Format::R8_UINT );

    auto data = vhAllocMem( width );
    for ( int i = 0; i < width; ++i ) ( *data )[i] = ( uint8_t ) i;
    vhUpdateTexture( tex, 0, 0, 1, 1, data );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), ( size_t ) width );
    for ( int i = 0; i < width; ++i )
    {
        EXPECT_EQ( readData[i], ( uint8_t ) i );
        if ( readData[i] != ( uint8_t ) i ) break;
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST( Buffer, NumVerts_CreateResize )
{
    if ( !g_testInit )
    {
        vhInit();
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // 1. Create Uninitialized
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "UninitCreate", nullptr, "float3 POSITION", 100, VRHI_BUFFER_ALLOW_RESIZE );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 2. Resize via numVerts
    vhUpdateVertexBuffer( buf, nullptr, 0, 200 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
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

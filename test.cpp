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
#define VRHI_SHADER_COMPILER
#include "vrhi.h"
#include "vrhi_impl.h"
#include "vrhi_utils.h"

// Internal benchmark functions from vrhi_impl.h


UTEST( Vrhi, Dummy )
{
    ASSERT_TRUE( true );
}

static bool g_testInit = false;
static bool g_testInitQuiet = true;

extern std::string vhGetDeviceInfo();
extern std::string vhBuildShaderFlagArgs_Internal( uint64_t flags );
extern bool vhRunExe( const std::string& command, std::string& outOutput );
extern void vhPartialFillGraphicsPipelineDescFromState_Internal( uint64_t state, nvrhi::GraphicsPipelineDesc& desc );
extern bool vhBackend_UNITTEST_GetFrameBuffer( const std::vector< vhTexture >& colors, vhTexture depth );

UTEST( ShaderInternal, StateToDesc )
{
    nvrhi::GraphicsPipelineDesc desc;
    
    // 1. Test Default (Depth Test Less, Write All, Cull CW)
    vhPartialFillGraphicsPipelineDescFromState_Internal( VRHI_STATE_DEFAULT, desc );
    EXPECT_TRUE( desc.renderState.depthStencilState.depthTestEnable );
    EXPECT_EQ( desc.renderState.depthStencilState.depthFunc, nvrhi::ComparisonFunc::Less );
    EXPECT_TRUE( desc.renderState.depthStencilState.depthWriteEnable );
    EXPECT_EQ( desc.renderState.rasterState.cullMode, nvrhi::RasterCullMode::Back );
    
    // 2. Test Blend Add
    desc = nvrhi::GraphicsPipelineDesc(); // Reset
    vhPartialFillGraphicsPipelineDescFromState_Internal( VRHI_STATE_BLEND_ADD, desc );
    EXPECT_EQ( desc.renderState.blendState.targets[0].srcBlend, nvrhi::BlendFactor::One );
    EXPECT_EQ( desc.renderState.blendState.targets[0].destBlend, nvrhi::BlendFactor::One );
    
    // 3. Test Primitive Topology
    desc = nvrhi::GraphicsPipelineDesc();
    vhPartialFillGraphicsPipelineDescFromState_Internal( VRHI_STATE_PT_LINES, desc );
    EXPECT_EQ( desc.primType, nvrhi::PrimitiveType::LineList );
}

UTEST( RHI, Init )
{
    // If global init is active, shut it down to test clean init
    if ( g_testInit )
    {
        vhShutdown( g_testInitQuiet );
        g_testInit = false;
    }

    // Test init
    vhInit( g_testInitQuiet );

    // Verify globals
    EXPECT_NE( g_vhDevice.Get(), nullptr );

    // Test GetInfo returns something
    std::string info = vhGetDeviceInfo();
    EXPECT_FALSE( info.empty() );
    EXPECT_TRUE( info.find( "Device:" ) != std::string::npos );

    // Test shutdown
    vhShutdown( g_testInitQuiet );
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
        vhShutdown( g_testInitQuiet );
        g_testInit = false;
    }

    std::vector<std::string> logs;
    int errorCount = 0;

    g_vhInit.debug = true;
    g_vhInit.fnLogCallback = [&]( bool err, const std::string& msg ) {
        if ( err ) errorCount++;
        logs.push_back( msg );
    };

    // We want logs here, so pass quiet=false explicitly.
    vhInit( false );

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


    vhShutdown( false );
    
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
        vhShutdown( g_testInitQuiet );
        g_testInit = false;
    }

    // Case 1: Disable RT
    g_vhInit.raytracing = false;
    vhInit( g_testInitQuiet );
    EXPECT_FALSE( g_vhRayTracingEnabled );
    vhShutdown( g_testInitQuiet );

    // Case 2: Enable RT
    g_vhInit.raytracing = true;
    vhInit( g_testInitQuiet );
    // g_vhRayTracingEnabled should be true if HW supports it. 
    // If not, it will be false, but initialization shouldn't crash.
    // In our test environment, we expect this to match whether extensions were actually enabled.
    VRHI_LOG( "Ray Tracing Supported by HW: %s\n", g_vhRayTracingEnabled ? "YES" : "NO" );
    vhShutdown( g_testInitQuiet );

    // Reset to default for other tests
    g_vhInit.raytracing = false;
}

UTEST( Texture, CreateDestroyError )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "UpdateTest", vhAllocMem( 1024 ), "float3 POSITION" );
    
    // Basic Update
    vhUpdateVertexBuffer( buf, vhAllocMem( 256 ), 0 );
    
    // Offset Update (512 bytes = ~42.67 vertices, round to 43 vertices for stride 12)
    vhUpdateVertexBuffer( buf, vhAllocMem( 100 ), 43 );
    
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( Texture, BlitConnectivity )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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

UTEST( Sampler, MaskNonOverlap )
{
    // Each mask should be non-overlapping with all others
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK & VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOR_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOR_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_SAMPLE_STENCIL & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
}

UTEST( Sampler, ValuesWithinMask )
{
    // Address U
    EXPECT_EQ( VRHI_SAMPLER_U_WRAP & ~VRHI_SAMPLER_U_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MIRROR & ~VRHI_SAMPLER_U_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_CLAMP & ~VRHI_SAMPLER_U_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_BORDER & ~VRHI_SAMPLER_U_MASK, 0u );

    // Address V
    EXPECT_EQ( VRHI_SAMPLER_V_WRAP & ~VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MIRROR & ~VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_CLAMP & ~VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_BORDER & ~VRHI_SAMPLER_V_MASK, 0u );

    // Address W
    EXPECT_EQ( VRHI_SAMPLER_W_WRAP & ~VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MIRROR & ~VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_CLAMP & ~VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_BORDER & ~VRHI_SAMPLER_W_MASK, 0u );

    // Min Filter
    EXPECT_EQ( VRHI_SAMPLER_MIN_LINEAR & ~VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_POINT & ~VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_ANISOTROPIC & ~VRHI_SAMPLER_MIN_MASK, 0u );

    // Mag Filter
    EXPECT_EQ( VRHI_SAMPLER_MAG_LINEAR & ~VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_POINT & ~VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_ANISOTROPIC & ~VRHI_SAMPLER_MAG_MASK, 0u );

    // Mip Filter
    EXPECT_EQ( VRHI_SAMPLER_MIP_LINEAR & ~VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_POINT & ~VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_NONE & ~VRHI_SAMPLER_MIP_MASK, 0u );

    // Compare Function
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_LESS & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_LEQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_EQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_GEQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_GREATER & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_NOTEQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_NEVER & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_ALWAYS & ~VRHI_SAMPLER_COMPARE_MASK, 0u );

    // Sample Stencil (single bit flag)
    EXPECT_NE( VRHI_SAMPLER_SAMPLE_STENCIL, 0u );

    // Anisotropy Levels
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_1 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_2 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_4 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_8 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_16 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
}

UTEST( Sampler, ShiftAlignment )
{
    // U: bits 0-1
    EXPECT_EQ( VRHI_SAMPLER_U_SHIFT, 0 );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK, 0x3u << VRHI_SAMPLER_U_SHIFT );

    // V: bits 2-3
    EXPECT_EQ( VRHI_SAMPLER_V_SHIFT, 2 );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK, 0x3u << VRHI_SAMPLER_V_SHIFT );

    // W: bits 4-5
    EXPECT_EQ( VRHI_SAMPLER_W_SHIFT, 4 );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK, 0x3u << VRHI_SAMPLER_W_SHIFT );

    // Min: bits 6-7
    EXPECT_EQ( VRHI_SAMPLER_MIN_SHIFT, 6 );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK, 0x3u << VRHI_SAMPLER_MIN_SHIFT );

    // Mag: bits 8-9
    EXPECT_EQ( VRHI_SAMPLER_MAG_SHIFT, 8 );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK, 0x3u << VRHI_SAMPLER_MAG_SHIFT );

    // Mip: bits 10-11
    EXPECT_EQ( VRHI_SAMPLER_MIP_SHIFT, 10 );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK, 0x3u << VRHI_SAMPLER_MIP_SHIFT );

    // Compare: bits 12-15
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_SHIFT, 12 );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK, 0xFu << VRHI_SAMPLER_COMPARE_SHIFT );

    // MipBias: bits 16-23
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_SHIFT, 16 );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK, 0xFFu << VRHI_SAMPLER_MIPBIAS_SHIFT );

    // Border Color: bits 24-27
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOR_SHIFT, 24 );
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOR_MASK, 0xFu << VRHI_SAMPLER_BORDER_COLOR_SHIFT );

    // Sample Stencil: bit 28
    EXPECT_EQ( VRHI_SAMPLER_SAMPLE_STENCIL, 1u << 28 );

    // Max Anisotropy: bits 29-31
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT, 29 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0x7u << VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT );
}

UTEST( Sampler, ValueUniqueness )
{
    // Address U values are distinct
    EXPECT_NE( VRHI_SAMPLER_U_WRAP, VRHI_SAMPLER_U_MIRROR );
    EXPECT_NE( VRHI_SAMPLER_U_WRAP, VRHI_SAMPLER_U_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_U_WRAP, VRHI_SAMPLER_U_BORDER );
    EXPECT_NE( VRHI_SAMPLER_U_MIRROR, VRHI_SAMPLER_U_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_U_MIRROR, VRHI_SAMPLER_U_BORDER );
    EXPECT_NE( VRHI_SAMPLER_U_CLAMP, VRHI_SAMPLER_U_BORDER );

    // Address V values are distinct
    EXPECT_NE( VRHI_SAMPLER_V_WRAP, VRHI_SAMPLER_V_MIRROR );
    EXPECT_NE( VRHI_SAMPLER_V_WRAP, VRHI_SAMPLER_V_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_V_WRAP, VRHI_SAMPLER_V_BORDER );
    EXPECT_NE( VRHI_SAMPLER_V_MIRROR, VRHI_SAMPLER_V_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_V_MIRROR, VRHI_SAMPLER_V_BORDER );
    EXPECT_NE( VRHI_SAMPLER_V_CLAMP, VRHI_SAMPLER_V_BORDER );

    // Address W values are distinct
    EXPECT_NE( VRHI_SAMPLER_W_WRAP, VRHI_SAMPLER_W_MIRROR );
    EXPECT_NE( VRHI_SAMPLER_W_WRAP, VRHI_SAMPLER_W_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_W_WRAP, VRHI_SAMPLER_W_BORDER );
    EXPECT_NE( VRHI_SAMPLER_W_MIRROR, VRHI_SAMPLER_W_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_W_MIRROR, VRHI_SAMPLER_W_BORDER );
    EXPECT_NE( VRHI_SAMPLER_W_CLAMP, VRHI_SAMPLER_W_BORDER );

    // Min filter values are distinct
    EXPECT_NE( VRHI_SAMPLER_MIN_LINEAR, VRHI_SAMPLER_MIN_POINT );
    EXPECT_NE( VRHI_SAMPLER_MIN_LINEAR, VRHI_SAMPLER_MIN_ANISOTROPIC );
    EXPECT_NE( VRHI_SAMPLER_MIN_POINT, VRHI_SAMPLER_MIN_ANISOTROPIC );

    // Mag filter values are distinct
    EXPECT_NE( VRHI_SAMPLER_MAG_LINEAR, VRHI_SAMPLER_MAG_POINT );
    EXPECT_NE( VRHI_SAMPLER_MAG_LINEAR, VRHI_SAMPLER_MAG_ANISOTROPIC );
    EXPECT_NE( VRHI_SAMPLER_MAG_POINT, VRHI_SAMPLER_MAG_ANISOTROPIC );

    // Mip filter values are distinct
    EXPECT_NE( VRHI_SAMPLER_MIP_LINEAR, VRHI_SAMPLER_MIP_POINT );
    EXPECT_NE( VRHI_SAMPLER_MIP_LINEAR, VRHI_SAMPLER_MIP_NONE );
    EXPECT_NE( VRHI_SAMPLER_MIP_POINT, VRHI_SAMPLER_MIP_NONE );

    // Compare values are distinct
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_LEQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_EQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_GEQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_GREATER );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_NOTEQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_NEVER );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_ALWAYS );

    // Anisotropy level values are distinct
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_2 );
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_4 );
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_8 );
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_16 );
}

UTEST( Sampler, CompositeMacros )
{
    // VRHI_SAMPLER_POINT combines min, mag, mip point
    EXPECT_EQ( VRHI_SAMPLER_POINT, 
               VRHI_SAMPLER_MIN_POINT | VRHI_SAMPLER_MAG_POINT | VRHI_SAMPLER_MIP_POINT );

    // UVW convenience macros
    EXPECT_EQ( VRHI_SAMPLER_UVW_WRAP, 
               VRHI_SAMPLER_U_WRAP | VRHI_SAMPLER_V_WRAP | VRHI_SAMPLER_W_WRAP );
    EXPECT_EQ( VRHI_SAMPLER_UVW_MIRROR, 
               VRHI_SAMPLER_U_MIRROR | VRHI_SAMPLER_V_MIRROR | VRHI_SAMPLER_W_MIRROR );
    EXPECT_EQ( VRHI_SAMPLER_UVW_CLAMP, 
               VRHI_SAMPLER_U_CLAMP | VRHI_SAMPLER_V_CLAMP | VRHI_SAMPLER_W_CLAMP );
    EXPECT_EQ( VRHI_SAMPLER_UVW_BORDER, 
               VRHI_SAMPLER_U_BORDER | VRHI_SAMPLER_V_BORDER | VRHI_SAMPLER_W_BORDER );

    // Verify VRHI_SAMPLER_NONE is 0
    EXPECT_EQ( VRHI_SAMPLER_NONE, 0u );
}

UTEST( Sampler, MipBiasMacro )
{
    // Zero bias
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS(0.0f) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 0u );

    // Positive bias: 1.0 * 16 = 16
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS(1.0f) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 16u );

    // Positive bias: 0.5 * 16 = 8
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS(0.5f) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 8u );

    // Positive bias: 2.0 * 16 = 32
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS(2.0f) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 32u );

    // Verify result fits within mask
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS(1.0f) & ~VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS(7.9f) & ~VRHI_SAMPLER_MIPBIAS_MASK, 0u );

    // Negative bias: -1.0 * 16 = -16 (two's complement, expect 0xF0 or 240 in 8-bit)
    uint32_t rawBias = ( VRHI_SAMPLER_MIPBIAS(-1.0f) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu;
    int8_t negBias = (int8_t)rawBias;
    EXPECT_EQ( negBias, -16 );
}

UTEST( Sampler, BorderColorMacro )
{
    // Color index 0
    EXPECT_EQ( ( VRHI_SAMPLER_BORDER_COLOR(0) >> VRHI_SAMPLER_BORDER_COLOR_SHIFT ), 0u );

    // Color index 1
    EXPECT_EQ( ( VRHI_SAMPLER_BORDER_COLOR(1) >> VRHI_SAMPLER_BORDER_COLOR_SHIFT ), 1u );

    // Max valid color index (15)
    EXPECT_EQ( ( VRHI_SAMPLER_BORDER_COLOR(15) >> VRHI_SAMPLER_BORDER_COLOR_SHIFT ), 15u );

    // Values fit within mask
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOR(0) & ~VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOR(15) & ~VRHI_SAMPLER_BORDER_COLOR_MASK, 0u );
}

UTEST( Sampler, MaxAnisotropyMacro )
{
    // Direct macro values
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY(0), VRHI_SAMPLER_ANISOTROPY_1 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY(1), VRHI_SAMPLER_ANISOTROPY_2 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY(2), VRHI_SAMPLER_ANISOTROPY_4 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY(3), VRHI_SAMPLER_ANISOTROPY_8 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY(4), VRHI_SAMPLER_ANISOTROPY_16 );

    // Extraction test
    uint32_t flags = VRHI_SAMPLER_ANISOTROPY_8;
    uint32_t anisoIndex = ( flags & VRHI_SAMPLER_MAX_ANISOTROPY_MASK ) >> VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT;
    EXPECT_EQ( anisoIndex, 3u ); // 8x = index 3
}

UTEST( Sampler, BitsMaskCoverage )
{
    uint32_t allMasks = 
        VRHI_SAMPLER_U_MASK |
        VRHI_SAMPLER_V_MASK |
        VRHI_SAMPLER_W_MASK |
        VRHI_SAMPLER_MIN_MASK |
        VRHI_SAMPLER_MAG_MASK |
        VRHI_SAMPLER_MIP_MASK |
        VRHI_SAMPLER_COMPARE_MASK |
        VRHI_SAMPLER_MIPBIAS_MASK |
        VRHI_SAMPLER_BORDER_COLOR_MASK |
        VRHI_SAMPLER_SAMPLE_STENCIL |
        VRHI_SAMPLER_MAX_ANISOTROPY_MASK;

    EXPECT_EQ( VRHI_SAMPLER_BITS_MASK, allMasks );

    // Verify full 32-bit coverage
    EXPECT_EQ( VRHI_SAMPLER_BITS_MASK, 0xFFFFFFFFu );
}

UTEST( Sampler, CombinedFlagExtraction )
{
    // Create a complex sampler configuration
    uint32_t samplerFlags = 
        VRHI_SAMPLER_U_CLAMP |
        VRHI_SAMPLER_V_MIRROR |
        VRHI_SAMPLER_W_BORDER |
        VRHI_SAMPLER_MIN_ANISOTROPIC |
        VRHI_SAMPLER_MAG_LINEAR |
        VRHI_SAMPLER_MIP_POINT |
        VRHI_SAMPLER_COMPARE_LEQUAL |
        VRHI_SAMPLER_MIPBIAS(1.5f) |
        VRHI_SAMPLER_BORDER_COLOR(5) |
        VRHI_SAMPLER_SAMPLE_STENCIL |
        VRHI_SAMPLER_ANISOTROPY_8;

    // Extract and verify each field
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_U_MASK, VRHI_SAMPLER_U_CLAMP );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_V_MASK, VRHI_SAMPLER_V_MIRROR );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_W_MASK, VRHI_SAMPLER_W_BORDER );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MIN_MASK, VRHI_SAMPLER_MIN_ANISOTROPIC );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MAG_MASK, VRHI_SAMPLER_MAG_LINEAR );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MIP_MASK, VRHI_SAMPLER_MIP_POINT );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_COMPARE_MASK, VRHI_SAMPLER_COMPARE_LEQUAL );
    EXPECT_EQ( ( samplerFlags & VRHI_SAMPLER_BORDER_COLOR_MASK ) >> VRHI_SAMPLER_BORDER_COLOR_SHIFT, 5u );
    EXPECT_NE( samplerFlags & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, VRHI_SAMPLER_ANISOTROPY_8 );
}

UTEST( Backend, FramebufferCaching )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhTexture color = vhAllocTexture();
    vhTexture depth = vhAllocTexture();

    vhCreateTexture2D( color, glm::ivec2( 128, 128 ), 2, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhCreateTexture2D( depth, glm::ivec2( 128, 128 ), 2, nvrhi::Format::D24S8, VRHI_TEXTURE_RT );
    vhFinish();

    // Verify caching/deduplication
    EXPECT_TRUE( vhBackend_UNITTEST_GetFrameBuffer( { color }, depth ) );

    vhDestroyTexture( color );
    vhDestroyTexture( depth );
    vhFinish();
}

UTEST( Texture, BlitFunctional )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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
        vhInit( g_testInitQuiet );
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

UTEST( IndexBuffer, Basic16 )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    EXPECT_NE( buf, VRHI_INVALID_HANDLE );

    const uint64_t kCount = 12;
    std::vector<uint16_t> indices = { 0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7 };
    size_t dataSize = indices.size() * sizeof( uint16_t );

    auto data = vhAllocMem( dataSize );
    memcpy( data->data(), indices.data(), dataSize );

    vhCreateIndexBuffer( buf, "Basic16", data, 0, VRHI_BUFFER_NONE );
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( IndexBuffer, Basic32 )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    EXPECT_NE( buf, VRHI_INVALID_HANDLE );

    const uint64_t kCount = 6;
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
    size_t dataSize = indices.size() * sizeof( uint32_t );

    auto data = vhAllocMem( dataSize );
    memcpy( data->data(), indices.data(), dataSize );

    vhCreateIndexBuffer( buf, "Basic32", data, 0, VRHI_BUFFER_INDEX32 );
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( IndexBuffer, Flags_Coverage )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // Compute Read
    vhBuffer bCompRead = vhAllocBuffer();
    vhCreateIndexBuffer( bCompRead, "CompRead", nullptr, 100, VRHI_BUFFER_COMPUTE_READ );
    
    // Compute Write
    vhBuffer bCompWrite = vhAllocBuffer();
    vhCreateIndexBuffer( bCompWrite, "CompWrite", nullptr, 100, VRHI_BUFFER_COMPUTE_WRITE );

    // Draw Indirect
    vhBuffer bIndirect = vhAllocBuffer();
    vhCreateIndexBuffer( bIndirect, "DrawIndirect", nullptr, 100, VRHI_BUFFER_DRAW_INDIRECT );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bCompRead );
    vhDestroyBuffer( bCompWrite );
    vhDestroyBuffer( bIndirect );
    vhFlush();
}

UTEST( IndexBuffer, Resize_And_Uninit )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // 1. Uninitialized Creation with Resize
    vhBuffer buf = vhAllocBuffer();
    vhCreateIndexBuffer( buf, "ResizeTest", nullptr, 100, VRHI_BUFFER_ALLOW_RESIZE | VRHI_BUFFER_INDEX32 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 2. Resize via Update
    // Increasing size to 200 indices (32-bit)
    vhUpdateIndexBuffer( buf, nullptr, 0, 200 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // 3. Validation: Resize without flag
    vhBuffer bufFixed = vhAllocBuffer();
    vhCreateIndexBuffer( bufFixed, "FixedTest", nullptr, 100, VRHI_BUFFER_INDEX32 ); // No resize flag
    vhFlush();
    
    // Attempt resize - should fail
    vhUpdateIndexBuffer( bufFixed, nullptr, 0, 200 );
    vhFlush();
    
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhDestroyBuffer( bufFixed );
    vhFlush();
}

UTEST( Buffer, UniformAlignment )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t baseline = g_vhErrorCounter.load();

    // Step A: Creation Stress
    vhBuffer b1 = vhAllocBuffer();
    vhCreateUniformBuffer( b1, "AutoAlignCreate255", nullptr, 255 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    // Step B: Update Stress
    vhMem* data255 = vhAllocMem( 255 );
    vhUpdateUniformBuffer( b1, data255, 0, 255 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    vhDestroyBuffer( b1 );
    vhFlush();
}

UTEST( Buffer, StorageAlignment )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t baseline = g_vhErrorCounter.load();

    // Step A: Creation Stress
    vhBuffer b1 = vhAllocBuffer();
    vhCreateStorageBuffer( b1, "AutoAlignCreate15", nullptr, 15 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    // Step B: Update Stress
    vhMem* data15 = vhAllocMem( 15 );
    vhUpdateStorageBuffer( b1, data15, 0, 15 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    vhDestroyBuffer( b1 );
    vhFlush();
}

UTEST( Shader, Lifecycle )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    int32_t baseline = g_vhErrorCounter.load();

    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            return output;
        }
    )";

    std::vector< uint32_t > spirv;
    bool compiled = vhCompileShader(
        "LifecycleShader",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        spirv,
        "main"
    );
    ASSERT_TRUE( compiled );

    vhShader s = vhAllocShader();
    vhCreateShader( s, "LifecycleShader", VRHI_SHADER_STAGE_VERTEX, spirv, "main" );
    
    vhDestroyShader( s );
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), baseline );
}

UTEST( Shader, BuildFlags )
{
#ifndef VRHI_SHADER_COMPILER
#error "Shader compiler implementation must be enabled for tests!"
#endif

    // Test 1: Default/Release
    {
        uint64_t flags = 0;
        std::string args = vhBuildShaderFlagArgs_Internal( flags );
        // ShaderMake uses -m for model, profile is in config file
        EXPECT_TRUE( args.find( "-m 6_5" ) != std::string::npos );
        EXPECT_TRUE( args.find( "-O 3" ) != std::string::npos );
    }

    // Test 2: Debug & SM 6.0 & Vertex
    {
        uint64_t flags = VRHI_SHADER_DEBUG | VRHI_SHADER_SM_6_0 | VRHI_SHADER_STAGE_VERTEX;
        std::string args = vhBuildShaderFlagArgs_Internal( flags );
        EXPECT_TRUE( args.find( "-m 6_0" ) != std::string::npos );
        EXPECT_TRUE( args.find( "-O 0" ) != std::string::npos );
        EXPECT_TRUE( args.find( "--embedPDB" ) != std::string::npos );
    }

    // Test 3: Matrix & Warnings
    {
        uint64_t flags = VRHI_SHADER_ROW_MAJOR | VRHI_SHADER_WARNINGS_AS_ERRORS;
        std::string args = vhBuildShaderFlagArgs_Internal( flags );
        EXPECT_TRUE( args.find( "--matrixRowMajor" ) != std::string::npos );
        EXPECT_TRUE( args.find( "--WX" ) != std::string::npos );
    }
}

UTEST( Shader, RunExe )
{
    std::string output;
    bool success = vhRunExe( "echo HelloVRHI", output );
    EXPECT_TRUE( success );
    EXPECT_TRUE( output.find( "HelloVRHI" ) != std::string::npos );
}

UTEST( Shader, Compile )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            return output;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "TestShader",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        spirv,
        "main",
        {},
        {},
        &error
    );

    if ( !success )
    {
        std::cout << "Shader compilation failed: " << error << std::endl;
    }

    EXPECT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );

    // Test Caching (second call should be fast and succeed)
    std::vector< uint32_t > cachedSpirv;
    success = vhCompileShader(
        "TestShader",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        cachedSpirv,
        "main",
        {},
        {},
        &error
    );

    EXPECT_TRUE( success );
    EXPECT_EQ( spirv.size(), cachedSpirv.size() );
    if ( spirv.size() == cachedSpirv.size() )
    {
        for ( size_t i = 0; i < spirv.size(); ++i )
        {
            EXPECT_EQ( spirv[i], cachedSpirv[i] );
        }
    }
}

UTEST( Shader, CompileFail )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Shader with syntax error (missing semicolon)
    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            return output // Missing semicolon
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "TestShaderFail",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        spirv,
        "main",
        {},
        {},
        &error
    );

    EXPECT_FALSE( success );
    EXPECT_GT( error.size(), 0 );
    // Check for some text indicating an error
    EXPECT_TRUE( error.find( "error" ) != std::string::npos || error.find( "Error" ) != std::string::npos );
}

UTEST( ResourceQueries, Texture )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhTexture tex = vhAllocTexture();
    glm::ivec2 dims( 128, 64 );
    nvrhi::Format fmt = nvrhi::Format::RGBA8_UNORM;
    vhCreateTexture2D( tex, dims, 1, fmt );
    vhFlush();

    std::vector< vhTextureMipInfo > mipInfo;
    vhTexInfo info = vhGetTextureInfo( tex, &mipInfo );

    EXPECT_EQ( info.dimensions, glm::ivec3( dims, 1 ) );
    EXPECT_EQ( info.format, fmt );
    EXPECT_EQ( mipInfo.size(), 1 );
    EXPECT_EQ( mipInfo[0].dimensions, glm::ivec3( dims, 1 ) );

    void* handle = vhGetTextureNvrhiHandle( tex );
    EXPECT_NE( handle, nullptr );

    vhDestroyTexture( tex );
    vhFlush();

    // Query after destruction should return null/default
    info = vhGetTextureInfo( tex );
    EXPECT_EQ( info.format, nvrhi::Format::UNKNOWN );
    EXPECT_EQ( vhGetTextureNvrhiHandle( tex ), nullptr );
}

UTEST( ResourceQueries, Buffer )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhBuffer buf = vhAllocBuffer();
    uint64_t size = 1024;
    uint16_t flags = VRHI_BUFFER_COMPUTE_WRITE;
    vhCreateUniformBuffer( buf, "TestBuffer", nullptr, size, flags );
    vhFlush();

    uint32_t stride = 0;
    uint64_t qFlags = 0;
    uint64_t qSize = vhGetBufferInfo( buf, &stride, &qFlags );

    EXPECT_EQ( qSize, size );
    EXPECT_EQ( stride, 1 ); // Uniform buffer stride is 1
    EXPECT_EQ( qFlags, flags );

    void* handle = vhGetBufferNvrhiHandle( buf );
    EXPECT_NE( handle, nullptr );

    vhDestroyBuffer( buf );
    vhFlush();

    // Query after destruction should return 0/null
    EXPECT_EQ( vhGetBufferInfo( buf ), 0 );
    EXPECT_EQ( vhGetBufferNvrhiHandle( buf ), nullptr );
}

UTEST( Shader, Reflection )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    const char* c_shaderSource = R"(
        struct Data { float4 val; };
        ConstantBuffer<Data> g_Constants;
        RWStructuredBuffer<Data> g_Output;

        [numthreads(8, 4, 1)]
        void main(uint3 threadID : SV_DispatchThreadID)
        {
            g_Output[threadID.x].val = g_Constants.val;
        }
    )";

    std::vector<uint32_t> spirv;
    std::string error;
    bool compiled = vhCompileShader( "TestQueryShader", c_shaderSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    ASSERT_TRUE( compiled );

    vhShader shader = vhAllocShader();
    vhCreateShader( shader, "TestQueryShader", VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main" );
    vhFlush();

    // Query Info
    glm::uvec3 groupSize = { 0, 0, 0 };
    std::vector< vhShaderReflectionResource > resources;
    vhGetShaderInfo( shader, &groupSize, &resources );

    EXPECT_EQ( groupSize.x, 8 );
    EXPECT_EQ( groupSize.y, 4 );
    EXPECT_EQ( groupSize.z, 1 );

    // Expecting 2 resources: ConstantBuffer at b0 and StructuredBuffer_UAV at u1
    EXPECT_EQ( resources.size(), 2 );

    bool foundCB = false;
    bool foundSB = false;
    for ( const auto& res : resources )
    {
        printf( "    Reflected Resource: %s, Slot: %u, Set: %u, Type: %d\n", res.name.c_str(), res.slot, res.set, (int)res.type );
        if ( res.name == "g_Constants" && res.type == nvrhi::ResourceType::ConstantBuffer ) foundCB = true;
        if ( res.name == "g_Output" && res.type == nvrhi::ResourceType::StructuredBuffer_UAV ) foundSB = true;
    }
    EXPECT_TRUE( foundCB );
    EXPECT_TRUE( foundSB );

    // Query Handle
    void* handle = vhGetShaderNvrhiHandle( shader );
    EXPECT_NE( handle, nullptr );

    vhDestroyShader( shader );
    vhFlush();

    // Query after destruction
    void* handleAfter = vhGetShaderNvrhiHandle( shader );
    EXPECT_EQ( handleAfter, nullptr );
}

// --------------------------------------------------------------------------
// State Tests
// --------------------------------------------------------------------------

UTEST( State, MultipleSlots )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state1 = {}, state2 = {};
    state1.SetViewRect( glm::vec4( 0, 0, 100, 100 ) );
    state2.SetViewRect( glm::vec4( 0, 0, 200, 200 ) );
    
    vhStateId id1 = 10, id2 = 20;
    vhSetState( id1, state1 );
    vhSetState( id2, state2 );
    vhFlush();
    
    vhState r1 = {}, r2 = {};
    ASSERT_TRUE( vhGetState( id1, r1 ) );
    ASSERT_TRUE( vhGetState( id2, r2 ) );
    
    EXPECT_EQ( r1.viewRect, state1.viewRect );
    EXPECT_EQ( r2.viewRect, state2.viewRect );
}

UTEST( State, InvalidId )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state = {};
    vhStateId nonExistent = 999999;
    
    // GetState should return false for non-existent ID
    ASSERT_FALSE( vhGetState( nonExistent, state ) );
}

UTEST( State, BasicSetGet )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state = {};
    state.SetViewRect( glm::vec4( 0, 0, 1280, 720 ) )
         .SetViewTransform( glm::mat4( 1.0f ), glm::mat4( 2.0f ) )
         .SetWorldTransform( glm::mat4( 3.0f ), 1 );
    
    vhStateId id = 1;
    ASSERT_TRUE( vhSetState( id, state ) );
    vhFlush(); // Wait for command to process
    
    vhState retrieved = {};
    ASSERT_TRUE( vhGetState( id, retrieved ) );
    
    EXPECT_EQ( retrieved.viewRect, state.viewRect );
    EXPECT_EQ( retrieved.viewMatrix, state.viewMatrix );
    EXPECT_EQ( retrieved.projMatrix, state.projMatrix );
    ASSERT_GT( retrieved.worldMatrix.size(), 0 );
    EXPECT_EQ( retrieved.worldMatrix[0], state.worldMatrix[0] );
}

UTEST( State, Attachments )
{
    vhState state = {};
    vhState::RenderTarget rt;
    rt.texture = 101;
    rt.mipLevel = 1;
    
    std::vector< vhState::RenderTarget > colors = { rt };
    vhState::RenderTarget depth;
    depth.texture = 201;
    
    state.SetAttachments( colors, depth );
    
    vhStateId id = 500;
    ASSERT_TRUE( vhSetState( id, state ) );
    vhFlush();
    
    vhState retrieved = {};
    ASSERT_TRUE( vhGetState( id, retrieved ) );
    
    ASSERT_EQ( retrieved.colourAttachment.size(), (size_t)1 );
    EXPECT_EQ( retrieved.colourAttachment[0].texture, 101u );
    EXPECT_EQ( retrieved.colourAttachment[0].mipLevel, 1u );
    EXPECT_EQ( retrieved.depthAttachment.texture, 201u );
}


UTEST( Sampler, GetSamplerDesc )
{
    // Case 1: Default/Zero Flags
    {
        uint64_t flags = 0;
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );
        
        EXPECT_TRUE( desc.minFilter );
        EXPECT_TRUE( desc.magFilter );
        EXPECT_TRUE( desc.mipFilter );
        EXPECT_EQ( desc.addressU, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_EQ( desc.addressV, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_EQ( desc.addressW, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_NEAR( desc.maxAnisotropy, 1.0f, 1e-5f );
        EXPECT_NEAR( desc.mipBias, 0.0f, 1e-5f );
        EXPECT_NEAR( desc.borderColor.r, 0.0f, 1e-5f );
        EXPECT_NEAR( desc.borderColor.a, 0.0f, 1e-5f );
        EXPECT_EQ( desc.reductionType, nvrhi::SamplerReductionType::Standard );
    }

    // Case 2: Point Sampling & Clamp
    {
        uint64_t flags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP;
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );
        
        EXPECT_FALSE( desc.minFilter );
        EXPECT_FALSE( desc.magFilter );
        EXPECT_FALSE( desc.mipFilter );
        EXPECT_EQ( desc.addressU, nvrhi::SamplerAddressMode::Clamp );
        EXPECT_EQ( desc.addressV, nvrhi::SamplerAddressMode::Clamp );
        EXPECT_EQ( desc.addressW, nvrhi::SamplerAddressMode::Clamp );
    }

    // Case 3: Anisotropy & MipBias
    {
        uint64_t flags = VRHI_SAMPLER_ANISOTROPY_16 | VRHI_SAMPLER_MIPBIAS( 2.5f );
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );
        
        EXPECT_NEAR( desc.maxAnisotropy, 16.0f, 1e-5f );
        EXPECT_NEAR( desc.mipBias, 2.5f, 0.01f );
    }

    // Case 4: Complex Mixed State
    {
        uint64_t flags = VRHI_SAMPLER_U_MIRROR | VRHI_SAMPLER_V_BORDER | VRHI_SAMPLER_MAG_POINT | VRHI_SAMPLER_MIPBIAS( -0.5f );
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );
        
        EXPECT_EQ( desc.addressU, nvrhi::SamplerAddressMode::Mirror );
        EXPECT_EQ( desc.addressV, nvrhi::SamplerAddressMode::Border );
        EXPECT_EQ( desc.addressW, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_FALSE( desc.magFilter );
        EXPECT_TRUE( desc.minFilter );
        EXPECT_NEAR( desc.mipBias, -0.5f, 0.01f );
    }

    // Case 5: Reduction/Compare
    {
        uint64_t flags = VRHI_SAMPLER_COMPARE_LESS;
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );
        
        EXPECT_EQ( desc.reductionType, nvrhi::SamplerReductionType::Comparison );
    }
}

UTEST( State, Extensions )
{
    // Test 1: Dirty Flags for Vertex/Index
    {
        vhState state;
        state.SetVertexBuffer( 1, 0 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_VERTEX_INDEX, VRHI_DIRTY_VERTEX_INDEX );
        state.dirty = 0;
        state.SetIndexBuffer( 2 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_VERTEX_INDEX, VRHI_DIRTY_VERTEX_INDEX );
    }

    // Test 2: Dirty Flags for Textures/Samplers
    {
        vhState state;
        state.SetTextures( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_TEXTURE_SAMPLERS, VRHI_DIRTY_TEXTURE_SAMPLERS );
        state.dirty = 0;
        state.SetSamplers( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_TEXTURE_SAMPLERS, VRHI_DIRTY_TEXTURE_SAMPLERS );
    }

    // Test 3: Dirty Flags for Buffers
    {
        vhState state;
        state.SetBuffers( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_BUFFERS, VRHI_DIRTY_BUFFERS );
    }

    // Test 4: Dirty Flags for Constants
    {
        vhState state;
        state.SetConstants( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_CONSTANTS, VRHI_DIRTY_CONSTANTS );
    }

    // Test 5: Dirty Flags for PushConstants
    {
        vhState state;
        state.SetPushConstants( glm::vec4(1.0f) );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_PUSH_CONSTANTS, VRHI_DIRTY_PUSH_CONSTANTS );
    }

    // Test 6: Dirty Flags for Program
    {
        vhState state;
        state.SetProgram( { 777 } );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_PROGRAM, VRHI_DIRTY_PROGRAM );
    }

    // Test 7: Dirty Flags for Uniforms
    {
        vhState state;
        state.SetUniforms( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_UNIFORMS, VRHI_DIRTY_UNIFORMS );
    }
}

UTEST( State, BackendPropagation )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    
    vhStateId id = 123; 
    vhState state;
    
    // Set some state
    state.SetPushConstants( glm::vec4( 1.1f, 2.2f, 3.3f, 4.4f ) );
    
    vhState::TextureBinding tex;
    tex.name = "PropTex";
    tex.slot = 3;
    tex.texture = 101;
    state.SetTextures( { tex } );
    
    vhSetState( id, state );
    vhFlush();
    
    // Verify backend state
    vhState backendState;
    EXPECT_TRUE( vhGetState( id, backendState ) );
    
    EXPECT_NEAR( backendState.pushConstants.x, 1.1f, 0.001f );
    EXPECT_NEAR( backendState.pushConstants.y, 2.2f, 0.001f );
    EXPECT_NEAR( backendState.pushConstants.z, 3.3f, 0.001f );
    EXPECT_NEAR( backendState.pushConstants.w, 4.4f, 0.001f );
    
    ASSERT_EQ( backendState.textures.size(), 1u );
    EXPECT_STREQ( backendState.textures[0].name, "PropTex" );
    EXPECT_EQ( backendState.textures[0].slot, 3 );
    EXPECT_EQ( backendState.textures[0].texture, 101u );

    // Verify it doesn't bleed to other states
    vhState otherState;
    EXPECT_FALSE( vhGetState( 999, otherState ) );
}

UTEST( State, IndividualAccessors )
{
    vhState state;
    
    // Texture with auto-resize
    {
        vhState::TextureBinding tex;
        tex.name = "ResizeTex";
        tex.slot = 10;
        state.SetTexture( 5, tex ); // Index 5, so size should be 6
        
        EXPECT_EQ( state.textures.size(), 6u );
        EXPECT_STREQ( state.textures[5].name, "ResizeTex" );
        EXPECT_EQ( state.textures[5].slot, 10 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_TEXTURE_SAMPLERS, VRHI_DIRTY_TEXTURE_SAMPLERS );
        
        // Get with resize
        state.GetTexture( 8 ); // Index 8, size -> 9
        EXPECT_EQ( state.textures.size(), 9u );
    }

    // Sampler with auto-resize
    {
        vhState::SamplerDefinition samp;
        samp.slot = 20;
        state.SetSampler( 3, samp ); 
        
        EXPECT_EQ( state.samplers.size(), 4u );
        EXPECT_EQ( state.samplers[3].slot, 20 );
        
        state.GetSampler( 6 );
        EXPECT_EQ( state.samplers.size(), 7u );
    }

    // Buffer with auto-resize
    {
        vhState::BufferBinding buf;
        buf.slot = 30;
        state.SetBuffer( 4, buf );
        
        EXPECT_EQ( state.buffers.size(), 5u );
        EXPECT_EQ( state.buffers[4].slot, 30 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_BUFFERS, VRHI_DIRTY_BUFFERS );
        
        state.GetBuffer( 5 );
        EXPECT_EQ( state.buffers.size(), 6u );
    }

    // Constant with auto-resize
    {
        vhState::ConstantBufferValue c;
        c.name = "ConstBuf";
        state.SetConstant( 2, c );
        
        EXPECT_EQ( state.constants.size(), 3u );
        EXPECT_STREQ( state.constants[2].name, "ConstBuf" );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_CONSTANTS, VRHI_DIRTY_CONSTANTS );
        
        state.GetConstant( 4 );
        EXPECT_EQ( state.constants.size(), 5u );
    }
}

UTEST( State, IndividualAttachments )
{
    vhState state;
    
    // Set color attachment at index 2 (forces resize)
    state.SetColorAttachment( 2, 101, 1, 2, nvrhi::Format::RGBA8_UNORM, true );
    
    EXPECT_EQ( state.colourAttachment.size(), 3u );
    EXPECT_EQ( state.colourAttachment[2].texture, 101u );
    EXPECT_EQ( state.colourAttachment[2].mipLevel, 1u );
    EXPECT_EQ( state.colourAttachment[2].arrayLayer, 2u );
    EXPECT_EQ( state.colourAttachment[2].formatOverride, nvrhi::Format::RGBA8_UNORM );
    EXPECT_TRUE( state.colourAttachment[2].readOnly );
    EXPECT_EQ( ( state.dirty & VRHI_DIRTY_ATTACHMENTS ), VRHI_DIRTY_ATTACHMENTS );
    
    // Reset dirty and check depth
    state.dirty = 0;
    state.SetDepthAttachment( 201, 0, 0, nvrhi::Format::D32, false );
    
    EXPECT_EQ( state.depthAttachment.texture, 201u );
    EXPECT_EQ( state.depthAttachment.formatOverride, nvrhi::Format::D32 );
    EXPECT_FALSE( state.depthAttachment.readOnly );
    EXPECT_EQ( ( state.dirty & VRHI_DIRTY_ATTACHMENTS ), VRHI_DIRTY_ATTACHMENTS );
}

UTEST_STATE();

int main( int argc, const char* const argv[] )
{
#ifndef NDEBUG
    g_vhInit.debug = true;
#endif

#ifdef _WIN32
    g_vhInit.shaderMakePath = "../tools/win_release";
    g_vhInit.shaderMakeSlangPath = "../tools/win_release";
#elif defined(__APPLE__)
    g_vhInit.shaderMakePath = "../tools/mac_release";
    g_vhInit.shaderMakeSlangPath = "../tools/mac_release";
#else
    g_vhInit.shaderMakePath = "../tools/linux_release";
    g_vhInit.shaderMakeSlangPath = "../tools/linux_release";
#endif

    int result = utest_main( argc, argv );

    if ( g_testInit )
    {
        vhShutdown( g_testInitQuiet );
        g_testInit = false;
    }

    return result;
}
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

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "utest.h"
#include "test.h"
#include <vrhi.h>
#include <vrhi_internal.h>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;
extern std::string vhGetDeviceInfo();
extern bool vhRunExe( const std::string& command, std::string& outOutput );

UTEST( RHI, Init )
{
    // If global init is active, shut it down to test clean init
    TestEnsureShutdown();

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
    EXPECT_TRUE( info.find( "not initialised" ) != std::string::npos );
}

UTEST( RHI, LogCallback )
{
    // Ensure clean state
    // Ensure clean state
    TestEnsureShutdown();

    std::vector<std::string> logs;
    int errorCount = 0;

    g_vhInit.debug = true;
    g_vhInit.fnLogCallback = [&]( bool err, const std::string& msg ) {
        if ( err ) errorCount++;
        logs.push_back( msg );
    };

    // We want logs here; pass quiet=false explicitly.
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
    // If global init is active, shut it down to test clean init
    TestEnsureShutdown();

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

UTEST( Device, DummyResources )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Test Buffer Retrieval
    nvrhi::BindingLayoutItem bufferItem = {};
    bufferItem.slot = 0;

    // Constant Buffer
    bufferItem.type = nvrhi::ResourceType::ConstantBuffer;
    auto cb = vhGetDummyBindingItem( bufferItem );
    EXPECT_EQ( cb.type, nvrhi::ResourceType::ConstantBuffer );
    EXPECT_NE( cb.resourceHandle, nullptr );

    // Structured Buffer SRV
    bufferItem.type = nvrhi::ResourceType::StructuredBuffer_SRV;
    auto sbSrv = vhGetDummyBindingItem( bufferItem );
    EXPECT_EQ( sbSrv.type, nvrhi::ResourceType::StructuredBuffer_SRV );
    EXPECT_NE( sbSrv.resourceHandle, nullptr );
    // Check that it returns the same omni-buffer
    EXPECT_EQ( cb.resourceHandle, sbSrv.resourceHandle );

    // Test Texture Retrieval
    nvrhi::BindingLayoutItem texItem = {};
    texItem.slot = 1;

    // Float 2D SRV
    texItem.type = nvrhi::ResourceType::Texture_SRV;
    auto texFloat2D = vhGetDummyBindingItem( texItem, nvrhi::Format::RGBA8_UNORM, nvrhi::TextureDimension::Texture2D );
    EXPECT_EQ( texFloat2D.type, nvrhi::ResourceType::Texture_SRV );
    EXPECT_NE( texFloat2D.resourceHandle, nullptr );
    EXPECT_EQ( texFloat2D.format, nvrhi::Format::RGBA8_UNORM );

    // Uint 2D SRV
    auto texUint2D = vhGetDummyBindingItem( texItem, nvrhi::Format::R8_UINT, nvrhi::TextureDimension::Texture2D );
    EXPECT_NE( texUint2D.resourceHandle, nullptr );
    // Ensure we got a different texture handle (or at least valid check, though strictly they might be different objects)
    EXPECT_NE( texFloat2D.resourceHandle, texUint2D.resourceHandle );

    // 3D Texture Fallback
    auto tex3D = vhGetDummyBindingItem( texItem, nvrhi::Format::RGBA8_UNORM, nvrhi::TextureDimension::Texture3D );
    EXPECT_NE( tex3D.resourceHandle, nullptr );

    // Test Sampler Retrieval
    nvrhi::BindingLayoutItem samplerItem = {};
    samplerItem.slot = 2;
    samplerItem.type = nvrhi::ResourceType::Sampler;
    auto samp = vhGetDummyBindingItem( samplerItem );
    EXPECT_EQ( samp.type, nvrhi::ResourceType::Sampler );
    EXPECT_NE( samp.resourceHandle, nullptr );
}

UTEST( Device, HashingReflectionMembers )
{
    // Test empty vector
    std::vector< vhReflectionMember > empty;
    uint64_t hash1 = vhHashReflectionMembers( empty );
    EXPECT_NE( hash1, 0u );

    // Test single member
    std::vector< vhReflectionMember > single;
    vhReflectionMember m1;
    m1.name = "test_member";
    m1.offset = 0;
    m1.size = 16;
    single.push_back( m1 );
    uint64_t hash2 = vhHashReflectionMembers( single );
    EXPECT_NE( hash2, 0u );
    EXPECT_NE( hash1, hash2 );

    // Test multiple members
    std::vector< vhReflectionMember > multiple;
    multiple.push_back( m1 );
    vhReflectionMember m2;
    m2.name = "another_member";
    m2.offset = 16;
    m2.size = 32;
    multiple.push_back( m2 );
    uint64_t hash3 = vhHashReflectionMembers( multiple );
    EXPECT_NE( hash3, 0u );
    EXPECT_NE( hash2, hash3 );

    // Test determinism
    uint64_t hash4 = vhHashReflectionMembers( multiple );
    EXPECT_EQ( hash3, hash4 );

    // Test sensitivity to order
    std::vector< vhReflectionMember > reversed;
    reversed.push_back( m2 );
    reversed.push_back( m1 );
    uint64_t hash5 = vhHashReflectionMembers( reversed );
    EXPECT_NE( hash3, hash5 );

    // Test sensitivity to member properties
    std::vector< vhReflectionMember > modified;
    vhReflectionMember m3 = m1;
    m3.offset = 100;
    modified.push_back( m3 );
    modified.push_back( m2 );
    uint64_t hash6 = vhHashReflectionMembers( modified );
    EXPECT_NE( hash3, hash6 );

    // Test with empty name
    std::vector< vhReflectionMember > emptyName;
    vhReflectionMember m4;
    m4.name = "";
    m4.offset = 0;
    m4.size = 4;
    emptyName.push_back( m4 );
    uint64_t hash7 = vhHashReflectionMembers( emptyName );
    EXPECT_NE( hash7, 0u );
}

UTEST( Device, QueryFeatureSupport )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Test basic feature queries that should return deterministic values
    EXPECT_TRUE( vhQueryFeatureSupport( nvrhi::Feature::ComputeQueue ) ); // Vulkan always has this
    // Test any bool is valid for this feature
    bool deferredSupport = vhQueryFeatureSupport( nvrhi::Feature::DeferredCommandLists );
    // Just verify it returns a valid bool - don't assume specific support

    // Test feature with info struct
    nvrhi::VariableRateShadingFeatureInfo vrsInfo = {};
    bool vrsSupported = vhQueryFeatureSupport( nvrhi::Feature::VariableRateShading, &vrsInfo, sizeof(vrsInfo) );
    if ( vrsSupported )
    {
        EXPECT_GT( vrsInfo.shadingRateImageTileSize, 0 ); // Should have valid tile size if supported
    }
}

UTEST( Device, QueryFormatSupport )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Test common format queries
    nvrhi::FormatSupport rgba8Support = vhQueryFormatSupport( nvrhi::Format::RGBA8_UNORM );
    EXPECT_NE( rgba8Support, nvrhi::FormatSupport::None ); // Common format should be supported
    EXPECT_TRUE( ( rgba8Support & nvrhi::FormatSupport::Texture ) != nvrhi::FormatSupport::None );
    EXPECT_TRUE( ( rgba8Support & nvrhi::FormatSupport::ShaderSample ) != nvrhi::FormatSupport::None );

    // Test format support flags
    nvrhi::FormatSupport depthSupport = vhQueryFormatSupport( nvrhi::Format::D32S8 );
    EXPECT_TRUE( ( depthSupport & nvrhi::FormatSupport::DepthStencil ) != nvrhi::FormatSupport::None );

    nvrhi::FormatSupport floatSupport = vhQueryFormatSupport( nvrhi::Format::R32_FLOAT );
    EXPECT_TRUE( ( floatSupport & nvrhi::FormatSupport::ShaderLoad ) != nvrhi::FormatSupport::None );
}
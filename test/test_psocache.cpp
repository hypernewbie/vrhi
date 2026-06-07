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

// Tests + benchmark for vhGetPSOCache / vhInitData::psoCacheInitialData (Vulkan driver pipeline cache).
//
// Six tests:
//   1. RHI.PSOCache_RoundTrip              — extract blob, re-init with it, ensure device/PSOs work.
//   2. RHI.PSOCache_StaleBlobIgnored       — feed garbage blob, init must still succeed.
//   3. RHI.PSOCache_NoFlushSizeMatchesBlob — flush-free snapshot and size query agree with the blob.
//   4. RHI.PSOCache_KeyStable              — device cache key is populated and deterministic.
//   5. RHI.PSOCache_ConcurrentQueryDuringPipelineCreation — query off-thread while the backend compiles.
//   6. RHI.PSOCache_ValidateKeyMismatchDiscardsSeed — psoCacheValidateKey rejects a foreign blob.
//
// One benchmark:
//   7. Bench.PSOCache_ColdVsWarm    — compile N compute PSOs cold, again warm with seeded cache,
//                                     print timings.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "test.h"
#include <vrhi_internal.h>
#include <vrhi.h>

extern bool g_testInit;
extern bool g_testInitQuiet;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

namespace
{
    // Build a unique compute shader source per index. Distinct thread group dimensions
    // generate distinct SPIRV (and thus distinct driver pipelines) without needing any
    // descriptor bindings.
    std::string MakeUniqueComputeSource( int idx )
    {
        const int tx = 8 + ( idx % 4 );          // 8..11
        const int ty = 1 + ( ( idx / 4 ) % 4 );  // 1..4
        char buf[256];
        std::snprintf( buf, sizeof( buf ),
            "[numthreads(%d, %d, 1)]\n"
            "void main(uint3 id : SV_DispatchThreadID) {}\n",
            tx, ty );
        return std::string( buf );
    }

    // Compile N distinct compute PSOs straight through the NVRHI device. Bypasses VRHI's queued
    // backend so the cache work happens synchronously on the main thread — exactly the path you
    // care about when measuring driver pipeline cache cost.
    bool BuildNComputePSOsDirect( int N, std::vector< nvrhi::ComputePipelineHandle >* outPsos = nullptr )
    {
        if ( outPsos ) outPsos->clear();
        if ( !g_vhDevice ) return false;

        // Empty binding layout — we only care about the PSO compile cost.
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        nvrhi::BindingLayoutHandle layout = g_vhDevice->createBindingLayout( layoutDesc );
        if ( !layout ) return false;

        for ( int i = 0; i < N; ++i )
        {
            char shaderName[64];
            std::snprintf( shaderName, sizeof( shaderName ), "PSOCache_CS_%d", i );

            std::string src = MakeUniqueComputeSource( i );
            std::vector< uint32_t > spirv;
            std::string err;
            bool ok = vhCompileShader( shaderName, src.c_str(),
                VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
                spirv, "main", {}, {}, &err );
            if ( !ok )
            {
                UTEST_PRINTF( "PSOCache: shader compile failed (%s): %s\n", shaderName, err.c_str() );
                return false;
            }

            nvrhi::ShaderDesc shDesc;
            shDesc.shaderType = nvrhi::ShaderType::Compute;
            shDesc.entryName = "main";
            shDesc.debugName = shaderName;
            nvrhi::ShaderHandle csHandle = g_vhDevice->createShader( shDesc, spirv.data(), spirv.size() * sizeof( uint32_t ) );
            if ( !csHandle ) return false;

            nvrhi::ComputePipelineDesc psoDesc;
            psoDesc.CS = csHandle;
            psoDesc.bindingLayouts.push_back( layout );

            nvrhi::ComputePipelineHandle pso = g_vhDevice->createComputePipeline( psoDesc );
            if ( !pso ) return false;
            if ( outPsos ) outPsos->push_back( pso );
        }

        return true;
    }
}

// --------------------------------------------------------------------------
// Roundtrip: cold extract, warm seed, both runs work.
// --------------------------------------------------------------------------

UTEST( RHI, PSOCache_RoundTrip )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }

    TestEnsureShutdown();

    const int N = 4;

    // Cold pass: empty cache in, build PSOs, extract blob.
    g_vhInit.psoCacheInitialData.clear();
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );
    ASSERT_TRUE( BuildNComputePSOsDirect( N ) );

    std::vector< uint8_t > blob;
    bool got = vhGetPSOCache( blob );
    vhShutdown( g_testInitQuiet );

    ASSERT_TRUE( got );
    ASSERT_GT( blob.size(), ( size_t ) 0 );

    // Warm pass: feed the blob back in. Init must succeed; PSOs must still build.
    g_vhInit.psoCacheInitialData = blob;
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );
    EXPECT_TRUE( BuildNComputePSOsDirect( N ) );

    std::vector< uint8_t > blob2;
    bool got2 = vhGetPSOCache( blob2 );
    vhShutdown( g_testInitQuiet );

    EXPECT_TRUE( got2 );
    EXPECT_GT( blob2.size(), ( size_t ) 0 );

    g_vhInit.psoCacheInitialData.clear();
}

// --------------------------------------------------------------------------
// Stale / garbage blob must be ignored, not crash.
// --------------------------------------------------------------------------

UTEST( RHI, PSOCache_StaleBlobIgnored )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }

    TestEnsureShutdown();

    g_vhInit.psoCacheInitialData.assign( 4096, 0xCC );
    vhInit( g_testInitQuiet );
    EXPECT_NE( g_vhDevice.Get(), nullptr );
    // Device must remain usable: build a PSO.
    EXPECT_TRUE( BuildNComputePSOsDirect( 1 ) );
    vhShutdown( g_testInitQuiet );

    g_vhInit.psoCacheInitialData.clear();
}

// --------------------------------------------------------------------------
// Flush-free snapshot and size query agree with the flushed blob.
// --------------------------------------------------------------------------

UTEST( RHI, PSOCache_NoFlushSizeMatchesBlob )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }

    TestEnsureShutdown();

    g_vhInit.psoCacheInitialData.clear();
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );
    ASSERT_TRUE( BuildNComputePSOsDirect( 4 ) );

    std::vector< uint8_t > blob;
    bool got = vhGetPSOCache( blob, false );   // no device idle
    size_t querySize = vhGetPSOCacheSize();

    vhShutdown( g_testInitQuiet );

    ASSERT_TRUE( got );
    ASSERT_GT( blob.size(), ( size_t ) 0 );
    EXPECT_EQ( querySize, blob.size() );

    g_vhInit.psoCacheInitialData.clear();
}

// --------------------------------------------------------------------------
// Device cache key is populated and stable across calls.
// --------------------------------------------------------------------------

UTEST( RHI, PSOCache_KeyStable )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }

    TestEnsureShutdown();

    g_vhInit.psoCacheInitialData.clear();
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );

    vhPSOCacheKey a = vhGetPSOCacheKey();
    vhPSOCacheKey b = vhGetPSOCacheKey();

    vhShutdown( g_testInitQuiet );

    EXPECT_NE( a.vendorID, 0u );
    EXPECT_EQ( a.vendorID, b.vendorID );
    EXPECT_EQ( a.deviceID, b.deviceID );
    EXPECT_EQ( a.driverVersion, b.driverVersion );
    EXPECT_EQ( a.apiVersion, b.apiVersion );
    EXPECT_EQ( 0, memcmp( a.pipelineCacheUUID, b.pipelineCacheUUID, sizeof( a.pipelineCacheUUID ) ) );

    g_vhInit.psoCacheInitialData.clear();
}

// --------------------------------------------------------------------------
// Querying the cache off-thread while the backend compiles pipelines must not crash.
// --------------------------------------------------------------------------

UTEST( RHI, PSOCache_ConcurrentQueryDuringPipelineCreation )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }

    TestEnsureShutdown();
    g_vhInit.psoCacheInitialData.clear();
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );

    // Distinct compute programs; each first dispatch makes the backend thread compile a new driver
    // pipeline, mutating the same cache the worker thread queries.
    const int N = 24;
    std::vector< vhShader > shaders;
    for ( int i = 0; i < N; ++i )
    {
        char name[64];
        std::snprintf( name, sizeof( name ), "CS_Concurrent_%d", i );
        std::vector< uint32_t > spirv;
        std::string err;
        ASSERT_TRUE( vhCompileShader( name, MakeUniqueComputeSource( i ).c_str(),
            VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
        vhShader cs = vhAllocShader();
        vhCreateShader( cs, name, VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
        shaders.push_back( cs );
    }

    std::atomic< bool > stop = false;
    std::atomic< int > queries = 0;
    std::thread worker( [&]() {
        std::vector< uint8_t > blob;
        while ( !stop.load() )
        {
            vhGetPSOCacheSize();
            vhGetPSOCache( blob, false );
            queries.fetch_add( 1 );
        }
    } );

    for ( int pass = 0; pass < 4; ++pass )
    {
        for ( int i = 0; i < N; ++i )
        {
            vhState state;
            state.SetProgram( vhCreateComputeProgram( shaders[i] ) );
            vhStateId sid = 9000 + i;
            vhSetState( sid, state );
            vhDispatch( sid, { 1, 1, 1 } );
        }
        vhFinish();
    }

    stop.store( true );
    worker.join();

    EXPECT_GT( queries.load(), 0 );

    std::vector< uint8_t > finalBlob;
    EXPECT_TRUE( vhGetPSOCache( finalBlob, true ) );
    EXPECT_GT( finalBlob.size(), ( size_t ) 0 );

    vhShutdown( g_testInitQuiet );
    g_vhInit.psoCacheInitialData.clear();
}

// --------------------------------------------------------------------------
// psoCacheValidateKey discards a seed blob whose key doesn't match the device.
// --------------------------------------------------------------------------

UTEST( RHI, PSOCache_ValidateKeyMismatchDiscardsSeed )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }
    if ( TestIsSoftwareVulkan() ) { UTEST_SKIP( "Seeded vs cold cache size is not observable on software Vulkan." ); }

    TestEnsureShutdown();

    // Cold pass: build PSOs, capture a blob valid for this device plus its key.
    g_vhInit.psoCacheInitialData.clear();
    g_vhInit.psoCacheValidateKey = {};
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );
    ASSERT_TRUE( BuildNComputePSOsDirect( 8 ) );

    std::vector< uint8_t > blob;
    bool got = vhGetPSOCache( blob );
    vhPSOCacheKey key = vhGetPSOCacheKey();
    vhShutdown( g_testInitQuiet );

    ASSERT_TRUE( got );
    ASSERT_GT( blob.size(), ( size_t ) 0 );

    // Matching key: the blob is seeded, so the driver cache starts populated.
    g_vhInit.psoCacheInitialData = blob;
    g_vhInit.psoCacheValidateKey = key;
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );
    size_t matchedSize = vhGetPSOCacheSize();
    vhShutdown( g_testInitQuiet );

    // Mismatching key: vrhi drops the seed, so the driver cache starts empty.
    vhPSOCacheKey wrongKey = key;
    wrongKey.pipelineCacheUUID[0] ^= 0xFF;
    g_vhInit.psoCacheInitialData = blob;
    g_vhInit.psoCacheValidateKey = wrongKey;
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );
    size_t mismatchedSize = vhGetPSOCacheSize();
    EXPECT_TRUE( BuildNComputePSOsDirect( 1 ) );
    vhShutdown( g_testInitQuiet );

    EXPECT_GT( matchedSize, mismatchedSize );

    g_vhInit.psoCacheInitialData.clear();
    g_vhInit.psoCacheValidateKey = {};
}

// --------------------------------------------------------------------------
// Bench: cold vs warm PSO compile time.
// --------------------------------------------------------------------------

UTEST( Bench, PSOCache_ColdVsWarm )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Driver pipeline cache requires a real Vulkan device." );
    }

    TestEnsureShutdown();

    const int N = 16;

    // ---- COLD ----
    g_vhInit.psoCacheInitialData.clear();
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );

    auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE( BuildNComputePSOsDirect( N ) );
    g_vhDevice->waitForIdle();
    auto coldUs = std::chrono::duration< double, std::micro >(
        std::chrono::steady_clock::now() - t0 ).count();

    std::vector< uint8_t > blob;
    bool got = vhGetPSOCache( blob );
    vhShutdown( g_testInitQuiet );

    ASSERT_TRUE( got );
    ASSERT_GT( blob.size(), ( size_t ) 0 );

    // ---- WARM ----
    g_vhInit.psoCacheInitialData = blob;
    vhInit( g_testInitQuiet );
    ASSERT_NE( g_vhDevice.Get(), nullptr );

    auto t1 = std::chrono::steady_clock::now();
    ASSERT_TRUE( BuildNComputePSOsDirect( N ) );
    g_vhDevice->waitForIdle();
    auto warmUs = std::chrono::duration< double, std::micro >(
        std::chrono::steady_clock::now() - t1 ).count();

    vhShutdown( g_testInitQuiet );

    g_vhInit.psoCacheInitialData.clear();

    const double ratio = ( coldUs > 0.0 ) ? ( 100.0 * warmUs / coldUs ) : 0.0;
    UTEST_PRINTF( "[VRHI_BENCH] %-48s N=%d cold=%.2fms warm=%.2fms warm/cold=%.1f%% blob=%zuKB\n",
        "PSOCache_ColdVsWarm",
        N,
        coldUs / 1000.0,
        warmUs / 1000.0,
        ratio,
        blob.size() / 1024 );
}

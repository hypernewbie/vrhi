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
    OR OTHER LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "test.h"
#include "vrhi.h"

extern bool g_testInitQuiet;

struct Bindless {};
UTEST_F_SETUP( Bindless )
{
    g_vhInit.markers = true;
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    if ( !g_captureActive )
    {
        vhCaptureStart();
        g_captureActive = true;
    }
    vhBeginMarker( utest_test_name );
}
UTEST_F_TEARDOWN( Bindless )
{
    vhEndMarker();
}

UTEST_F( Bindless, Supported )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Null mode has no real features" );
    EXPECT_TRUE( g_vhDeviceInfo.bindless );
    EXPECT_GT( g_vhDeviceInfo.maxBindlessSampledImages,  0u );
    EXPECT_GT( g_vhDeviceInfo.maxBindlessStorageImages,  0u );
    EXPECT_GT( g_vhDeviceInfo.maxBindlessStorageBuffers, 0u );
    EXPECT_GT( g_vhDeviceInfo.maxBindlessUniformBuffers, 0u );
    EXPECT_GT( g_vhDeviceInfo.maxBindlessSamplers,       0u );
    EXPECT_GT( g_vhDeviceInfo.maxBoundDescriptorSets,    0u );
    EXPECT_GT( g_vhDeviceInfo.maxPerStageResources,      0u );
    EXPECT_TRUE( g_vhDeviceInfo.bindlessUpdateAfterBind & VRHI_UAB_SAMPLED_IMAGE );
    EXPECT_GT( g_vhDeviceInfo.maxBindlessSampledImagesUAB, 0u );
}


UTEST_F( Bindless, AllocAndDestroy )
{
    vhDescriptorTable t0 = vhAllocDescriptorTable();
    EXPECT_NE( t0, VRHI_INVALID_HANDLE );

    vhDescriptorTable t1 = vhAllocDescriptorTable();
    EXPECT_NE( t1, VRHI_INVALID_HANDLE );
    EXPECT_NE( t0, t1 );

    vhDestroyDescriptorTable( t0 );
    vhDestroyDescriptorTable( t1 );
    vhFinish();
}

UTEST_F( Bindless, Create_BasicTable )
{
    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 16;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );

    vhDescriptorTable result = vhCreateDescriptorTable( t, desc );
    EXPECT_EQ( result, t );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Destroy_InvalidHandle_IsNoop )
{
    vhDestroyDescriptorTable( VRHI_INVALID_HANDLE );
    vhFinish();
}

UTEST_F( Bindless, NvrhiHandles_Returned )
{
    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc );
    vhFinish();

    if ( !g_vhInit.nullMode )
    {
        nvrhi::DescriptorTableHandle tableHandle = vhGetDescriptorTableNvrhiHandle( t );
        EXPECT_TRUE( tableHandle != nullptr );

        nvrhi::BindingLayoutHandle layoutHandle = vhGetDescriptorTableLayoutNvrhiHandle( t );
        EXPECT_TRUE( layoutHandle != nullptr );
    }

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Reflect_BindlessArray_NoAssert )
{
    const char* csSource = R"(
        Texture2D<float> g_Textures[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = 0.0;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "CS_BindlessReflect",
        csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirv, "main", {}, {}, &error
    );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BindlessReflect", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhDestroyShader( cs );
    vhFinish();
}

UTEST_F( Bindless, Reflect_BindlessResourceFlagged )
{
    const char* csSource = R"(
        Texture2D<float> g_T[] : register(t0, VRHI_BINDLESS_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_T[NonUniformResourceIndex(id.x)][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "CS_BindlessReflectFlagged",
        csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirv, "main", {}, {}, &error
    );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BindlessReflectFlagged", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
    vhFinish();

    std::vector< vhShaderReflectionResource > resources;
    vhGetShaderInfo( cs, nullptr, &resources );

    const vhShaderReflectionResource* bindlessRes = nullptr;
    for ( const auto& r : resources )
    {
        if ( r.bindless )
        {
            bindlessRes = &r;
            break;
        }
    }

    ASSERT_TRUE( bindlessRes != nullptr );
    EXPECT_EQ( bindlessRes->set, ( uint32_t ) VRHI_DESCRIPTOR_SET_BINDLESS );
    EXPECT_EQ( bindlessRes->arraySize, 0u ); // unbounded array reflects as count 0
    EXPECT_EQ( bindlessRes->type, nvrhi::ResourceType::Texture_SRV );
    EXPECT_EQ( bindlessRes->name, std::string( "g_T" ) );

    vhDestroyShader( cs );
    vhFinish();
}

UTEST_F( Bindless, Texture_SRV_Index0 )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc );
    vhFinish();

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "BindlessTex0", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, 0 );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, tex );

    nvrhi::DescriptorTableHandle handle = vhGetDescriptorTableNvrhiHandle( t );
    EXPECT_TRUE( handle != nullptr );

    vhDestroyTexture( tex );
    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Clear_SetsNullDescriptor )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc );
    vhFinish();

    vhDescriptorTableClear( t, 0, nvrhi::ResourceType::Texture_SRV );
    vhFinish();

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, EndToEnd_ComputeSamplesBindlessTexture )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhFlush();

    vhTexture texA = vhAllocTexture();
    vhTexture texB = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 50, 16 );
        vhCreateTexture2D( texA, "BindlessSrcA", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );

        vhMem* dataB = vhAllocMem( 16 );
        memset( dataB->data(), 200, 16 );
        vhCreateTexture2D( texB, "BindlessSrcB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataB );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "BindlessOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhDescriptorTable table = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhDescriptorTableSetTexture( table, 0, texA );
    vhDescriptorTableSetTexture( table, 1, texB );
    vhFinish();

    const char* csSource = R"(
        Texture2D<float> g_Textures[] : register(t0, VRHI_BINDLESS_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint index = id.x & 1;
            g_Out[id.xy] = g_Textures[NonUniformResourceIndex(index)][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_BindlessSample", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BindlessSample", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );

    state.SetDescriptorTable( 0, table );

    vhStateId sid = 920;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), 16u );
    for ( int row = 0; row < 4; ++row )
    {
        for ( int col = 0; col < 4; ++col )
        {
            const uint8_t expected = ( col & 1 ) ? 200 : 50;
            EXPECT_NEAR( readData[row * 4 + col], expected, 1 );
        }
    }

    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( table );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, BindlessSpace_Define_InShader )
{
    const char* csSource = R"(
        cbuffer Params : register(b0, VRHI_STAGE_SPACE) { uint g_Index; };
        Texture2D<float> g_Textures[] : register(t0, VRHI_BINDLESS_SPACE);
        [[vk::image_format("r32f")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = float(g_Index);
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "CS_BindlessSpace",
        csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirv, "main", {}, {}, &error
    );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BindlessSpace", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
    vhDestroyShader( cs );
    vhFinish();
}

UTEST_F( Bindless, UpdateAfterBind_WriteAcrossFrames )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );
    if ( g_vhDeviceInfo.name.find( "CPU" ) != std::string::npos ) UTEST_SKIP( "Requires real GPU" );
    if ( !( g_vhDeviceInfo.bindlessUpdateAfterBind & VRHI_UAB_SAMPLED_IMAGE ) ) UTEST_SKIP( "No sampled-image update-after-bind support" );

    vhFlush();

    vhTexture texA = vhAllocTexture();
    vhTexture texB = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 50, 16 );
        vhCreateTexture2D( texA, "UABSrcA", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );

        vhMem* dataB = vhAllocMem( 16 );
        memset( dataB->data(), 200, 16 );
        vhCreateTexture2D( texB, "UABSrcB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataB );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "UABOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhDescriptorTable table = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16, 0, true );

    const char* csSource = R"(
        Texture2D<float> g_Textures[] : register(t0, VRHI_BINDLESS_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint index = id.x & 1;
            g_Out[id.xy] = g_Textures[NonUniformResourceIndex(index)][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_UABSample", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_UABSample", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, table );

    vhStateId sid = 921;

    vhDescriptorTableSetTexture( table, 0, texA );
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFlush();

    vhDescriptorTableSetTexture( table, 1, texB );
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), 16u );
    for ( int row = 0; row < 4; ++row )
    {
        for ( int col = 0; col < 4; ++col )
        {
            const uint8_t expected = ( col & 1 ) ? 200 : 50;
            EXPECT_NEAR( readData[row * 4 + col], expected, 1 );
        }
    }

    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( table );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// Allocator tests
// --------------------------------------------------------------------------

UTEST_F( Bindless, Allocator_Basic )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 16;

    uint32_t i0 = alloc.Alloc();
    uint32_t i1 = alloc.Alloc();
    uint32_t i2 = alloc.Alloc();
    EXPECT_EQ( i0, 0u );
    EXPECT_EQ( i1, 1u );
    EXPECT_EQ( i2, 2u );
    EXPECT_EQ( alloc.Allocated(), 3u );

    alloc.Free( i1 );
    EXPECT_EQ( alloc.Allocated(), 2u );
    uint32_t i3 = alloc.Alloc();
    EXPECT_EQ( i3, 3u );
    EXPECT_EQ( alloc.Allocated(), 3u );

    alloc.Free( i0 );
    alloc.Free( i2 );
    alloc.Free( i3 );
    EXPECT_EQ( alloc.Allocated(), 0u );

    for ( uint32_t i = 0; i < alloc.kDeferredDepth; i++ ) alloc.Step();
    EXPECT_EQ( alloc.Allocated(), 0u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Full_ReturnsInvalid )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 2 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 2;

    EXPECT_NE( alloc.Alloc(), VRHI_INVALID_HANDLE );
    EXPECT_NE( alloc.Alloc(), VRHI_INVALID_HANDLE );
    EXPECT_EQ( alloc.Alloc(), VRHI_INVALID_HANDLE );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Growable )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 8 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 2;
    alloc.maxCapacity = 8;
    alloc.growable = true;

    EXPECT_EQ( alloc.Alloc(), 0u );
    EXPECT_EQ( alloc.Alloc(), 1u );
    uint32_t i2 = alloc.Alloc();
    EXPECT_NE( i2, VRHI_INVALID_HANDLE );
    EXPECT_EQ( i2, 2u );
    EXPECT_GE( alloc.capacity, 4u );

    alloc.Reset();
    EXPECT_EQ( alloc.Allocated(), 0u );
    EXPECT_EQ( alloc.Alloc(), 0u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_DeferredFree )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 16;

    alloc.Alloc();                 // index 0, stays live
    uint32_t i1 = alloc.Alloc();   // index 1
    EXPECT_EQ( alloc.Allocated(), 2u );

    alloc.Free( i1 );
    EXPECT_EQ( alloc.Allocated(), 1u );

    // Freed index is not reusable yet: Alloc() must hand out a fresh slot, not i1.
    uint32_t fresh = alloc.Alloc();
    EXPECT_NE( fresh, i1 );
    EXPECT_EQ( fresh, 2u );

    // Still not reclaimed after kDeferredDepth-1 Steps.
    for ( uint32_t i = 0; i < alloc.kDeferredDepth - 1; i++ ) alloc.Step();
    EXPECT_NE( alloc.Alloc(), i1 );

    // The kDeferredDepth'th Step reclaims it.
    alloc.Step();
    EXPECT_EQ( alloc.Alloc(), i1 );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_DeferredFree_Staggered )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 16;

    uint32_t a = alloc.Alloc();
    uint32_t b = alloc.Alloc();
    alloc.Free( a );

    alloc.Step();
    alloc.Free( b );

    for ( uint32_t i = 0; i < alloc.kDeferredDepth - 1; i++ ) alloc.Step();
    EXPECT_EQ( alloc.Alloc(), a );

    alloc.Step();
    EXPECT_EQ( alloc.Alloc(), b );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Reset_ClearsDeferred )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 16;

    uint32_t a = alloc.Alloc();
    alloc.Free( a );
    EXPECT_EQ( alloc.Allocated(), 0u );

    alloc.Reset();
    EXPECT_EQ( alloc.Allocated(), 0u );
    EXPECT_EQ( alloc.Alloc(), 0u );

    for ( uint32_t i = 0; i < alloc.kDeferredDepth; i++ ) alloc.Step();
    uint32_t after = alloc.Alloc();
    EXPECT_NE( after, a );
    EXPECT_EQ( alloc.Allocated(), 2u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Step_EmptyNoop )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 16;

    for ( uint32_t i = 0; i < 10; i++ ) alloc.Step();
    EXPECT_EQ( alloc.Allocated(), 0u );
    EXPECT_EQ( alloc.Alloc(), 0u );
    EXPECT_EQ( alloc.Alloc(), 1u );
    EXPECT_EQ( alloc.Alloc(), 2u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Full_Boundary )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 2 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 2;

    alloc.Alloc();
    alloc.Alloc();
    EXPECT_EQ( alloc.Alloc(), VRHI_INVALID_HANDLE );

    alloc.Free( 0u );
    EXPECT_EQ( alloc.Alloc(), VRHI_INVALID_HANDLE );

    for ( uint32_t i = 0; i < alloc.kDeferredDepth - 1; i++ ) alloc.Step();
    EXPECT_EQ( alloc.Alloc(), VRHI_INVALID_HANDLE );

    alloc.Step();
    EXPECT_EQ( alloc.Alloc(), 0u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Growable_DeferredFree )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 8 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 2;
    alloc.maxCapacity = 8;
    alloc.growable = true;

    alloc.Alloc(); // 0
    alloc.Alloc(); // 1
    alloc.Free( 1u );

    uint32_t grow = alloc.Alloc();
    EXPECT_NE( grow, 1u );
    EXPECT_GT( alloc.capacity, 2u );

    for ( uint32_t i = 0; i < alloc.kDeferredDepth; i++ ) alloc.Step();
    EXPECT_EQ( alloc.Alloc(), 1u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Allocator_Churn_Invariants )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 32 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 32;

    std::vector< uint32_t > live;
    for ( int frame = 0; frame < 80; frame++ )
    {
        alloc.Step();

        if ( !live.empty() && ( frame & 1 ) )
        {
            uint32_t idx = live.back();
            live.pop_back();
            alloc.Free( idx );
        }

        if ( live.size() < 10 )
        {
            uint32_t idx = alloc.Alloc();
            EXPECT_NE( idx, VRHI_INVALID_HANDLE );
            for ( uint32_t other : live ) EXPECT_NE( idx, other );
            live.push_back( idx );
        }

        EXPECT_EQ( alloc.Allocated(), ( uint32_t ) live.size() );
    }

    vhDestroyDescriptorTable( t );
    vhFinish();
}

// --------------------------------------------------------------------------
// Descriptor-table API coverage
// --------------------------------------------------------------------------

UTEST_F( Bindless, Capacity_ReportsConfigured )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 24 );
    vhFinish();

    EXPECT_EQ( vhDescriptorTableCapacity( t ), 24u );

    vhResizeDescriptorTable( t, 12, true );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 12u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, Resize_NoKeepContents )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhResizeDescriptorTable( t, 12, false );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 12u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

// --------------------------------------------------------------------------
// Upgraded functional tests (readback-verified)
// --------------------------------------------------------------------------

UTEST_F( Bindless, Buffer_SRV_WriteAndReadback )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::StructuredBuffer_SRV, 4 );
    vhFinish();

    uint32_t data[4] = { 10, 20, 30, 40 };
    vhMem* bufData = vhAllocMem( sizeof( data ) );
    memcpy( bufData->data(), data, sizeof( data ) );
    vhBuffer buf = vhAllocBuffer();
    vhCreateStorageBuffer( buf, "BindlessBuf", bufData, sizeof( data ), VRHI_BUFFER_COMPUTE_READ_WRITE, sizeof( uint32_t ) );
    vhDescriptorTableSetBuffer( t, 0, buf, nvrhi::ResourceType::StructuredBuffer_SRV );
    vhFinish();

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "BufOut", { 1, 1 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        StructuredBuffer<uint> g_Buf[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = float(g_Buf[0][0] & 0xFF) / 255.0;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_BindlessBuf", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BindlessBuf", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 922;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), 1u );
    EXPECT_NEAR( readData[0], ( uint8_t ) 10, 1 );

    vhDestroyTexture( outTex );
    vhDestroyBuffer( buf );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// Resize down to 8 with keepContents: slot 0 written before the resize must survive,
// and a slot within the new capacity must remain writable. (The Vulkan backend keeps the
// table at maxCapacity, so this exercises vrhi's capacity bookkeeping and slot persistence.)
UTEST_F( Bindless, Resize_KeepContents )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhTexture tex0 = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 4 );
        memset( data->data(), 100, 4 );
        vhCreateTexture2D( tex0, "ResizeTex0", { 2, 2 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }
    vhDescriptorTableSetTexture( t, 0, tex0 );

    vhTexture tex8 = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 4 );
        memset( data->data(), 200, 4 );
        vhCreateTexture2D( tex8, "ResizeTex8", { 2, 2 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ResizeOut", { 1, 1 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhResizeDescriptorTable( t, 8, true );
    vhFinish();

    vhDescriptorTableSetTexture( t, 7, tex8 );

    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            float a = g_Tex[0][uint2(0, 0)].x;
            float b = g_Tex[7][uint2(0, 0)].x;
            g_Out[id.xy] = (a + b) * 0.5;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_BindlessResize", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BindlessResize", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 923;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), 1u );
    const uint8_t expected = ( uint8_t )( ( 100 + 200 ) / 2 );
    EXPECT_NEAR( readData[0], expected, 2 );

    vhDestroyTexture( tex0 );
    vhDestroyTexture( tex8 );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, MultipleResourceTypes )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable texTable = vhAllocDescriptorTable();
    {
        nvrhi::BindlessLayoutDesc desc;
        desc.visibility = nvrhi::ShaderType::All;
        desc.maxCapacity = 8;
        desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
        vhCreateDescriptorTable( texTable, desc );
    }

    vhDescriptorTable bufTable = vhAllocDescriptorTable();
    {
        nvrhi::BindlessLayoutDesc desc;
        desc.visibility = nvrhi::ShaderType::All;
        desc.maxCapacity = 8;
        desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::StructuredBuffer_SRV( 0 ) );
        vhCreateDescriptorTable( bufTable, desc );
    }
    vhFinish();

    for ( int i = 0; i < 4; i++ )
    {
        vhTexture tex = vhAllocTexture();
        vhCreateTexture2D( tex, "MultiTex", { 2, 2 }, 1, nvrhi::Format::R8_UNORM, 0 );
        vhDescriptorTableSetTexture( texTable, i, tex );
        vhDestroyTexture( tex );
    }

    for ( int i = 0; i < 4; i++ )
    {
        vhBuffer buf = vhAllocBuffer();
        vhCreateStorageBuffer( buf, "MultiBufSRV", nullptr, sizeof( uint32_t ) * 4, VRHI_BUFFER_COMPUTE_READ_WRITE, sizeof( uint32_t ) );
        vhDescriptorTableSetBuffer( bufTable, i, buf, nvrhi::ResourceType::StructuredBuffer_SRV );
        vhDestroyBuffer( buf );
    }
    vhFinish();

    vhDestroyDescriptorTable( texTable );
    vhDestroyDescriptorTable( bufTable );
    vhFinish();
}

// --------------------------------------------------------------------------
// End-to-end: allocator slot reuse across frames, verified on the GPU
// --------------------------------------------------------------------------

UTEST_F( Bindless, EndToEnd_AllocatorDeferredReuse )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    vhBindlessAllocator alloc;
    alloc.table = t;
    alloc.capacity = 16;

    vhTexture tex50 = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), 50, 16 );
        vhCreateTexture2D( tex50, "Reuse50", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }

    vhTexture tex200 = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), 200, 16 );
        vhCreateTexture2D( tex200, "Reuse200", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ReuseOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Tex[0][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_DeferredReuse", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_DeferredReuse", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    uint32_t slot = alloc.Alloc();
    vhDescriptorTableSetTexture( t, slot, tex50 );

    vhStateId sid = 927;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 50, 2 );

    // Drain kDeferredDepth real frames so the slot the prior dispatch sampled is safe to recycle.
    alloc.Free( slot );
    for ( uint32_t i = 0; i < alloc.kDeferredDepth; i++ )
    {
        vhFinish();
        alloc.Step();
    }

    uint32_t reused = alloc.Alloc();
    EXPECT_EQ( reused, slot );

    vhDescriptorTableSetTexture( t, reused, tex200 );
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData2;
    vhReadTextureSlow( outTex, 0, 0, &readData2 );
    vhFinish();
    ASSERT_EQ( readData2.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData2[i], ( uint8_t ) 200, 2 );

    vhDestroyTexture( tex50 );
    vhDestroyTexture( tex200 );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

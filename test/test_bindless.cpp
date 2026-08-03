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
    // state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
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

// --------------------------------------------------------------------------
// P0 Semantics Pinning Tests
// --------------------------------------------------------------------------

UTEST_F( Bindless, Clear_ThenOverwrite_ShowsNewResource )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture texA = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 10, 16 );
        vhCreateTexture2D( texA, "TexA", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );
    }

    vhTexture texB = vhAllocTexture();
    {
        vhMem* dataB = vhAllocMem( 16 );
        memset( dataB->data(), 20, 16 );
        vhCreateTexture2D( texB, "TexB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataB );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ClearOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
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
    ASSERT_TRUE( vhCompileShader( "CS_ClearOverwrite", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_ClearOverwrite", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhDescriptorTableSetTexture( t, 0, texA );
    vhDescriptorTableClear( t, 0, nvrhi::ResourceType::Texture_SRV );
    vhDescriptorTableSetTexture( t, 0, texB );

    vhStateId sid = 930;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 20, 2 );

    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, Clear_Characterisation_OldDataPersists )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture texA = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 10, 16 );
        vhCreateTexture2D( texA, "TexA_Char", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "CharOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
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
    ASSERT_TRUE( vhCompileShader( "CS_ClearChar", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_ClearChar", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhDescriptorTableSetTexture( t, 0, texA );
    vhStateId sid = 931;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData1;
    vhReadTextureSlow( outTex, 0, 0, &readData1 );
    vhFinish();
    ASSERT_EQ( readData1.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData1[i], ( uint8_t ) 10, 2 );

    // Clear slot 0 (on Vulkan/nvrhi this is currently a no-op; texA remains bound in Vulkan set)
    vhDescriptorTableClear( t, 0, nvrhi::ResourceType::Texture_SRV );

    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData2;
    vhReadTextureSlow( outTex, 0, 0, &readData2 );
    vhFinish();
    ASSERT_EQ( readData2.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData2[i], ( uint8_t ) 10, 2 );

    vhDestroyTexture( texA );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, Resize_SoftContract )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );
    vhFinish();

    // Shrink capacity
    vhResizeDescriptorTable( t, 4 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 4u );

    // Grow back within maxCapacity
    vhResizeDescriptorTable( t, 8 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 8u );

    // Grow past maxCapacity 16 -> clamped to 16 by our fix
    vhResizeDescriptorTable( t, 32 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 16u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, LastWriteWins_SameSlotBeforeUse )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture texA = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 15, 16 );
        vhCreateTexture2D( texA, "TexA_LWW", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );
    }

    vhTexture texB = vhAllocTexture();
    {
        vhMem* dataB = vhAllocMem( 16 );
        memset( dataB->data(), 75, 16 );
        vhCreateTexture2D( texB, "TexB_LWW", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataB );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "LWWOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
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
    ASSERT_TRUE( vhCompileShader( "CS_LWW", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_LWW", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhDescriptorTableSetTexture( t, 0, texA );
    vhDescriptorTableSetTexture( t, 0, texB );

    vhStateId sid = 932;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 75, 2 );

    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, WriteOOB_NoCrash_NoPin )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable tSRV = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );
    vhDescriptorTable tUAV = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_UAV, 4 );

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "TexOOB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    {
        TestLogCapture log;

        vhDescriptorTableSetTexture( tSRV, 100, tex );
        vhFinish();
        EXPECT_TRUE( log.Contains( "Failed to write descriptor table" ) );
        EXPECT_GT( log.ErrorCount(), 0 );

        // Had the rejected write still pinned ShaderResource, this UAV pin would conflict.
        log.Clear();
        vhDescriptorTableSetTexture( tUAV, 0, tex, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, true );
        vhFinish();
        EXPECT_FALSE( log.Contains( "Attempted to switch permanent state" ) );
        EXPECT_EQ( log.ErrorCount(), 0 );
    }

    vhDestroyTexture( tex );
    vhDestroyDescriptorTable( tSRV );
    vhDestroyDescriptorTable( tUAV );
    vhFinish();
}

UTEST_F( Bindless, TypeMismatchWrite_Surfaced )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhBuffer buf = vhAllocBuffer();
    vhCreateStorageBuffer( buf, "BufMismatch", nullptr, 64, VRHI_BUFFER_COMPUTE_READ_WRITE, 16 );
    vhFinish();

    {
        TestLogCapture log;

        // Table only declares Texture_SRV; nvrhi would drop this silently.
        vhDescriptorTableSetBuffer( t, 0, buf, nvrhi::ResourceType::StructuredBuffer_SRV );
        vhFinish();
        EXPECT_TRUE( log.Contains( "does not declare resource type" ) );
        EXPECT_GT( log.ErrorCount(), 0 );
    }

    vhDestroyBuffer( buf );
    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, PipelinedOrdering_NoFinish )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    // Enqueue all allocations and creation without intermediate vhFinish
    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc );

    vhTexture tex = vhAllocTexture();
    vhMem* data = vhAllocMem( 16 );
    memset( data->data(), 123, 16 );
    vhCreateTexture2D( tex, "PipelineTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "PipelineOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhDescriptorTableSetTexture( t, 0, tex );

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
    ASSERT_TRUE( vhCompileShader( "CS_Pipeline", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Pipeline", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 933;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish(); // Single vhFinish at the very end
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 123, 2 );

    vhDestroyTexture( tex );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, WriteBeforeCreate_GracefulError )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    {
        TestLogCapture log;

        vhTexture uncreatedTex = 999999;
        vhDescriptorTableSetTexture( t, 0, uncreatedTex );
        vhFinish();
        EXPECT_TRUE( log.Contains( "Texture 999999 not found" ) );
        EXPECT_GT( log.ErrorCount(), 0 );
    }

    vhDestroyDescriptorTable( t );
    vhFinish();
}

// --------------------------------------------------------------------------
// P1 Pending Command Buffer Suite
// --------------------------------------------------------------------------

UTEST_F( Bindless, UAB_IntraFrame_WriteBetweenDispatches )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );
    uint32_t reqUAB = VRHI_UAB_SAMPLED_IMAGE | VRHI_UAB_UPDATE_UNUSED_WHILE_PENDING;
    if ( ( g_vhDeviceInfo.bindlessUpdateAfterBind & reqUAB ) != reqUAB ) UTEST_SKIP( "UAB sampled image unsupported" );

    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.updateAfterBind = true;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc );

    vhTexture texA = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 30, 16 );
        vhCreateTexture2D( texA, "TexA_UAB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );
    }

    vhTexture texB = vhAllocTexture();
    {
        vhMem* dataB = vhAllocMem( 16 );
        memset( dataB->data(), 80, 16 );
        vhCreateTexture2D( texB, "TexB_UAB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataB );
    }

    vhTexture out1 = vhAllocTexture();
    vhCreateTexture2D( out1, "Out1_UAB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhTexture out2 = vhAllocTexture();
    vhCreateTexture2D( out2, "Out2_UAB", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource1 = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Tex[0][id.xy];
        }
    )";

    const char* csSource2 = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Tex[1][id.xy];
        }
    )";

    std::vector< uint32_t > spirv1, spirv2;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_UABIntra1", csSource1, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv1, "main", {}, {}, &error ) );
    ASSERT_TRUE( vhCompileShader( "CS_UABIntra2", csSource2, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv2, "main", {}, {}, &error ) );

    vhShader cs1 = vhAllocShader();
    vhCreateShader( cs1, "CS_UABIntra1", VRHI_SHADER_STAGE_COMPUTE, spirv1, "main" );

    vhShader cs2 = vhAllocShader();
    vhCreateShader( cs2, "CS_UABIntra2", VRHI_SHADER_STAGE_COMPUTE, spirv2, "main" );

    // First write slot 0 & dispatch
    vhDescriptorTableSetTexture( t, 0, texA );

    vhState state1 = g_state0;
    state1.SetProgram( vhCreateComputeProgram( cs1 ) );
    vhState::TextureBinding tbOut1;
    tbOut1.name = "g_Out";
    tbOut1.texture = out1;
    tbOut1.computeUAV = true;
    tbOut1.formatOverride = nvrhi::Format::R8_UNORM;
    state1.SetTexture( 0, tbOut1 );
    state1.SetDescriptorTable( 0, t );

    vhStateId sid1 = 934;
    vhSetState( sid1, state1 );
    vhDispatch( sid1, { 1, 1, 1 } );

    // Write slot 1 without vhFinish
    vhDescriptorTableSetTexture( t, 1, texB );

    vhState state2 = g_state0;
    state2.SetProgram( vhCreateComputeProgram( cs2 ) );
    vhState::TextureBinding tbOut2;
    tbOut2.name = "g_Out";
    tbOut2.texture = out2;
    tbOut2.computeUAV = true;
    tbOut2.formatOverride = nvrhi::Format::R8_UNORM;
    state2.SetTexture( 0, tbOut2 );
    state2.SetDescriptorTable( 0, t );

    vhStateId sid2 = 935;
    vhSetState( sid2, state2 );
    vhDispatch( sid2, { 1, 1, 1 } );

    vhMem readData1, readData2;
    vhReadTextureSlow( out1, 0, 0, &readData1 );
    vhReadTextureSlow( out2, 0, 0, &readData2 );
    vhFinish();

    ASSERT_EQ( readData1.size(), 16u );
    ASSERT_EQ( readData2.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( ( uint8_t ) 30, readData1[i], 2 );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( ( uint8_t ) 80, readData2[i], 2 );

    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( out1 );
    vhDestroyTexture( out2 );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs1 );
    vhDestroyShader( cs2 );
    vhSetState( sid1, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, NonUAB_Fenced_WriteBetweenDispatches )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture texA = vhAllocTexture();
    {
        vhMem* dataA = vhAllocMem( 16 );
        memset( dataA->data(), 45, 16 );
        vhCreateTexture2D( texA, "TexA_Fenced", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataA );
    }

    vhTexture texB = vhAllocTexture();
    {
        vhMem* dataB = vhAllocMem( 16 );
        memset( dataB->data(), 95, 16 );
        vhCreateTexture2D( texB, "TexB_Fenced", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataB );
    }

    vhTexture out1 = vhAllocTexture();
    vhCreateTexture2D( out1, "Out1_Fenced", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhTexture out2 = vhAllocTexture();
    vhCreateTexture2D( out2, "Out2_Fenced", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
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
    ASSERT_TRUE( vhCompileShader( "CS_NonUABFenced", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_NonUABFenced", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhDescriptorTableSetTexture( t, 0, texA );

    vhState state1 = g_state0;
    state1.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut1;
    tbOut1.name = "g_Out";
    tbOut1.texture = out1;
    tbOut1.computeUAV = true;
    tbOut1.formatOverride = nvrhi::Format::R8_UNORM;
    state1.SetTexture( 0, tbOut1 );
    state1.SetDescriptorTable( 0, t );

    vhStateId sid1 = 936;
    vhSetState( sid1, state1 );
    vhDispatch( sid1, { 1, 1, 1 } );

    // Explicit vhFinish before non-UAB update
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, texB );

    vhState state2 = g_state0;
    state2.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut2;
    tbOut2.name = "g_Out";
    tbOut2.texture = out2;
    tbOut2.computeUAV = true;
    tbOut2.formatOverride = nvrhi::Format::R8_UNORM;
    state2.SetTexture( 0, tbOut2 );
    state2.SetDescriptorTable( 0, t );

    vhStateId sid2 = 937;
    vhSetState( sid2, state2 );
    vhDispatch( sid2, { 1, 1, 1 } );

    vhMem readData1, readData2;
    vhReadTextureSlow( out1, 0, 0, &readData1 );
    vhReadTextureSlow( out2, 0, 0, &readData2 );
    vhFinish();

    ASSERT_EQ( readData1.size(), 16u );
    ASSERT_EQ( readData2.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData1[i], ( uint8_t ) 45, 2 );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData2[i], ( uint8_t ) 95, 2 );

    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( out1 );
    vhDestroyTexture( out2 );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid1, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, UAB_CrossFrame_NoFence )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );
    uint32_t reqUAB = VRHI_UAB_SAMPLED_IMAGE | VRHI_UAB_UPDATE_UNUSED_WHILE_PENDING;
    if ( ( g_vhDeviceInfo.bindlessUpdateAfterBind & reqUAB ) != reqUAB ) UTEST_SKIP( "UAB sampled image unsupported" );

    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.updateAfterBind = true;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc );

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
    ASSERT_TRUE( vhCompileShader( "CS_UABCross", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_UABCross", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    for ( int frame = 0; frame < 4; frame++ )
    {
        uint8_t val = ( uint8_t )( ( frame + 1 ) * 40 );
        vhTexture tex = vhAllocTexture();
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), val, 16 );
        vhCreateTexture2D( tex, "CrossTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );

        vhTexture outTex = vhAllocTexture();
        vhCreateTexture2D( outTex, "CrossOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

        vhDescriptorTableSetTexture( t, 0, tex );

        vhState state = g_state0;
        state.SetProgram( vhCreateComputeProgram( cs ) );
        vhState::TextureBinding tbOut;
        tbOut.name = "g_Out";
        tbOut.texture = outTex;
        tbOut.computeUAV = true;
        tbOut.formatOverride = nvrhi::Format::R8_UNORM;
        state.SetTexture( 0, tbOut );
        state.SetDescriptorTable( 0, t );

        vhStateId sid = 938 + frame;
        vhSetState( sid, state );
        vhDispatch( sid, { 1, 1, 1 } );

        vhMem readData;
        vhReadTextureSlow( outTex, 0, 0, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), 16u );
        for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], val, 2 );

        vhDestroyTexture( tex );
        vhDestroyTexture( outTex );
        vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    }

    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhFinish();
}

// --------------------------------------------------------------------------
// P2 Coverage Breadth Suite
// --------------------------------------------------------------------------

UTEST_F( Bindless, CB_Bindless_EndToEnd )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::ConstantBuffer, 4 );

    struct CBData { glm::vec4 val; };
    CBData cbVal = { { 7.0f, 14.0f, 21.0f, 28.0f } };
    vhMem* cbMem = vhAllocMem( sizeof( CBData ) );
    memcpy( cbMem->data(), &cbVal, sizeof( CBData ) );

    vhBuffer cbBuf = vhAllocBuffer();
    vhCreateUniformBuffer( cbBuf, "BindlessCB", cbMem, sizeof( CBData ) );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "CBOut", { 4, 4 }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetBuffer( t, 0, cbBuf, nvrhi::ResourceType::ConstantBuffer, 0, sizeof( CBData ) );

    const char* csSource = R"(
        struct CBData
        {
            float4 val;
        };
        ConstantBuffer<CBData> g_CBuf[] : register(b0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_CBuf[0].val.x;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_CBBindless", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_CBBindless", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_FLOAT;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 940;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u * sizeof( float ) );
    float* fPtr = ( float* ) readData.data();
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( fPtr[i], 7.0f, 0.01f );

    vhDestroyBuffer( cbBuf );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, StructuredBuffer_Bindless_EndToEnd )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::StructuredBuffer_SRV, 4 );

    uint32_t bufDataRaw[4] = { 100, 200, 300, 400 };
    vhMem* bufData = vhAllocMem( sizeof( bufDataRaw ) );
    memcpy( bufData->data(), bufDataRaw, sizeof( bufDataRaw ) );
    vhBuffer sbuf = vhAllocBuffer();
    vhCreateStorageBuffer( sbuf, "StructBuf", bufData, sizeof( bufDataRaw ), VRHI_BUFFER_COMPUTE_READ, sizeof( uint32_t ) );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "StructOut", { 4, 4 }, 1, nvrhi::Format::R32_UINT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetBuffer( t, 0, sbuf, nvrhi::ResourceType::StructuredBuffer_SRV );

    const char* csSource = R"(
        StructuredBuffer<uint> g_Buf[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<uint> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Buf[0][id.x % 4];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_StructBindless", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_StructBindless", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_UINT;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 941;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u * sizeof( uint32_t ) );
    uint32_t* uPtr = ( uint32_t* ) readData.data();
    for ( uint32_t y = 0; y < 4; y++ )
        for ( uint32_t x = 0; x < 4; x++ )
            EXPECT_EQ( uPtr[y * 4 + x], bufDataRaw[x] );

    vhDestroyBuffer( sbuf );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, RawBuffer_SRV_Bindless )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::RawBuffer_SRV, 4 );

    uint32_t rawDataRaw[4] = { 111, 222, 333, 444 };
    vhMem* rawData = vhAllocMem( sizeof( rawDataRaw ) );
    memcpy( rawData->data(), rawDataRaw, sizeof( rawDataRaw ) );
    vhBuffer rbuf = vhAllocBuffer();
    vhCreateStorageBuffer( rbuf, "RawBuf", rawData, sizeof( rawDataRaw ), VRHI_BUFFER_COMPUTE_READ, 0, nvrhi::Format::UNKNOWN );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "RawOut", { 4, 4 }, 1, nvrhi::Format::R32_UINT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetBuffer( t, 0, rbuf, nvrhi::ResourceType::RawBuffer_SRV );

    const char* csSource = R"(
        ByteAddressBuffer g_Buf[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<uint> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Buf[0].Load((id.x % 4) * 4);
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_RawBindless", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_RawBindless", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_UINT;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 942;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u * sizeof( uint32_t ) );
    uint32_t* uPtr = ( uint32_t* ) readData.data();
    for ( uint32_t y = 0; y < 4; y++ )
        for ( uint32_t x = 0; x < 4; x++ )
            EXPECT_EQ( uPtr[y * 4 + x], rawDataRaw[x] );

    vhDestroyBuffer( rbuf );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, TypedBuffer_Format_Bindless )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::TypedBuffer_SRV, 4 );

    uint32_t typedDataRaw[4] = { 55, 66, 77, 88 };
    vhMem* typedData = vhAllocMem( sizeof( typedDataRaw ) );
    memcpy( typedData->data(), typedDataRaw, sizeof( typedDataRaw ) );
    vhBuffer tbuf = vhAllocBuffer();
    vhCreateStorageBuffer( tbuf, "TypedBuf", typedData, sizeof( typedDataRaw ), VRHI_BUFFER_COMPUTE_READ, 0, nvrhi::Format::R32_UINT );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "TypedOut", { 4, 4 }, 1, nvrhi::Format::R32_UINT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetBuffer( t, 0, tbuf, nvrhi::ResourceType::TypedBuffer_SRV, 0, 0, nvrhi::Format::R32_UINT );

    const char* csSource = R"(
        Buffer<uint> g_Buf[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<uint> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Buf[0][id.x % 4];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_TypedBindless", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_TypedBindless", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_UINT;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 943;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u * sizeof( uint32_t ) );
    uint32_t* uPtr = ( uint32_t* ) readData.data();
    for ( uint32_t y = 0; y < 4; y++ )
        for ( uint32_t x = 0; x < 4; x++ )
            EXPECT_EQ( uPtr[y * 4 + x], typedDataRaw[x] );

    vhDestroyBuffer( tbuf );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, TextureUAV_Bindless_WritePath )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_UAV, 4 );

    vhTexture targetTex = vhAllocTexture();
    vhCreateTexture2D( targetTex, "UAVTarget", { 4, 4 }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, targetTex, nvrhi::Format::R32_FLOAT, nvrhi::AllSubresources, true );
    vhFinish();

    const char* csSource = R"(
        [format("r32f")]
        RWTexture2D<float> g_BindlessOut[] : register(u0, VRHI_BINDLESS_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_BindlessOut[0][id.xy] = 0.5f;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_TextureUAVBindless", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_TextureUAVBindless", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 944;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    // KNOWN BUG: the bindless UAV write pins this texture to UnorderedAccess permanently, so the
    // readback cannot transition it to CopySource and copies from GENERAL. Right bytes by luck on
    // MoltenVK. Asserted so a fix surfaces as a failure instead of passing quietly.
    vhMem readData;
    {
        TestLogCapture log;
        vhReadTextureSlow( targetTex, 0, 0, &readData );
        vhFinish();
        EXPECT_TRUE( log.Contains( "doesn't have the right state bits" ) );
    }
    ASSERT_EQ( readData.size(), 16u * sizeof( float ) );
    float* fPtr = ( float* ) readData.data();
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( fPtr[i], 0.5f, 0.01f );

    vhDestroyTexture( targetTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, MixedSpaces_OneTable_OrdinalBinding )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhAllocDescriptorTable();
    nvrhi::BindlessLayoutDesc desc;
    desc.visibility = nvrhi::ShaderType::All;
    desc.maxCapacity = 4;
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    desc.registerSpaces.push_back( nvrhi::BindingLayoutItem::StructuredBuffer_SRV( 1 ) );
    vhCreateDescriptorTable( t, desc );

    vhTexture tex = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), 60, 16 );
        vhCreateTexture2D( tex, "MixedTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }

    uint32_t bufVal = 500;
    vhMem* bufData = vhAllocMem( sizeof( uint32_t ) );
    memcpy( bufData->data(), &bufVal, sizeof( uint32_t ) );
    vhBuffer buf = vhAllocBuffer();
    vhCreateStorageBuffer( buf, "MixedBuf", bufData, sizeof( uint32_t ), VRHI_BUFFER_COMPUTE_READ, sizeof( uint32_t ) );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "MixedOut", { 4, 4 }, 1, nvrhi::Format::R32_UINT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, tex );
    vhDescriptorTableSetBuffer( t, 0, buf, nvrhi::ResourceType::StructuredBuffer_SRV );

    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        StructuredBuffer<uint> g_Buf[] : register(t1, VRHI_BINDLESS_SPACE);
        RWTexture2D<uint> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint texVal = (uint)(g_Tex[0][id.xy] * 255.0f);
            g_Out[id.xy] = texVal + g_Buf[0][0];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_MixedOrdinal", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_MixedOrdinal", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_UINT;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 945;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u * sizeof( uint32_t ) );
    uint32_t* uPtr = ( uint32_t* ) readData.data();
    for ( size_t i = 0; i < 16; i++ ) EXPECT_EQ( uPtr[i], 60u + 500u );

    vhDestroyTexture( tex );
    vhDestroyBuffer( buf );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, Graphics_PS_BindlessTexture_Draw )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture tex = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), 200, 16 );
        vhCreateTexture2D( tex, "GfxTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }

    vhTexture rt = vhAllocTexture();
    vhCreateTexture2D( rt, "GfxRT", { 16, 16 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_RT );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, tex );

    const char* vsSource = R"(
        struct VSOut
        {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD0;
        };

        VSOut main(uint id : SV_VertexID)
        {
            VSOut output;
            float2 grid[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
            output.pos = float4(grid[id], 0.0, 1.0);
            output.uv = (grid[id] + 1.0) * 0.5;
            return output;
        }
    )";

    const char* psSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);

        float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
        {
            float val = g_Tex[0].Load(int3(0, 0, 0));
            return float4(val, val, val, 1.0);
        }
    )";

    std::vector< uint32_t > vsSpirv, psSpirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "VS_GfxBindless", vsSource, VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_0, vsSpirv, "main", {}, {}, &error ) );
    ASSERT_TRUE( vhCompileShader( "PS_GfxBindless", psSource, VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0, psSpirv, "main", {}, {}, &error ) );

    vhShader vs = vhAllocShader();
    vhCreateShader( vs, "VS_GfxBindless", VRHI_SHADER_STAGE_VERTEX, vsSpirv, "main" );

    vhShader ps = vhAllocShader();
    vhCreateShader( ps, "PS_GfxBindless", VRHI_SHADER_STAGE_PIXEL, psSpirv, "main" );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 16, 16 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK )
         .SetProgram( vhCreateGfxProgram( vs, ps ) )
         .SetDescriptorTable( 0, t );

    vhStateId sid = 946;
    vhSetState( sid, state );
    vhClear( sid, VRHI_CLEAR_COLOR );
    vhDraw( sid, 3 );

    vhMem readData;
    vhReadTextureSlow( rt, 0, 0, &readData );
    vhFinish();
    ASSERT_GT( readData.size(), 0u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 200, 4 );

    vhDestroyTexture( tex );
    vhDestroyTexture( rt );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, Graphics_VS_BindlessFetch )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::StructuredBuffer_SRV, 4 );

    glm::vec4 vertColors[3] = {
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f }
    };
    vhMem* sbufData = vhAllocMem( sizeof( vertColors ) );
    memcpy( sbufData->data(), vertColors, sizeof( vertColors ) );
    vhBuffer sbuf = vhAllocBuffer();
    vhCreateStorageBuffer( sbuf, "VSBuf", sbufData, sizeof( vertColors ), VRHI_BUFFER_COMPUTE_READ, sizeof( glm::vec4 ) );

    vhTexture rt = vhAllocTexture();
    vhCreateTexture2D( rt, "VSRT", { 16, 16 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhFinish();

    vhDescriptorTableSetBuffer( t, 0, sbuf, nvrhi::ResourceType::StructuredBuffer_SRV );

    const char* vsSource = R"(
        StructuredBuffer<float4> g_Colors[] : register(t0, VRHI_BINDLESS_SPACE);

        struct VSOut
        {
            float4 pos : SV_Position;
            float4 color : COLOR0;
        };

        VSOut main(uint id : SV_VertexID)
        {
            VSOut output;
            float2 grid[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
            output.pos = float4(grid[id], 0.0, 1.0);
            output.color = g_Colors[0][id];
            return output;
        }
    )";

    const char* psSource = R"(
        float4 main(float4 pos : SV_Position, float4 color : COLOR0) : SV_Target
        {
            return color;
        }
    )";

    std::vector< uint32_t > vsSpirv, psSpirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "VS_BindlessFetch", vsSource, VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_0, vsSpirv, "main", {}, {}, &error ) );
    ASSERT_TRUE( vhCompileShader( "PS_BindlessFetch", psSource, VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0, psSpirv, "main", {}, {}, &error ) );

    vhShader vs = vhAllocShader();
    vhCreateShader( vs, "VS_BindlessFetch", VRHI_SHADER_STAGE_VERTEX, vsSpirv, "main" );

    vhShader ps = vhAllocShader();
    vhCreateShader( ps, "PS_BindlessFetch", VRHI_SHADER_STAGE_PIXEL, psSpirv, "main" );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 16, 16 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK )
         .SetProgram( vhCreateGfxProgram( vs, ps ) )
         .SetDescriptorTable( 0, t );

    vhStateId sid = 947;
    vhSetState( sid, state );
    vhClear( sid, VRHI_CLEAR_COLOR );
    vhDraw( sid, 3 );

    vhMem readData;
    vhReadTextureSlow( rt, 0, 0, &readData );
    vhFinish();
    ASSERT_GE( readData.size(), 64u );
    // Check center pixel is red (RGBA8 = 255, 0, 0, 255)
    uint8_t* p = readData.data() + ( 8 * 16 + 8 ) * 4;
    EXPECT_NEAR( p[0], 255, 2 );
    EXPECT_NEAR( p[1], 0, 2 );

    vhDestroyBuffer( sbuf );
    vhDestroyTexture( rt );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, DepthTexture_InTable_Sampled )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture depthTex = vhAllocTexture();
    vhCreateTexture2D( depthTex, "DepthTex", { 4, 4 }, 1, nvrhi::Format::D32, VRHI_TEXTURE_RT );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "DepthOut", { 4, 4 }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, depthTex );

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
    ASSERT_TRUE( vhCompileShader( "CS_DepthBindless", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_DepthBindless", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_FLOAT;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 948;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u * sizeof( float ) );

    vhDestroyTexture( depthTex );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, SamePinTwice_Idempotent )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture tex = vhAllocTexture();
    {
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), 77, 16 );
        vhCreateTexture2D( tex, "SamePinTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "SamePinOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    // Write same texture to two different slots in the same table
    vhDescriptorTableSetTexture( t, 0, tex );
    vhDescriptorTableSetTexture( t, 1, tex );

    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Tex[0][id.xy] + g_Tex[1][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_SamePin", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_SamePin", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 949;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t )( 77 + 77 ), 4 );

    vhDestroyTexture( tex );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, ConflictingPin_SRVthenUAV )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable tSRV = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );
    vhDescriptorTable tUAV = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_UAV, 4 );

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "ConflictTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    {
        TestLogCapture log;

        // Bindless writes pin permanent state, so one texture cannot be both SRV and UAV bindless.
        vhDescriptorTableSetTexture( tSRV, 0, tex );
        vhDescriptorTableSetTexture( tUAV, 0, tex, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, true );
        vhFinish();
        EXPECT_TRUE( log.Contains( "Attempted to switch permanent state" ) );
    }

    vhDestroyTexture( tex );
    vhDestroyDescriptorTable( tSRV );
    vhDestroyDescriptorTable( tUAV );
    vhFinish();
}

// --------------------------------------------------------------------------
// P3 MoltenVK Canaries
// --------------------------------------------------------------------------

UTEST_F( Bindless, DestroyResource_WhileInTable_OtherSlotsSurvive )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture tex0 = vhAllocTexture();
    {
        vhMem* data0 = vhAllocMem( 16 );
        memset( data0->data(), 111, 16 );
        vhCreateTexture2D( tex0, "TexSurvive0", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data0 );
    }

    vhTexture tex1 = vhAllocTexture();
    {
        vhMem* data1 = vhAllocMem( 16 );
        memset( data1->data(), 222, 16 );
        vhCreateTexture2D( tex1, "TexSurvive1", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data1 );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "SurviveOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, tex0 );
    vhDescriptorTableSetTexture( t, 1, tex1 );

    // Destroy tex0 while it's still written in slot 0
    vhDestroyTexture( tex0 );
    vhFinish();

    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            // Sample only slot 1 (tex1) which is still alive
            g_Out[id.xy] = g_Tex[1][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_Survive", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Survive", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 950;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 222, 2 );

    vhDestroyTexture( tex1 );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, TrueNonUniform_DivergentIndexFromBuffer )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhTexture tex0 = vhAllocTexture();
    {
        vhMem* data0 = vhAllocMem( 16 );
        memset( data0->data(), 10, 16 );
        vhCreateTexture2D( tex0, "DivergeTex0", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data0 );
    }

    vhTexture tex1 = vhAllocTexture();
    {
        vhMem* data1 = vhAllocMem( 16 );
        memset( data1->data(), 90, 16 );
        vhCreateTexture2D( tex1, "DivergeTex1", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data1 );
    }

    uint32_t indices[16];
    for ( int i = 0; i < 16; i++ ) indices[i] = ( i % 2 );
    vhMem* idxData = vhAllocMem( sizeof( indices ) );
    memcpy( idxData->data(), indices, sizeof( indices ) );

    vhBuffer idxBuf = vhAllocBuffer();
    vhCreateStorageBuffer( idxBuf, "IdxBuf", idxData, sizeof( indices ), VRHI_BUFFER_COMPUTE_READ, sizeof( uint32_t ) );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "DivergeOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    vhDescriptorTableSetTexture( t, 0, tex0 );
    vhDescriptorTableSetTexture( t, 1, tex1 );

    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        StructuredBuffer<uint> g_IdxBuf : register(t0, VRHI_STAGE_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint linearIdx = id.y * 4 + id.x;
            uint slot = NonUniformResourceIndex(g_IdxBuf[linearIdx]);
            g_Out[id.xy] = g_Tex[slot][id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_Divergent", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Divergent", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::BufferBinding bbIdx;
    bbIdx.name = "g_IdxBuf";
    bbIdx.buffer = idxBuf;
    state.SetBuffer( 0, bbIdx );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 951;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ )
    {
        uint8_t expected = ( i % 2 == 0 ) ? 10 : 90;
        EXPECT_NEAR( readData[i], expected, 2 );
    }

    vhDestroyTexture( tex0 );
    vhDestroyTexture( tex1 );
    vhDestroyBuffer( idxBuf );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, MaxCapacityTable_SparseSlots )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    uint32_t maxCap = std::min< uint32_t >( g_vhDeviceInfo.maxBindlessSampledImages, 256 );
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, maxCap );

    vhTexture tex0 = vhAllocTexture();
    {
        vhMem* data0 = vhAllocMem( 16 );
        memset( data0->data(), 11, 16 );
        vhCreateTexture2D( tex0, "Sparse0", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data0 );
    }

    vhTexture texMid = vhAllocTexture();
    {
        vhMem* dataMid = vhAllocMem( 16 );
        memset( dataMid->data(), 55, 16 );
        vhCreateTexture2D( texMid, "SparseMid", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataMid );
    }

    vhTexture texEnd = vhAllocTexture();
    {
        vhMem* dataEnd = vhAllocMem( 16 );
        memset( dataEnd->data(), 99, 16 );
        vhCreateTexture2D( texEnd, "SparseEnd", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, dataEnd );
    }

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "SparseOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    uint32_t slotMid = maxCap / 2;
    uint32_t slotEnd = maxCap - 1;

    vhDescriptorTableSetTexture( t, 0, tex0 );
    vhDescriptorTableSetTexture( t, slotMid, texMid );
    vhDescriptorTableSetTexture( t, slotEnd, texEnd );

    std::string csSource = std::string( R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            float v0 = g_Tex[0][id.xy];
            float vMid = g_Tex[)" ) + std::to_string( slotMid ) + R"(][id.xy];
            float vEnd = g_Tex[)" + std::to_string( slotEnd ) + R"(][id.xy];
            g_Out[id.xy] = (v0 + vMid + vEnd) / 3.0f;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_Sparse", csSource.c_str(), VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Sparse", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 952;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    uint8_t expectedAvg = ( uint8_t )( ( 11 + 55 + 99 ) / 3 );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], expectedAvg, 3 );

    vhDestroyTexture( tex0 );
    vhDestroyTexture( texMid );
    vhDestroyTexture( texEnd );
    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, Dispatch_MissingTableBinding )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "MissingTableOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    // The read hides behind a condition the optimiser cannot fold, so the array survives into the
    // SPIR-V. id.x never reaches 1000, so the descriptor is never accessed and ePartiallyBound holds.
    const char* csSource = R"(
        Texture2D<float> g_Tex[] : register(t0, VRHI_BINDLESS_SPACE);
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            float v = 1.0f;
            if ( id.x > 1000 ) v = g_Tex[NonUniformResourceIndex(id.x)][id.xy];
            g_Out[id.xy] = v;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_MissingTable", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_MissingTable", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
    vhFinish();

    // Guard the premise: a stripped array would leave nothing to test.
    std::vector< vhShaderReflectionResource > resources;
    vhGetShaderInfo( cs, nullptr, &resources );
    bool sawBindless = false;
    for ( const auto& r : resources )
    {
        if ( !r.bindless ) continue;
        sawBindless = true;
        break;
    }
    ASSERT_TRUE( sawBindless );

    // No descriptor table set in state
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH );
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );

    vhStateId sid = 953;
    vhMem readData;
    {
        TestLogCapture log;
        vhSetState( sid, state );
        vhDispatch( sid, { 1, 1, 1 } );
        vhReadTextureSlow( outTex, 0, 0, &readData );
        vhFinish();

        EXPECT_TRUE( log.Contains( "no descriptor table is bound" ) );
    }

    // Dispatch still completes; the unbound descriptor is never accessed.
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 255, 2 );

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// P4 Stress & Lifecycle Churn
// --------------------------------------------------------------------------

UTEST_F( Bindless, Churn_AllocWriteDispatch_64Frames )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 16 );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ChurnOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
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
    ASSERT_TRUE( vhCompileShader( "CS_Churn", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Churn", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );
    state.SetDescriptorTable( 0, t );

    vhStateId sid = 954;

    for ( int frame = 0; frame < 64; frame++ )
    {
        uint8_t val = ( uint8_t )( ( frame * 3 + 17 ) % 255 );

        vhTexture tex = vhAllocTexture();
        vhMem* data = vhAllocMem( 16 );
        memset( data->data(), val, 16 );
        vhCreateTexture2D( tex, "ChurnFrameTex", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, data );

        vhDescriptorTableSetTexture( t, 0, tex );
        vhSetState( sid, state );
        vhDispatch( sid, { 1, 1, 1 } );

        vhMem readData;
        vhReadTextureSlow( outTex, 0, 0, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), 16u );
        for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], val, 2 );

        vhDestroyTexture( tex );
        vhFinish();
    }

    vhDestroyTexture( outTex );
    vhDestroyDescriptorTable( t );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, RecreateTable_SameHandle_NewCapacity )
{
    vhDescriptorTable t = vhAllocDescriptorTable();

    nvrhi::BindlessLayoutDesc desc1;
    desc1.visibility = nvrhi::ShaderType::All;
    desc1.maxCapacity = 8;
    desc1.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc1 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 8u );

    // Recreate table on same handle with capacity 16
    nvrhi::BindlessLayoutDesc desc2;
    desc2.visibility = nvrhi::ShaderType::All;
    desc2.maxCapacity = 16;
    desc2.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    vhCreateDescriptorTable( t, desc2 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 16u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

UTEST_F( Bindless, DestroyTable_ResetState_NoCrash )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 4 );

    vhState state = g_state0;
    state.SetDescriptorTable( 0, t );
    vhStateId sid = 955;
    vhSetState( sid, state );

    vhDestroyDescriptorTable( t );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();

    EXPECT_EQ( vhDescriptorTableCapacity( t ), 0u );

    // Canary dispatch: destroying a bound table must leave the device usable.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "DestroyTableCanary", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = 1.0f;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    ASSERT_TRUE( vhCompileShader( "CS_DestroyTableCanary", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_DestroyTableCanary", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState canary = g_state0;
    canary.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    canary.SetTexture( 0, tbOut );
    vhSetState( sid, canary );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    ASSERT_EQ( readData.size(), 16u );
    for ( size_t i = 0; i < 16; i++ ) EXPECT_NEAR( readData[i], ( uint8_t ) 255, 2 );

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Bindless, CapacityQuery_FreshnessContract )
{
    vhDescriptorTable t = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 12 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 12u );

    vhResizeDescriptorTable( t, 6 );
    vhFinish();
    EXPECT_EQ( vhDescriptorTableCapacity( t ), 6u );

    vhDestroyDescriptorTable( t );
    vhFinish();
}

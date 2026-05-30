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

struct Native {};
UTEST_F_SETUP( Native )
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
UTEST_F_TEARDOWN( Native )
{
    vhEndMarker();
}

UTEST_F( Native, BracketAndRebaseline )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhFlush();

    vhBuffer outBuf = vhAllocBuffer();
    vhCreateStorageBuffer( outBuf, "NativeBuf", nullptr, 64, VRHI_BUFFER_COMPUTE_READ_WRITE, sizeof( uint32_t ) );

    vhTexture outTex = vhAllocTexture();
    vhMem* initData = vhAllocMem( 64 );
    memset( initData->data(), 0x42, 64 );
    vhCreateTexture2D( outTex, "NativeTex", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE, initData );
    vhFinish();

    nvrhi::BufferHandle nvBuf = vhGetBufferNvrhiHandle( outBuf );
    ASSERT_TRUE( nvBuf != nullptr );
    VkBuffer vkBuf = (VkBuffer) nvBuf->getNativeObject( nvrhi::ObjectTypes::VK_Buffer ).pointer;
    ASSERT_TRUE( vkBuf != VK_NULL_HANDLE );
    ASSERT_TRUE( vhGetTextureNvrhiHandle( outTex ) != nullptr );

    std::vector< vhNativeResource > resources = {{
        .texture     = outTex,
        .stateBefore = nvrhi::ResourceStates::UnorderedAccess,
        .stateAfter  = nvrhi::ResourceStates::ShaderResource,
    }};

    vhExecuteNative(
        []( const vhNativeContext& ctx, vhNativeResource* /*resources*/, uint32_t /*count*/, void* user )
        {
            vkCmdFillBuffer( ctx.cmdbuf, *(VkBuffer*) user, 0, VK_WHOLE_SIZE, 0xDEADBEEFu );
        },
        &vkBuf, 0, resources
    );

    vhMem bufRead;
    vhReadBufferSlow( outBuf, 0, 64, &bufRead );
    vhFinish();
    ASSERT_EQ( bufRead.size(), 64u );
    for ( uint32_t i = 0; i < 16; i++ )
        EXPECT_EQ( ( (uint32_t*) bufRead.data() )[i], 0xDEADBEEFu );

    // Read outTex through a normal vrhi dispatch — proves stateAfter re-baseline worked.
    const char* csSource = R"(
        Texture2D<float> g_In  : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = g_In[id.xy]; }
    )";
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( "CS_NativeRebaseline", csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !ok ) printf( "Shader error: %s\n", error.c_str() );
    ASSERT_TRUE( ok );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_NativeRebaseline", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhTexture verifyTex = vhAllocTexture();
    vhCreateTexture2D( verifyTex, "NativeVerify", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_In",  .texture = outTex } );
    state.SetTexture( 1, { .name = "g_Out", .texture = verifyTex, .computeUAV = true, .formatOverride = nvrhi::Format::R8_UNORM } );

    vhStateId sid = 80000;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem texRead;
    vhReadTextureSlow( verifyTex, 0, 0, &texRead );
    vhFinish();

    ASSERT_EQ( texRead.size(), 64u );
    for ( int i = 0; i < 64; i++ )
        EXPECT_NEAR( texRead[i], 0x42, 1 );

    vhDestroyBuffer( outBuf );
    vhDestroyTexture( outTex );
    vhDestroyTexture( verifyTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

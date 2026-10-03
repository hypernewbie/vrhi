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
    state.SetTexture( 1, { .name = "g_Out", .texture = verifyTex, .formatOverride = nvrhi::Format::R8_UNORM, .computeUAV = true } );

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

UTEST_F( Native, DeviceLock )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );

    vhNativeDeviceLock lock;
    ASSERT_TRUE( vhGetVkDevice() != VK_NULL_HANDLE );
    ASSERT_TRUE( vhGetVkPhysicalDevice() != VK_NULL_HANDLE );
    ASSERT_TRUE( vhGetVkInstance() != VK_NULL_HANDLE );
}

// TESTPLAN NV01 — Native callback actually writes its declared texture.
// The anchor fixture declares a texture but only fills an undeclared buffer,
// so the declared texture never undergoes a native write. Here the texture is
// declared CopyDest -> ShaderResource, the callback performs a real
// vkCmdCopyBufferToImage of a per-texel pattern into it, and a normal VRHI
// compute pass then consumes it as an SRV. Targets an incorrect image state
// before the callback or a missing tracker rebaseline afterwards.
UTEST_F( Native, NV01_NativeCallbackWritesTexture )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Requires GPU" );
    vhFlush();
    const int32_t startErrors = g_vhErrorCounter.load();

    constexpr int DIM = 8;
    vhTexture outTex = vhAllocTexture();
    vhMem* initData = vhAllocMem( DIM * DIM );
    memset( initData->data(), 0x42, DIM * DIM );
    vhCreateTexture2D( outTex, "NV01_tex", { DIM, DIM }, 1, nvrhi::Format::R8_UNORM,
                       VRHI_TEXTURE_COMPUTE_WRITE, initData );
    vhFinish();

    // Staging buffer carrying the native pattern: unique per texel.
    std::vector< uint8_t > pattern( DIM * DIM );
    for ( int y = 0; y < DIM; ++y )
        for ( int x = 0; x < DIM; ++x )
            pattern[(size_t)y * DIM + x] = (uint8_t)( x * 31 + y * 17 + 5 );
    vhBuffer stageBuf = vhAllocBuffer();
    vhMem* stageMem = vhAllocMem( pattern.size() );
    memcpy( stageMem->data(), pattern.data(), pattern.size() );
    vhCreateStorageBuffer( stageBuf, "NV01_stage", stageMem, pattern.size(), VRHI_BUFFER_COMPUTE_READ );
    vhFinish();

    nvrhi::BufferHandle nvStage = vhGetBufferNvrhiHandle( stageBuf );
    ASSERT_TRUE( nvStage != nullptr );
    VkBuffer vkStage = (VkBuffer)nvStage->getNativeObject( nvrhi::ObjectTypes::VK_Buffer ).pointer;
    ASSERT_TRUE( vkStage != VK_NULL_HANDLE );
    nvrhi::TextureHandle nvTex = vhGetTextureNvrhiHandle( outTex );
    ASSERT_TRUE( nvTex != nullptr );
    VkImage vkImage = (VkImage)nvTex->getNativeObject( nvrhi::ObjectTypes::VK_Image ).pointer;
    ASSERT_TRUE( vkImage != VK_NULL_HANDLE );

    struct NativeArgs { VkBuffer buffer; VkImage image; int dim; };
    NativeArgs args = { vkStage, vkImage, DIM };

    std::vector< vhNativeResource > resources = {{
        .texture     = outTex,
        .stateBefore = nvrhi::ResourceStates::CopyDest,
        .stateAfter  = nvrhi::ResourceStates::ShaderResource,
    }};

    vhExecuteNative(
        []( const vhNativeContext& ctx, vhNativeResource* resources, uint32_t count, void* user )
        {
            NativeArgs* a = (NativeArgs*)user;
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { (uint32_t)a->dim, (uint32_t)a->dim, 1 };
            vkCmdCopyBufferToImage( ctx.cmdbuf, a->buffer, a->image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

            // stateAfter is the callback's responsibility: the engine only
            // rebaselines its tracker, so transition the image to the declared
            // final layout (ShaderResource -> SHADER_READ_ONLY_OPTIMAL).
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = a->image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier( ctx.cmdbuf,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier );
            (void)resources; (void)count;
        },
        &args, 0, resources
    );
    vhFinish();

    // Consume the native-written texture through an ordinary VRHI shader.
    const char* csSource = R"(
        Texture2D<float> g_In  : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = g_In.Load( int3( id.xy, 0 ) ); }
    )";
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( "CS_NV01", csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !ok ) printf( "Shader error: %s\n", error.c_str() );
    ASSERT_TRUE( ok );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_NV01", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhTexture verifyTex = vhAllocTexture();
    vhCreateTexture2D( verifyTex, "NV01_verify", { DIM, DIM }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_In",  .texture = outTex } );
    state.SetTexture( 1, { .name = "g_Out", .texture = verifyTex, .formatOverride = nvrhi::Format::R8_UNORM, .computeUAV = true } );

    vhStateId sid = 80010;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhMem texRead;
    vhReadTextureSlow( verifyTex, 0, 0, &texRead );
    vhFinish();

    ASSERT_EQ( texRead.size(), (size_t)DIM * DIM );
    int mismatches = 0;
    for ( int y = 0; y < DIM && mismatches < 4; ++y )
        for ( int x = 0; x < DIM && mismatches < 4; ++x )
        {
            uint8_t got = texRead[(size_t)y * DIM + x];
            uint8_t expect = pattern[(size_t)y * DIM + x];
            // Shader round-trips the byte through unorm floats.
            int rounded = (int)( ( expect / 255.0f ) * 255.0f + 0.5f );
            if ( abs( (int)got - rounded ) > 1 )
            {
                UTEST_PRINTF( "NV01: (%d,%d) = %d, expected %d\n", x, y, got, expect );
                ++mismatches;
            }
        }
    EXPECT_EQ( mismatches, 0 );

    vhDestroyBuffer( stageBuf );
    vhDestroyTexture( outTex );
    vhDestroyTexture( verifyTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

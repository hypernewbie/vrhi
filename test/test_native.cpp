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
// The callback writes the declared texture with a real vkCmdCopyBufferToImage
// and records its own final layout transition, since stateAfter is only a
// tracker rebaseline. The consuming compute pass and its initialized output are
// created and recorded before the native interval, and the consumer command is
// recorded immediately after vhExecuteNative with no drain, flush, or readback
// in between, so tracker faults surface in the same submission. The callback's
// own view of its resources (count, image identity, dimensions, format) is
// captured into a test-owned record and read only after backend completion.
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
    nvrhi::TextureHandle nvTex = vhGetTextureNvrhiHandle( outTex );
    VkBuffer vkStage = nvStage ? (VkBuffer)nvStage->getNativeObject( nvrhi::ObjectTypes::VK_Buffer ).pointer : VK_NULL_HANDLE;
    VkImage vkImage = nvTex ? (VkImage)nvTex->getNativeObject( nvrhi::ObjectTypes::VK_Image ).pointer : VK_NULL_HANDLE;
    if ( !vkStage || !vkImage )
    {
        vhDestroyTexture( outTex ); vhDestroyBuffer( stageBuf );
        vhFinish();
        EXPECT_TRUE( vkStage != VK_NULL_HANDLE );
        EXPECT_TRUE( vkImage != VK_NULL_HANDLE );
        return;
    }

    // Compile the consumer before the native interval and create its output
    // with a sentinel, so the only thing that can change it is the native
    // write followed by the consumer.
    const char* csSource = R"(
        Texture2D<float> g_In  : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = g_In.Load( int3( id.xy, 0 ) ); }
    )";
    std::vector< uint32_t > spirv;
    std::string error;
    bool compiled = vhCompileShader( "CS_NV01", csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !compiled || spirv.empty() )
    {
        UTEST_PRINTF( "CS_NV01 compile error: %s\n", error.c_str() );
        vhDestroyTexture( outTex ); vhDestroyBuffer( stageBuf );
        vhFinish();
        EXPECT_TRUE( compiled && !spirv.empty() );
        return;
    }
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_NV01", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhTexture verifyTex = vhAllocTexture();
    vhCreateTexture2D( verifyTex, "NV01_verify", { DIM, DIM }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Sentinel the consumer output with 0xFF, outside the pattern's range for
    // every texel, so a missing native write cannot pass.
    {
        vhMem* fillMem = vhAllocMem( DIM * DIM );
        memset( fillMem->data(), 0xFF, DIM * DIM );
        vhUpdateTexture( verifyTex, 0, 0, 1, 1, fillMem );
    }
    vhFinish(); // setup drain, before the interval under test

    vhStateId sid = 80010;
    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_In",  .texture = outTex } );
    state.SetTexture( 1, { .name = "g_Out", .texture = verifyTex, .formatOverride = nvrhi::Format::R8_UNORM, .computeUAV = true } );
    vhSetState( sid, state );

    // What the callback observed about its declared resources. Written only by
    // the backend thread, read only after a drain, and never asserted inside
    // the callback itself.
    struct NativeObservation
    {
        uint32_t count = 0;
        uintptr_t image = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    } observed;
    memset( &observed, 0, sizeof( observed ) );

    struct NativeArgs
    {
        VkBuffer buffer;
        VkImage image;
        int dim;
        NativeObservation* observed;
    } args = { vkStage, vkImage, DIM, &observed };

    std::vector< vhNativeResource > resources = {{
        .texture     = outTex,
        .stateBefore = nvrhi::ResourceStates::CopyDest,
        .stateAfter  = nvrhi::ResourceStates::ShaderResource,
    }};

    vhExecuteNative(
        []( const vhNativeContext& ctx, vhNativeResource* res, uint32_t count, void* user )
        {
            NativeArgs* a = (NativeArgs*)user;
            if ( a->observed )
            {
                a->observed->count = count;
                if ( count > 0 )
                {
                    a->observed->image = (uintptr_t)res[0].image;
                    a->observed->width = res[0].width;
                    a->observed->height = res[0].height;
                    a->observed->format = res[0].format;
                }
            }

            // writeBuffer uploads on the GPU, not through a host mapping.
            VkBufferMemoryBarrier srcBarrier{};
            srcBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcBarrier.buffer = a->buffer;
            srcBarrier.offset = 0;
            srcBarrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier( ctx.cmdbuf,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 1, &srcBarrier, 0, nullptr );

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
        },
        &args, 0, resources
    );

    // Ordinary consumption on the same list: no drain between the callback and
    // this dispatch.
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    EXPECT_EQ( observed.count, 1u );
    EXPECT_EQ( observed.image, (uintptr_t)vkImage );
    EXPECT_EQ( observed.width, (uint32_t)DIM );
    EXPECT_EQ( observed.height, (uint32_t)DIM );
    EXPECT_EQ( (uint32_t)observed.format, (uint32_t)VK_FORMAT_R8_UNORM );

    vhMem texRead;
    vhReadTextureSlow( verifyTex, 0, 0, &texRead );
    vhFinish();

    int mismatches = 0;
    if ( (int)texRead.size() != DIM * DIM )
    {
        { UTEST_PRINTF( "NV01: readback size %d, expected %d\n", (int)texRead.size(), DIM * DIM ); }
        ++mismatches;
    }
    for ( int y = 0; y < DIM && mismatches < 4 && (int)texRead.size() == DIM * DIM; ++y )
        for ( int x = 0; x < DIM && mismatches < 4; ++x )
        {
            uint8_t got = texRead[(size_t)y * DIM + x];
            uint8_t expect = pattern[(size_t)y * DIM + x];
            // Shader round-trips the byte through unorm floats.
            int rounded = (int)( ( expect / 255.0f ) * 255.0f + 0.5f );
            if ( abs( (int)got - rounded ) > 1 )
            {
                { UTEST_PRINTF( "NV01: (%d,%d) = %d, expected %d\n", x, y, got, expect ); }
                ++mismatches;
            }
        }
    EXPECT_EQ( mismatches, 0 );

    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhDestroyBuffer( stageBuf );
    vhDestroyTexture( outTex );
    vhDestroyTexture( verifyTex );
    vhDestroyShader( cs );
    vhFinish();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

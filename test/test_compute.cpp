/*
    -- Vrhi --

    Copyright 2026 UAA Software
*/

#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "test.h"
#include <vrhi_internal.h>
#include <vrhi.h>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;

void Helper_FillPattern( std::vector<uint8_t>& data, int width, int height )
{
    data.resize( width * height );
    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            data[y * width + x] = static_cast<uint8_t>( ( x + y ) % 256 );
        }
    }
}

struct Compute {};
UTEST_F_SETUP( Compute )
{
    // g_vhInit.logBackendCmds = true;
    // g_vhInit.logPSOCache = true;
    // g_vhInit.renderdoc = true;
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
UTEST_F_TEARDOWN( Compute )
{
    vhEndMarker();
}

UTEST_F( Compute, EndToEnd_TextureWrite )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Allocate and Create 8x8 R8 Texture
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ComputeBasicOut", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Compute Shader Source
    // Writes (x+y)/255.0 to output
    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            float val = float((id.x + id.y) % 256) / 255.0;
            g_Out[id.xy] = val;
        }
    )";

    // Compile and Create Shader
    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "CS_TexWrite",
        csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirv,
        "main",
        {}, {}, &error
    );
    if ( !success ) std::cout << "Shader Compile Error: " << error << std::endl;
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_TexWrite", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // Setup State
    vhState state = g_state0; // Copy base state
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );
    
    vhState::TextureBinding tb;
    tb.name = "g_Out";
    tb.texture = outTex;
    tb.dimensionOverride = nvrhi::TextureDimension::Texture2D;
    tb.formatOverride = nvrhi::Format::R8_UNORM;
    tb.computeUAV = true;
    state.SetTexture( 0, tb );

    // Upload State and Dispatch
    vhStateId sid = 123;
    vhSetState( sid, state );
    
    vhDispatch( sid, { 1, 1, 1 } ); // 1 group, 8x8 threads total
    
    // Readback and Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    ASSERT_EQ( readData.size(), 64 * 1 );

    if ( !g_vhInit.nullMode )
    {
        for ( int y = 0; y < 8; ++y )
        {
            for ( int x = 0; x < 8; ++x )
            {
                uint8_t expected = ( x + y ) % 256;
                uint8_t actual = readData[y * 8 + x];
                EXPECT_NEAR( actual, expected, 1 );
            }
        }
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, ReadFromTexture )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Setup Resources
    vhTexture inTex = vhAllocTexture();
    vhTexture outTex = vhAllocTexture();

    int width = 8, height = 8;
    std::vector<uint8_t> hostData;
    Helper_FillPattern( hostData, width, height );

    vhMem* initData = vhAllocMem( hostData.size() );
    memcpy( initData->data(), hostData.data(), hostData.size() );

    vhCreateTexture2D( inTex, "ComputeCopyIn", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, initData );
    vhCreateTexture2D( outTex, "ComputeCopyOut", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Shader: Copy Texture to Texture
    const char* csSource = R"(
        Texture2D<float> g_In : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_In[id.xy];
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_TexRead", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !success ) std::cout << error << std::endl;
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_TexRead", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // State
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::TextureBinding tbIn;
    tbIn.name = "g_In";
    tbIn.texture = inTex;
    state.SetTexture( 0, tbIn );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 1, tbOut );

    vhStateId sid = 124;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    // Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readData.size(), 64 );
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 64; ++i )
        {
            EXPECT_NEAR( readData[i], hostData[i], 1 );
        }
    }

    vhDestroyTexture( inTex );
    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, ReadFromBuffer )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    // Input Buffer: 64 floats (matching 8x8 pixels)
    // Output Texture: 8x8 R8
    vhBuffer inBuf = vhAllocBuffer();
    vhTexture outTex = vhAllocTexture();

    int width = 8, height = 8;
    int count = width * height;
    
    vhMem* data = vhAllocMem( count * sizeof( float ) );
    float* fData = reinterpret_cast<float*>( data->data() );
    for ( int i = 0; i < count; ++i )
    {
        // Pattern: i / 255.0
        fData[i] = static_cast<float>( i ) / 255.0f;
    }

    vhCreateStorageBuffer( inBuf, "InBuf", data, count * sizeof( float ), VRHI_BUFFER_COMPUTE_READ );
    vhCreateTexture2D( outTex, "ComputeBufReadOut", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Shader
    const char* csSource = R"(
        ByteAddressBuffer g_InRaw : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint idx = id.y * 8 + id.x;
            float val = asfloat(g_InRaw.Load(idx * 4));
            g_Out[id.xy] = val;
        }
    )";
    
    std::vector<uint32_t> spirv;
    std::string err;
    bool res = vhCompileShader( "CS_BufRead", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err );
    if ( !res ) printf( "Shader Compile Error: %s\n", err.c_str() );
    ASSERT_TRUE( res );
    
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BufRead", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // State
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::BufferBinding bIn;
    bIn.name = "g_InRaw";
    bIn.buffer = inBuf;
    bIn.byteSize = count * sizeof( float );
    state.SetBuffer( 0, bIn );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );

    vhStateId sid = 125;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } ); // 64 threads
    
    // Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readData.size(), 64 );
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 64; ++i )
        {
            // Expected: i (since we wrote i/255.0 and R8 stores round(val*255))
            uint8_t expected = static_cast<uint8_t>( i );
            EXPECT_NEAR( expected, readData[i], 1 );
        }
    }

    vhDestroyBuffer( inBuf );
    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}


UTEST_F( Compute, ReadFromBuffer_Unbound )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    // g_vhInit.logBackendCmds = true;
    // g_vhInit.logPSOCache = true;
    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    // Input Buffer: 64 floats (matching 8x8 pixels)
    // Output Texture: 8x8 R8
    vhBuffer inBuf = vhAllocBuffer();
    vhTexture outTex = vhAllocTexture();

    int width = 8, height = 8;
    int count = width * height;

    vhMem* data = vhAllocMem( count * sizeof( float ) );
    float* fData = reinterpret_cast<float*>( data->data() );
    for ( int i = 0; i < count; ++i )
    {
        // Pattern: i / 255.0
        fData[i] = static_cast<float>( i ) / 255.0f;
    }

    vhCreateStorageBuffer( inBuf, "InBuf", data, count * sizeof( float ), VRHI_BUFFER_COMPUTE_READ );
    vhCreateTexture2D( outTex, "ComputeUnboundOut", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Shader
    const char* csSource = R"(
        ByteAddressBuffer g_InRaw : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            // uint idx = id.y * 8 + id.x;
            g_Out[id.xy] = 0.5;
        }
    )";

    std::vector<uint32_t> spirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_BufRead_Unbound", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_BufRead_Unbound", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // State
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::BufferBinding bIn;
    bIn.name = "g_InRaw";
    bIn.buffer = inBuf;
    bIn.byteSize = count * sizeof( float );
    state.SetBuffer( 0, bIn );

    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tbOut );

    vhStateId sid = 125;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } ); // 64 threads

    // Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readData.size(), 64 );
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 64; ++i )
        {
            // Expected: i (since we wrote i/255.0 and R8 stores round(val*255))
            uint8_t expected = 127;
            EXPECT_NEAR( expected, readData[i], 1 );
        }
    }

    vhDestroyBuffer( inBuf );
    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, EndToEnd_UniformsAndConstants )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ComputeUniformsOut", { 1, 1 }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );

    vhBuffer userCB = vhAllocBuffer();
    float userConstants[4] = { 10.0f, 20.0f, 30.0f, 40.0f };
    vhMem* userData = vhAllocMem( sizeof( userConstants ) );
    memcpy( userData->data(), userConstants, sizeof( userConstants ) );
    vhCreateUniformBuffer( userCB, "UserCB", userData, sizeof( userConstants ) );

    // Shader
    // Expecting:
    // b0 -> Global Uniforms (View/Proj)
    // b1 -> World Uniforms (World Matrix)
    // b2 -> User Constant Buffer
    const char* csSource = R"(
        cbuffer GlobalUniforms : register(b0, VRHI_STAGE_SPACE)
        {
            float4 u_viewRect;
            float4 u_viewTexel;
            float4x4 u_view;
            float4x4 u_invView;
            float4x4 u_proj;
        };

        cbuffer WorldUniforms : register(b1, VRHI_STAGE_SPACE)
        {
            float4x4 u_world[4];
        };

        cbuffer UserCB : register(b2, VRHI_STAGE_SPACE)
        {
            float4 u_userData;
        };

        [[vk::image_format("r32f")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            // Read specific values to verify correct binding slots and data upload.
            
            // From Global: u_proj[0][0] set to 5.0
            float valGlobal = u_proj[0][0];

            // From World: u_world[0][3][0] (m30 - x translation) set to 7.0
            float valWorld = u_world[0][3][0];

            // From User: u_userData.x set to 10.0
            float valUser = u_userData.x;

            float result = valGlobal + valWorld + valUser;
            g_Out[id.xy] = result;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string err;
    bool res = vhCompileShader( "CS_Uniforms", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0 | VRHI_SHADER_ROW_MAJOR, spirv, "main", {}, {}, &err );
    if ( !res ) printf( "Shader Compile Error: %s\n", err.c_str() );
    ASSERT_TRUE( res );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Uniforms", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // State
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    // Initialise Global Uniforms via View/Proj
    glm::mat4 proj = glm::mat4( 1.0f );
    proj[0][0] = 5.0f;
    state.SetViewTransform( glm::mat4( 1.0f ), proj );

    // Initialise World Uniforms
    glm::mat4 world = glm::mat4( 1.0f );
    world[3][0] = 7.0f;
    state.SetWorldTransform( world );

    // Setup User Constant Buffer
    vhState::BufferBinding bUser;
    bUser.name = "UserCB";
    bUser.buffer = userCB;
    state.SetBuffer( 0, bUser );

    // Setup Output
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::R32_FLOAT;
    state.SetTexture( 0, tbOut );

    // Dispatch
    vhStateId sid = 126;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    // Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readData.size(), 4 );
    if ( !g_vhInit.nullMode )
    {
        float* fData = reinterpret_cast< float* >( readData.data() );
        EXPECT_NEAR( 22.0f, fData[0], 0.001f );
    }

    vhDestroyTexture( outTex );
    vhDestroyBuffer( userCB );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, DispatchIndirect )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    // Output Texture: 8x8 R8
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ComputeIndirectOut", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Indirect Buffer
    vhBuffer indirectBuf = vhAllocBuffer();
    struct IndirectCmd { uint32_t x, y, z; };
    IndirectCmd cmd = { 1, 1, 1 }; // 1 group of 8x8 threads
    
    // Create CPU staging data
    vhMem* cmdData = vhAllocMem( sizeof( cmd ) );
    memcpy( cmdData->data(), &cmd, sizeof( cmd ) );

    // Create Indirect Buffer with REQUIRED FLAG
    vhCreateStorageBuffer( indirectBuf, "IndirectBuf", cmdData, sizeof( cmd ), VRHI_BUFFER_DRAW_INDIRECT );

    // Shader
    // Same as basic test: write 1.0 (white)
    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = 1.0; 
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_Indirect", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Indirect", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // State
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::TextureBinding tb;
    tb.name = "g_Out";
    tb.texture = outTex;
    tb.computeUAV = true;
    tb.formatOverride = nvrhi::Format::R8_UNORM;
    state.SetTexture( 0, tb );

    vhStateId sid = 127;
    vhSetState( sid, state );
    
    // Test: Indirect Dispatch
    // Offset 0
    vhDispatchIndirect( sid, indirectBuf, 0 );
    
    // Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readData.size(), 64 );
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 64; ++i )
        {
            EXPECT_EQ( readData[i], 255 );
        }
    }

    // Cleanup
    vhDestroyTexture( outTex );
    vhDestroyBuffer( indirectBuf );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, MultipleStageSpaceBindings )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Setup 3 input textures with different patterns
    vhTexture texA = vhAllocTexture();
    vhTexture texB = vhAllocTexture();
    vhTexture texC = vhAllocTexture();
    vhTexture outSum = vhAllocTexture();
    vhTexture outWeighted = vhAllocTexture();

    int width = 4, height = 4;
    int count = width * height;

    // Pattern A: x coordinate (0,1,2,3 repeating)
    std::vector<uint8_t> dataA;
    dataA.resize( count );
    for ( int i = 0; i < count; ++i )
    {
        int x = i % width;
        dataA[i] = static_cast<uint8_t>( x );
    }

    // Pattern B: y coordinate (0,0,0,0, 1,1,1,1, etc)
    std::vector<uint8_t> dataB;
    dataB.resize( count );
    for ( int i = 0; i < count; ++i )
    {
        int y = i / width;
        dataB[i] = static_cast<uint8_t>( y );
    }

    // Pattern C: constant 10
    std::vector<uint8_t> dataC;
    dataC.resize( count, 10 );

    vhMem* initA = vhAllocMem( dataA.size() );
    vhMem* initB = vhAllocMem( dataB.size() );
    vhMem* initC = vhAllocMem( dataC.size() );
    memcpy( initA->data(), dataA.data(), dataA.size() );
    memcpy( initB->data(), dataB.data(), dataB.size() );
    memcpy( initC->data(), dataC.data(), dataC.size() );

    vhCreateTexture2D( texA, "TexA", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, initA );
    vhCreateTexture2D( texB, "TexB", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, initB );
    vhCreateTexture2D( texC, "TexC", { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, initC );
    vhCreateTexture2D( outSum, "OutSum", { width, height }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );
    vhCreateTexture2D( outWeighted, "OutWeighted", { width, height }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );

    // 4 constant buffers with different values
    vhBuffer cbA = vhAllocBuffer();
    vhBuffer cbB = vhAllocBuffer();
    vhBuffer cbC = vhAllocBuffer();
    vhBuffer cbOffset = vhAllocBuffer();

    float weightA[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float weightB[4] = { 0.0f, 2.0f, 0.0f, 0.0f };
    float weightC[4] = { 0.0f, 0.0f, 3.0f, 0.0f };
    float offsetVal[4] = { 5.0f, 0.0f, 0.0f, 0.0f };

    vhMem* dataA_CB = vhAllocMem( sizeof( weightA ) );
    vhMem* dataB_CB = vhAllocMem( sizeof( weightB ) );
    vhMem* dataC_CB = vhAllocMem( sizeof( weightC ) );
    vhMem* dataOffset_CB = vhAllocMem( sizeof( offsetVal ) );
    memcpy( dataA_CB->data(), weightA, sizeof( weightA ) );
    memcpy( dataB_CB->data(), weightB, sizeof( weightB ) );
    memcpy( dataC_CB->data(), weightC, sizeof( weightC ) );
    memcpy( dataOffset_CB->data(), offsetVal, sizeof( offsetVal ) );

    vhCreateUniformBuffer( cbA, "ParamsA", dataA_CB, sizeof( weightA ) );
    vhCreateUniformBuffer( cbB, "ParamsB", dataB_CB, sizeof( weightB ) );
    vhCreateUniformBuffer( cbC, "ParamsC", dataC_CB, sizeof( weightC ) );
    vhCreateUniformBuffer( cbOffset, "ParamsOffset", dataOffset_CB, sizeof( offsetVal ) );

    // Shader with multiple VRHI_STAGE_SPACE bindings
    // Note: b0 and b1 are reserved for GlobalUniforms and WorldUniforms
    // User constant buffers must start at b2
    const char* csSource = R"(
        Texture2D<float> g_TexA : register(t0, VRHI_STAGE_SPACE);
        Texture2D<float> g_TexB : register(t1, VRHI_STAGE_SPACE);
        Texture2D<float> g_TexC : register(t2, VRHI_STAGE_SPACE);

        [[vk::image_format("r32f")]] RWTexture2D<float> g_OutSum : register(u0, VRHI_STAGE_SPACE);
        [[vk::image_format("r32f")]] RWTexture2D<float> g_OutWeighted : register(u1, VRHI_STAGE_SPACE);

        cbuffer ParamsA : register(b2, VRHI_STAGE_SPACE) { float4 u_weightA; };
        cbuffer ParamsB : register(b3, VRHI_STAGE_SPACE) { float4 u_weightB; };
        cbuffer ParamsC : register(b4, VRHI_STAGE_SPACE) { float4 u_weightC; };
        cbuffer ParamsOffset : register(b5, VRHI_STAGE_SPACE) { float u_offset; };

        [numthreads(4, 4, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            float valA = g_TexA[id.xy];
            float valB = g_TexB[id.xy];
            float valC = g_TexC[id.xy];
            
            float sum = valA + valB + valC;
            float weighted = valA * u_weightA.x + valB * u_weightB.y + valC * u_weightC.z + u_offset;
            
            g_OutSum[id.xy] = sum;
            g_OutWeighted[id.xy] = weighted;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader( "CS_MultiBindings", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !success ) std::cout << "Shader Compile Error: " << error << std::endl;
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_MultiBindings", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // Setup state with all bindings
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    // Bind 3 input textures at t0, t1, t2 (slots 0, 1, 2)
    vhState::TextureBinding tbA;
    tbA.name = "g_TexA";
    tbA.texture = texA;
    state.SetTexture( 0, tbA );

    vhState::TextureBinding tbB;
    tbB.name = "g_TexB";
    tbB.texture = texB;
    state.SetTexture( 1, tbB );

    vhState::TextureBinding tbC;
    tbC.name = "g_TexC";
    tbC.texture = texC;
    state.SetTexture( 2, tbC );

    // Bind 2 output UAVs at u0, u1 (slots 3, 4 - separate from SRV slots)
    vhState::TextureBinding tbOutSum;
    tbOutSum.name = "g_OutSum";
    tbOutSum.texture = outSum;
    tbOutSum.computeUAV = true;
    tbOutSum.formatOverride = nvrhi::Format::R32_FLOAT;
    state.SetTexture( 3, tbOutSum );

    vhState::TextureBinding tbOutWeighted;
    tbOutWeighted.name = "g_OutWeighted";
    tbOutWeighted.texture = outWeighted;
    tbOutWeighted.computeUAV = true;
    tbOutWeighted.formatOverride = nvrhi::Format::R32_FLOAT;
    state.SetTexture( 4, tbOutWeighted );

    // Bind 4 constant buffers at b0, b1, b2, b3
    vhState::BufferBinding bbA;
    bbA.name = "ParamsA";
    bbA.buffer = cbA;
    state.SetBuffer( 0, bbA );

    vhState::BufferBinding bbB;
    bbB.name = "ParamsB";
    bbB.buffer = cbB;
    state.SetBuffer( 1, bbB );

    vhState::BufferBinding bbC;
    bbC.name = "ParamsC";
    bbC.buffer = cbC;
    state.SetBuffer( 2, bbC );

    vhState::BufferBinding bbOffset;
    bbOffset.name = "ParamsOffset";
    bbOffset.buffer = cbOffset;
    state.SetBuffer( 3, bbOffset );

    // Dispatch
    vhStateId sid = 128;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    // Readback and verify
    vhMem readSum;
    vhMem readWeighted;
    vhReadTextureSlow( outSum, 0, 0, &readSum );
    vhReadTextureSlow( outWeighted, 0, 0, &readWeighted );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readSum.size(), count * sizeof( float ) );
    ASSERT_EQ( readWeighted.size(), count * sizeof( float ) );

    if ( !g_vhInit.nullMode )
    {
        float* fSum = reinterpret_cast< float* >( readSum.data() );
        float* fWeighted = reinterpret_cast< float* >( readWeighted.data() );

        for ( int y = 0; y < height; ++y )
        {
            for ( int x = 0; x < width; ++x )
            {
                int idx = y * width + x;
                // R8_UNORM textures normalize to 0-1 range when read as float
                float valA = static_cast< float >( x ) / 255.0f;
                float valB = static_cast< float >( y ) / 255.0f;
                float valC = 10.0f / 255.0f;

                float expectedSum = valA + valB + valC;
                // weighted = A*1 + B*2 + C*3 + offset
                // u_weightA.x = 1.0, u_weightB.y = 2.0, u_weightC.z = 3.0, u_offset = 5.0
                float expectedWeighted = valA * 1.0f + valB * 2.0f + valC * 3.0f + 5.0f;

                EXPECT_NEAR( fSum[idx], expectedSum, 0.001f );
                EXPECT_NEAR( fWeighted[idx], expectedWeighted, 0.001f );
            }
        }
    }

    // Cleanup
    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyTexture( texC );
    vhDestroyTexture( outSum );
    vhDestroyTexture( outWeighted );
    vhDestroyBuffer( cbA );
    vhDestroyBuffer( cbB );
    vhDestroyBuffer( cbC );
    vhDestroyBuffer( cbOffset );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, RawUniforms_Exhaustive )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ComputeRawUniformsOut", { 1, 1 }, 1, nvrhi::Format::RGBA32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );

    // Exhaustive Shader with "raw dog" uniforms
    const char* csSource = R"(
        uniform float g_f;
        uniform float2 g_f2;
        uniform float3 g_f3;
        uniform float4 g_f4;
        uniform int g_i;
        uniform uint g_u;
        uniform bool g_b;
        uniform float4x4 g_m44;
        uniform float g_array[4];

        [[vk::image_format("rgba32f")]] RWTexture2D<float4> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            float4 res = float4(0, 0, 0, 0);
            
            // Channel 0: Sum of float types (expecting 1.1 + 2.2 + 4.4 + 7.7 = 15.4)
            res.x = g_f + g_f2.x + g_f3.x + g_f4.x;
            
            // Channel 1: Sum of int types (expecting -42 + 123 + (1.0 if true) = 82)
            res.y = float(g_i) + float(g_u) + (g_b ? 1.0f : 0.0f);

            // Channel 2: Matrix check (expecting g_m44[0][3] which is 7.0)
            res.z = g_m44[0][3];
            
            // Channel 3: Array check (expecting g_array[3] which is 40.0)
            res.w = g_array[3];

            g_Out[id.xy] = res;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string err;
    // Note: VRHI_SHADER_ROW_MAJOR is used to match how we set matrix data usually.
    // VRHI_SHADER_PATCH_DSET0 is REQUIRED for raw uniforms to move them from set 0 to the stage set.
    bool res = vhCompileShader( "CS_RawUniforms", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0 | VRHI_SHADER_ROW_MAJOR | VRHI_SHADER_PATCH_DSET0, spirv, "main", {}, {}, &err );
    if ( !res ) printf( "Shader Compile Error: %s\n", err.c_str() );
    ASSERT_TRUE( res );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_RawUniforms", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // State
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    // Set Uniforms
    state.SetUniform( 0, { "g_f", { glm::vec4( 1.1f, 0, 0, 0 ) } } );
    state.SetUniform( 1, { "g_f2", { glm::vec4( 2.2f, 3.3f, 0, 0 ) } } );
    state.SetUniform( 2, { "g_f3", { glm::vec4( 4.4f, 5.5f, 6.6f, 0 ) } } );
    state.SetUniform( 3, { "g_f4", { glm::vec4( 7.7f, 8.8f, 9.9f, 10.1f ) } } );
    
    int iVal = -42;
    uint32_t uVal = 123;
    uint32_t bVal = 1; // bool is usually 4 bytes in HLSL CBs
    state.SetUniform( 4, { "g_i", { glm::vec4( *(float*)&iVal, 0, 0, 0 ) } } );
    state.SetUniform( 5, { "g_u", { glm::vec4( *(float*)&uVal, 0, 0, 0 ) } } );
    state.SetUniform( 6, { "g_b", { glm::vec4( *(float*)&bVal, 0, 0, 0 ) } } );

    glm::mat4 m44 = glm::mat4( 1.0f );
    m44[3][0] = 7.0f; // Row-major translation X if using row-major storage
    // HLSL float4x4 row-major:
    // row0: [0][0], [0][1], [0][2], [0][3]
    // ...
    // row3: [3][0], [3][1], [3][2], [3][3]
    // glm::mat4 is column-major:
    // col0: [0][0], [0][1], [0][2], [0][3]
    // col3: [3][0], [3][1], [3][2], [3][3]
    // If we pass col-major glm matrix to row-major HLSL, it will be transposed.
    // So we transpose it here to compensate.
    glm::mat4 m44_row = glm::transpose( m44 );
    state.SetUniform( 7, { "g_m44", { m44_row[0], m44_row[1], m44_row[2], m44_row[3] } } );

    state.SetUniform( 8, { "g_array", { 
        glm::vec4( 10.0f, 0, 0, 0 ), 
        glm::vec4( 20.0f, 0, 0, 0 ), 
        glm::vec4( 30.0f, 0, 0, 0 ), 
        glm::vec4( 40.0f, 0, 0, 0 ) 
    } } );

    // Setup Output
    vhState::TextureBinding tbOut;
    tbOut.name = "g_Out";
    tbOut.texture = outTex;
    tbOut.computeUAV = true;
    tbOut.formatOverride = nvrhi::Format::RGBA32_FLOAT;
    state.SetTexture( 0, tbOut );

    // Dispatch
    vhStateId sid = 129;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    // Verify
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    ASSERT_GT( g_vhPSOCompileCounter.load(), startPSOs );
    ASSERT_EQ( readData.size(), sizeof(float) * 4 );
    if ( !g_vhInit.nullMode )
    {
        float* fData = reinterpret_cast< float* >( readData.data() );
        
        // Channel 0: 1.1 + 2.2 + 4.4 + 7.7 = 15.4
        EXPECT_NEAR( 15.4f, fData[0], 0.0001f );
        // Channel 1: -42 + 123 + 1 = 82
        EXPECT_NEAR( 82.0f, fData[1], 0.0001f );
        // Channel 2: 7.0 (Row 0, Col 3 in row-major storage)
        EXPECT_NEAR( 7.0f, fData[2], 0.0001f );
        // Channel 3: 40.0
        EXPECT_NEAR( 40.0f, fData[3], 0.0001f );
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, StaleUniformReproduction )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    
    // Output Texture: 1x1 R32F
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "StaleUniformOut", { 1, 1 }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );

    // Simple shader using a raw uniform
    const char* csSource = R"(
        uniform float g_val;
        [[vk::image_format("r32f")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_val;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_StaleRepro", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0 | VRHI_SHADER_PATCH_DSET0, spirv, "main", {}, {}, &err ) );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_StaleRepro", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    // Duplicate shader with same layout but different name
    vhShader cs2 = vhAllocShader();
    vhCreateShader( cs2, "CS_StaleRepro2", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::TextureBinding tb;
    tb.name = "g_Out";
    tb.texture = outTex;
    tb.computeUAV = true;
    tb.formatOverride = nvrhi::Format::R32_FLOAT;
    state.SetTexture( 0, tb );

    vhStateId sid = 200;

    // First Dispatch: g_val = 1.0, Shader 1
    state.SetUniform( 0, { "g_val", { glm::vec4( 1.0f, 0, 0, 0 ) } } );
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    // Second Dispatch: g_val = 2.0, Shader 2 (same layout hash!)
    state.SetProgram( vhCreateComputeProgram( cs2 ) );
    state.SetUniform( 0, { "g_val", { glm::vec4( 2.0f, 0, 0, 0 ) } } );
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    // Readback
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    if ( !g_vhInit.nullMode )
    {
        float result = *reinterpret_cast< float* >( readData.data() );
        printf( "RESULT: %f\n", result );
        // THIS SHOULD FAIL IF THE BUG IS PRESENT (it will likely return 1.0 instead of 2.0)
        EXPECT_EQ( 2.0f, result );
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhDestroyShader( cs2 );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, StaleUniformReproduction_MultiShader )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();
    
    // Output Texture: 1x1 RGBA32F
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "StaleUniformOutMS", { 1, 1 }, 1, nvrhi::Format::RGBA32_FLOAT, VRHI_TEXTURE_RT );

    // Vertex Shader
    const char* vsSource = R"(
        uniform float g_val;
        void main(uint v : SV_VulkanVertexID, out float4 p : SV_Position)
        {
            float2 uv = float2((v << 1) & 2, v & 2);
            p = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        }
    )";

    // Pixel Shader using SAME uniform
    const char* psSource = R"(
        uniform float g_val;
        void main(out float4 c : SV_Target0)
        {
            c = float4(g_val, 0, 0, 1);
        }
    )";

    std::vector< uint32_t > vsSpirv, psSpirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "VS_StaleRepro", vsSource, VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_0 | VRHI_SHADER_PATCH_DSET0, vsSpirv, "main", {}, {}, &err ) );
    ASSERT_TRUE( vhCompileShader( "PS_StaleRepro", psSource, VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0 | VRHI_SHADER_PATCH_DSET0, psSpirv, "main", {}, {}, &err ) );

    vhShader vs = vhAllocShader();
    vhCreateShader( vs, "VS_StaleRepro", VRHI_SHADER_STAGE_VERTEX, vsSpirv, "main" );
    vhShader ps = vhAllocShader();
    vhCreateShader( ps, "PS_StaleRepro", VRHI_SHADER_STAGE_PIXEL, psSpirv, "main" );

    vhState state = g_state0;
    state.SetProgram( { vs, ps } );
    state.SetViewRect( glm::vec4( 0, 0, 1, 1 ) );
    state.SetStateFlags( VRHI_STATE_WRITE_MASK );
    
    vhState::RenderTarget rt;
    rt.texture = outTex;
    state.colourAttachment.push_back( rt );

    vhStateId sid = 201;

    // First Draw: g_val = 1.0
    state.SetUniform( 0, { "g_val", { glm::vec4( 1.0f, 0, 0, 0 ) } } );
    vhSetState( sid, state, VRHI_DIRTY_ALL );
    vhDraw( sid, 3 );

    // Second Draw: g_val = 2.0
    state.SetUniform( 0, { "g_val", { glm::vec4( 2.0f, 0, 0, 0 ) } } );
    vhSetState( sid, state, VRHI_DIRTY_ALL );
    vhDraw( sid, 3 );

    // Readback
    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    if ( !g_vhInit.nullMode )
    {
        float* fData = reinterpret_cast< float* >( readData.data() );
        printf( "RESULT MS: %f\n", fData[0] );
        EXPECT_EQ( 2.0f, fData[0] );
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// ==========================================================================
// Phase 7 - Group 1: Compute expansion
// ==========================================================================

UTEST_F( Compute, Atomics_BufferUAV )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();

    // Counter buffer initialised to 0; 1024 threads each InterlockedAdd 1.
    // Result must be 1024 (8x8x16 thread groups, each with single thread).
    uint32_t initial = 0;
    vhBuffer counterBuf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( sizeof( uint32_t ) );
    memcpy( mem->data(), &initial, sizeof( uint32_t ) );
    vhCreateStorageBuffer( counterBuf, "Counter", mem, sizeof( uint32_t ),
        VRHI_BUFFER_COMPUTE_READ_WRITE,
        sizeof( uint32_t ) );

    // Output texture so we can verify dispatch ran — write 1 per pixel.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "AtomOut", { 32, 32 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        RWStructuredBuffer<uint> g_Counter : register(u1, VRHI_STAGE_SPACE);

        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint orig;
            InterlockedAdd(g_Counter[0], 1, orig);
            g_Out[id.xy] = 1.0;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Atom", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Atom", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
    state.SetBuffer( 0, { .name = "g_Counter", .buffer = counterBuf, .computeUAV = true } );

    vhStateId sid = 8000;
    vhSetState( sid, state );
    vhDispatch( sid, { 4, 4, 1 } );  // 4*4*64 = 1024 threads
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();
    EXPECT_EQ( readData.size(), 32 * 32 );
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 32 * 32; ++i ) EXPECT_EQ( readData[i], 255 );
    }

    vhMem counterData;
    vhReadBufferSlow( counterBuf, 0, sizeof( uint32_t ), &counterData );
    if ( !g_vhInit.nullMode && counterData.size() >= sizeof( uint32_t ) )
    {
        uint32_t result = 0;
        memcpy( &result, counterData.data(), sizeof( uint32_t ) );
        EXPECT_EQ( result, 1024u );  // 4*4 groups * 8*8 threads
    }

    vhDestroyTexture( outTex );
    vhDestroyBuffer( counterBuf );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, WorkgroupSizeVariations )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();

    // Three dispatches with different numthreads layouts writing same pattern.
    // All must produce the same 16x16 output.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "WGOut", { 16, 16 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    auto Compile = []( const char* src, const char* name ) -> vhShader
    {
        std::vector< uint32_t > spirv;
        std::string err;
        bool ok = vhCompileShader( name, src, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err );
        if ( !ok )
        {
            UTEST_PRINTF( "Shader %s compile failed: %s\n", name, err.c_str() );
            return VRHI_INVALID_HANDLE;
        }
        vhShader cs = vhAllocShader();
        vhCreateShader( cs, name, VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
        return cs;
    };

    const char* cs1 = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";
    const char* cs8 = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";
    const char* cs64 = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(16, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";

    vhShader s1 = Compile( cs1, "CS_WG_1" );
    vhShader s2 = Compile( cs8, "CS_WG_8" );
    vhShader s3 = Compile( cs64, "CS_WG_16x1" );
    ASSERT_NE( s1, VRHI_INVALID_HANDLE );
    ASSERT_NE( s2, VRHI_INVALID_HANDLE );
    ASSERT_NE( s3, VRHI_INVALID_HANDLE );

    auto RunCheck = [&]( vhShader cs, glm::uvec3 groups, vhStateId sid )
    {
        vhState state = g_state0;
        state.SetProgram( vhCreateComputeProgram( cs ) );
        state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
        vhSetState( sid, state );
        vhDispatch( sid, groups );
        vhFinish();
        vhMem rd; vhReadTextureSlow( outTex, 0, 0, &rd ); vhFinish();
        if ( !g_vhInit.nullMode )
        {
            for ( int i = 0; i < 16 * 16; ++i ) EXPECT_EQ( rd[i], 255 );
        }
    };

    RunCheck( s1, { 16, 16, 1 }, 8001 );
    RunCheck( s2, { 2, 2, 1 }, 8002 );
    RunCheck( s3, { 1, 16, 1 }, 8003 );

    vhDestroyTexture( outTex );
    vhDestroyShader( s1 );
    vhDestroyShader( s2 );
    vhDestroyShader( s3 );
    vhSetState( 8001, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, MultiDispatchReadback )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();

    // Three sequential dispatches each write a different value. Verify the
    // last value persists. Tests barrier insertion between back-to-back
    // dispatches writing the same UAV.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "MultiOut", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    auto Compile = []( const char* src, const char* name ) -> vhShader
    {
        std::vector< uint32_t > spirv;
        std::string err;
        bool ok = vhCompileShader( name, src, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err );
        if ( !ok )
        {
            UTEST_PRINTF( "Shader %s compile failed: %s\n", name, err.c_str() );
            return VRHI_INVALID_HANDLE;
        }
        vhShader cs = vhAllocShader();
        vhCreateShader( cs, name, VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
        return cs;
    };

    const char* tmpl = R"([[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = %s; })";
    char buf1[1024], buf2[1024], buf3[1024];
    snprintf( buf1, sizeof( buf1 ), tmpl, "0.25" );
    snprintf( buf2, sizeof( buf2 ), tmpl, "0.50" );
    snprintf( buf3, sizeof( buf3 ), tmpl, "1.00" );

    vhShader s1 = Compile( buf1, "CS_MD1" );
    vhShader s2 = Compile( buf2, "CS_MD2" );
    vhShader s3 = Compile( buf3, "CS_MD3" );
    ASSERT_NE( s1, VRHI_INVALID_HANDLE );
    ASSERT_NE( s2, VRHI_INVALID_HANDLE );
    ASSERT_NE( s3, VRHI_INVALID_HANDLE );

    auto Run = [&]( vhShader cs, vhStateId sid )
    {
        vhState state = g_state0;
        state.SetProgram( vhCreateComputeProgram( cs ) );
        state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
        vhSetState( sid, state );
        vhDispatch( sid, { 1, 1, 1 } );
    };

    Run( s1, 8010 );
    Run( s2, 8011 );
    Run( s3, 8012 );

    vhMem rd;
    vhReadTextureSlow( outTex, 0, 0, &rd );
    vhFinish();
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 64; ++i ) EXPECT_EQ( rd[i], 255 );  // last dispatch wrote 1.0
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( s1 );
    vhDestroyShader( s2 );
    vhDestroyShader( s3 );
    vhSetState( 8010, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, LargeDispatch )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();

    // 1024x1024 single dispatch (numthreads(8,8,1) x 128x128 groups).
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "LargeOut", { 1024, 1024 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8, 8, 1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";

    std::vector< uint32_t > spirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Large", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Large", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );

    vhStateId sid = 8020;
    vhSetState( sid, state );
    vhDispatch( sid, { 128, 128, 1 } );
    vhFinish();

    vhMem rd;
    vhReadTextureSlow( outTex, 0, 0, &rd );
    vhFinish();
    if ( !g_vhInit.nullMode )
    {
        // Spot-check four corners and centre.
        EXPECT_EQ( rd[0], 255 );
        EXPECT_EQ( rd[1023], 255 );
        EXPECT_EQ( rd[1023 * 1024], 255 );
        EXPECT_EQ( rd[1023 * 1024 + 1023], 255 );
        EXPECT_EQ( rd[512 * 1024 + 512], 255 );
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, IndirectDispatch_Chained )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();

    // Pass 1: compute writes (1, 1, 1) into an indirect args buffer.
    // Pass 2: indirect dispatch reads those args and writes pixels.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ChainOut", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    uint32_t init[3] = { 0, 0, 0 };
    vhBuffer indirectBuf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( sizeof( init ) );
    memcpy( mem->data(), init, sizeof( init ) );
    vhCreateStorageBuffer( indirectBuf, "IndirectArgs", mem, sizeof( init ),
        VRHI_BUFFER_COMPUTE_READ_WRITE | VRHI_BUFFER_DRAW_INDIRECT,
        sizeof( uint32_t ) );

    const char* csWriteArgs = R"(
        RWStructuredBuffer<uint> g_Args : register(u0, VRHI_STAGE_SPACE);
        [numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID)
        {
            g_Args[0] = 1;
            g_Args[1] = 1;
            g_Args[2] = 1;
        }
    )";
    const char* csConsume = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";

    auto Compile = []( const char* src, const char* name ) -> vhShader
    {
        std::vector< uint32_t > spirv;
        std::string err;
        bool ok = vhCompileShader( name, src, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err );
        if ( !ok )
        {
            UTEST_PRINTF( "Shader %s compile failed: %s\n", name, err.c_str() );
            return VRHI_INVALID_HANDLE;
        }
        vhShader cs = vhAllocShader();
        vhCreateShader( cs, name, VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
        return cs;
    };

    vhShader sWrite = Compile( csWriteArgs, "CS_WriteArgs" );
    vhShader sConsume = Compile( csConsume, "CS_Consume" );
    ASSERT_NE( sWrite, VRHI_INVALID_HANDLE );
    ASSERT_NE( sConsume, VRHI_INVALID_HANDLE );

    vhState s1 = g_state0;
    s1.SetProgram( vhCreateComputeProgram( sWrite ) );
    s1.SetBuffer( 0, { .name = "g_Args", .buffer = indirectBuf, .computeUAV = true } );
    vhStateId sid1 = 8030;
    vhSetState( sid1, s1 );
    vhDispatch( sid1, { 1, 1, 1 } );

    vhState s2 = g_state0;
    s2.SetProgram( vhCreateComputeProgram( sConsume ) );
    s2.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
    vhStateId sid2 = 8031;
    vhSetState( sid2, s2 );
    vhDispatchIndirect( sid2, indirectBuf, 0 );
    vhFinish();

    vhMem rd;
    vhReadTextureSlow( outTex, 0, 0, &rd );
    vhFinish();
    if ( !g_vhInit.nullMode )
    {
        for ( int i = 0; i < 64; ++i ) EXPECT_EQ( rd[i], 255 );
    }

    vhDestroyTexture( outTex );
    vhDestroyBuffer( indirectBuf );
    vhDestroyShader( sWrite );
    vhDestroyShader( sConsume );
    vhSetState( sid1, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, Benchmark_DispatchRate )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute shaders require GPU in Null RHI mode" );
    }

    vhFlush();

    // 1000 small dispatches, time total. Print rate. Does not assert threshold.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "BenchOut", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";
    std::vector< uint32_t > spirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Bench", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Bench", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
    vhStateId sid = 8500;
    vhSetState( sid, state );

    auto t0 = std::chrono::high_resolution_clock::now();
    for ( int i = 0; i < 1000; ++i ) vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();
    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast< std::chrono::microseconds >( t1 - t0 ).count();
    UTEST_PRINTF( "Benchmark: 1000 compute dispatches in %lld us (%.2f us/dispatch)\n", ( long long ) us, double( us ) / 1000.0 );

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, Validation_DispatchZeroDimensions )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "GPU required" );
    vhFlush();

    // Compute dispatch with 0 group count must not crash and must early-out.
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ZeroOut", { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";
    std::vector< uint32_t > spirv;
    std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Zero", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Zero", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
    vhStateId sid = 8200;
    vhSetState( sid, state );

    // Zero dimensions: should not crash.
    vhDispatch( sid, { 0, 0, 0 } );
    vhDispatch( sid, { 0, 1, 1 } );
    vhDispatch( sid, { 1, 0, 1 } );
    vhDispatch( sid, { 1, 1, 0 } );
    vhFinish();

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, Validation_DispatchWithoutShader )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "GPU required" );
    vhFlush();

    // Enable the skipped-draw error path so the dispatch logs the missing-program
    // condition; restore prior value at the end.
    bool prevErrorOnSkippedDraw = g_vhInit.errorOnSkippedDraw;
    g_vhInit.errorOnSkippedDraw = true;

    int32_t startErrors = g_vhErrorCounter.load();

    // Dispatch without setting a program. Must log error, must not crash.
    vhState state = g_state0;
    vhStateId sid = 8201;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    // Error counter must have incremented (at least once).
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

    g_vhInit.errorOnSkippedDraw = prevErrorOnSkippedDraw;

    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// ComputeThenGraphics
// --------------------------------------------------------------------------

UTEST_F( Compute, ComputeThenGraphics )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    // CS writes a solid green pattern, GFX samples it and outputs to a separate RT
    vhTexture csOut = vhAllocTexture();
    vhCreateTexture2D( csOut, "CS2GfxTex", { 4, 4 }, 1, nvrhi::Format::RGBA8_UNORM,
                       VRHI_TEXTURE_COMPUTE_WRITE | VRHI_TEXTURE_NONE );
    vhTexture gfxOut = vhAllocTexture();
    vhCreateTexture2D( gfxOut, "GfxOut", { 4, 4 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhFinish();

    const char* csSource = R"(
        [[vk::image_format("rgba8")]] RWTexture2D<float4> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,4,1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = float4(0,1,0,1); }
    )";
    std::vector< uint32_t > spirv; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Fill", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Fill", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState csState = g_state0;
    csState.SetProgram( vhCreateComputeProgram( cs ) );
    csState.SetTexture( 0, { .name = "g_Out", .texture = csOut, .computeUAV = true } );
    vhStateId csid = 9100;
    vhSetState( csid, csState );
    vhDispatch( csid, { 1, 1, 1 } );
    vhFinish();

    const char* vsSource = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };
[shader("vertex")]
VSOut main(uint vid : SV_VulkanVertexID) {
    float2 p[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
    VSOut o; o.pos = float4(p[vid], 0, 1); o.uv = (p[vid]+1)*0.5; return o;
}
)";
    const char* psSource = R"(
Texture2D g_Tex : register(t0, VRHI_STAGE_SPACE);
SamplerState g_Sam : register(s0, VRHI_STAGE_SPACE);
[shader("pixel")]
float4 main(float2 uv : TEXCOORD) : SV_Target { return g_Tex.Sample(g_Sam, uv); }
)";
    std::vector< uint32_t > vsSpirv, psSpirv;
    ASSERT_TRUE( vhCompileShader( "VS_Quad2", vsSource, VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_0, vsSpirv, "main", {}, {}, &err ) );
    ASSERT_TRUE( vhCompileShader( "PS_Tex2",  psSource, VRHI_SHADER_STAGE_PIXEL  | VRHI_SHADER_SM_6_0, psSpirv, "main", {}, {}, &err ) );
    vhShader vs = vhAllocShader(); vhCreateShader( vs, "VS_Quad2", VRHI_SHADER_STAGE_VERTEX, vsSpirv );
    vhShader ps = vhAllocShader(); vhCreateShader( ps, "PS_Tex2",  VRHI_SHADER_STAGE_PIXEL,  psSpirv );

    vhState gState = g_state0;
    gState.SetColourAttachment( 0, gfxOut )
          .SetViewRect( glm::vec4( 0, 0, 4, 4 ) )
          .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0 ) )
          .SetStateFlags( VRHI_STATE_WRITE_MASK )
          .SetTexture( 0, { .name = "g_Tex", .texture = csOut } )
          .SetSampler( 0, { .name = "g_Sam", .flags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } )
          .SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId gsid = 9101;
    vhSetState( gsid, gState );
    vhClear( gsid, VRHI_CLEAR_COLOR );
    vhDraw( gsid, 3 );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( gfxOut, 0, 0, &readData );
    vhFinish();
    EXPECT_EQ( readData.size(), 64u );
    for ( int px = 0; px < 4 * 4; px++ )
    {
        EXPECT_EQ( readData[px*4+0], 0 );    // R
        EXPECT_EQ( readData[px*4+1], 255 );  // G
        EXPECT_EQ( readData[px*4+2], 0 );    // B
    }

    vhDestroyTexture( csOut ); vhDestroyTexture( gfxOut );
    vhDestroyShader( cs ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( csid, g_state0, VRHI_DIRTY_ALL );
    vhSetState( gsid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// TypedStorageBuffer
// --------------------------------------------------------------------------

UTEST_F( Compute, TypedStorageBuffer )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    const int N = 16;
    float hostData[N];
    for ( int i = 0; i < N; i++ ) hostData[i] = float( i ) / float( N - 1 );

    vhBuffer typedBuf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( N * sizeof( float ) );
    memcpy( mem->data(), hostData, N * sizeof( float ) );
    vhCreateStorageTypedBuffer( typedBuf, "TypedF32", mem, N * sizeof( float ), nvrhi::Format::R32_FLOAT, VRHI_BUFFER_COMPUTE_READ );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "TypedOut", { N, 1 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        Buffer<float> g_In : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(16,1,1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[uint2(id.x, 0)] = g_In[id.x];
        }
    )";
    std::vector< uint32_t > spirv; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Typed", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Typed", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetBuffer( 0, { .name = "g_In", .buffer = typedBuf } );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );

    vhStateId sid = 9200;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    EXPECT_EQ( (int)readData.size(), N );
    for ( int i = 0; i < N; i++ )
    {
        uint8_t expected = ( uint8_t ) roundf( hostData[i] * 255.0f );
        EXPECT_TRUE( abs( (int)readData[i] - (int)expected ) <= 1 );
    }

    vhDestroyTexture( outTex ); vhDestroyBuffer( typedBuf ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// BufferBindingByteRange
// --------------------------------------------------------------------------

UTEST_F( Compute, BufferBindingByteRange )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
#ifdef __APPLE__
    UTEST_SKIP( "Buffer subbuffer range binding unreliable on MoltenVK" );
#endif
    vhFlush();

    const uint32_t sectionSize = 16;
    uint8_t sections[3][sectionSize];
    memset( sections[0], 0xAA, sectionSize );
    memset( sections[1], 0xBB, sectionSize );
    memset( sections[2], 0xCC, sectionSize );

    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( sizeof( sections ) );
    memcpy( mem->data(), sections, sizeof( sections ) );
    vhCreateStorageBuffer( buf, "SectionBuf", mem, sizeof( sections ), VRHI_BUFFER_COMPUTE_READ );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "RangeOut", { 4, 1 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        ByteAddressBuffer g_Buf : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,1,1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            uint b = g_Buf.Load(id.x * 4);
            g_Out[uint2(id.x,0)] = float(b & 0xFF) / 255.0;
        }
    )";
    std::vector< uint32_t > spirv; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Range", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Range", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    vhState::BufferBinding bb; bb.name="g_Buf"; bb.buffer=buf; bb.byteOffset=sectionSize; bb.byteSize=sectionSize;
    state.SetBuffer( 0, bb );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );

    vhStateId sid = 9300;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    EXPECT_EQ( (int)readData.size(), 4 );
    for ( int i = 0; i < 4; i++ ) EXPECT_EQ( readData[i], 0xBB );

    vhDestroyTexture( outTex ); vhDestroyBuffer( buf ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// PushConstants_Compute
// --------------------------------------------------------------------------

UTEST_F( Compute, PushConstants_Compute )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
#ifdef __APPLE__
    UTEST_SKIP( "Push constants in compute shaders unreliable on MoltenVK" );
#endif
    vhFlush();

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "PushOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        struct PushConst { float4 tint; };
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,4,1)]
        void main(uint3 id : SV_DispatchThreadID, uniform PushConst pc : push_constant)
        {
            g_Out[id.xy] = pc.tint.x;
        }
    )";
    std::vector< uint32_t > spirv; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Push", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_Push", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name = "g_Out", .texture = outTex, .computeUAV = true } );
    state.SetPushConstants( glm::vec4( 0.5f, 0, 0, 0 ) );

    vhStateId sid = 9400;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    EXPECT_EQ( (int)readData.size(), 16 );
    for ( int i = 0; i < 16; i++ ) EXPECT_TRUE( abs( (int)readData[i] - 127 ) <= 2 );

    vhDestroyTexture( outTex ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// MultiDispatchChained
// --------------------------------------------------------------------------

UTEST_F( Compute, MultiDispatchChained )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    const int N = 4;
    vhBuffer bufA = vhAllocBuffer();
    vhBuffer bufB = vhAllocBuffer();
    vhCreateStorageStructuredBuffer( bufA, "BufA", nullptr, N * sizeof( float ), sizeof( float ) );
    vhCreateStorageStructuredBuffer( bufB, "BufB", nullptr, N * sizeof( float ), sizeof( float ) );
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "ChainOut", { N, 1 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* cs1Src = R"(
        RWStructuredBuffer<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,1,1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.x] = 0.25; }
    )";
    const char* cs2Src = R"(
        StructuredBuffer<float> g_In  : register(t0, VRHI_STAGE_SPACE);
        RWStructuredBuffer<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,1,1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[id.x] = g_In[id.x] * 2.0; }
    )";
    const char* cs3Src = R"(
        StructuredBuffer<float> g_In : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,1,1)]
        void main(uint3 id : SV_DispatchThreadID) { g_Out[uint2(id.x,0)] = g_In[id.x]; }
    )";

    auto compile = [&]( const char* src, const char* name, vhShader& sh )
    {
        std::vector<uint32_t> spirv; std::string er;
        ASSERT_TRUE( vhCompileShader( name, src, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &er ) );
        sh = vhAllocShader();
        vhCreateShader( sh, name, VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
    };

    vhShader cs1 = VRHI_INVALID_HANDLE, cs2 = VRHI_INVALID_HANDLE, cs3 = VRHI_INVALID_HANDLE;
    compile( cs1Src, "CS_Chain1", cs1 );
    compile( cs2Src, "CS_Chain2", cs2 );
    compile( cs3Src, "CS_Chain3", cs3 );

    vhState s1 = g_state0; s1.SetProgram( vhCreateComputeProgram( cs1 ) );
    s1.SetBuffer( 0, { .name="g_Out", .buffer=bufA, .computeUAV=true } );
    vhStateId sid1 = 9500; vhSetState( sid1, s1 ); vhDispatch( sid1, {1,1,1} ); vhFinish();

    vhState s2 = g_state0; s2.SetProgram( vhCreateComputeProgram( cs2 ) );
    s2.SetBuffer( 0, { .name="g_In",  .buffer=bufA } );
    s2.SetBuffer( 1, { .name="g_Out", .buffer=bufB, .computeUAV=true } );
    vhStateId sid2 = 9501; vhSetState( sid2, s2 ); vhDispatch( sid2, {1,1,1} ); vhFinish();

    vhState s3 = g_state0; s3.SetProgram( vhCreateComputeProgram( cs3 ) );
    s3.SetBuffer( 0, { .name="g_In", .buffer=bufB } );
    s3.SetTexture( 0, { .name="g_Out", .texture=outTex, .computeUAV=true } );
    vhStateId sid3 = 9502; vhSetState( sid3, s3 ); vhDispatch( sid3, {1,1,1} ); vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    EXPECT_EQ( (int)readData.size(), N );
    for ( int i = 0; i < N; i++ ) EXPECT_TRUE( abs( (int)readData[i] - 127 ) <= 2 );

    vhDestroyBuffer( bufA ); vhDestroyBuffer( bufB ); vhDestroyTexture( outTex );
    vhDestroyShader( cs1 ); vhDestroyShader( cs2 ); vhDestroyShader( cs3 );
    vhSetState( sid1, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid3, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// BlitBufferAndVerify
// --------------------------------------------------------------------------

UTEST_F( Compute, BlitBufferAndVerify )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    const uint32_t N = 16;
    uint8_t pattern[N];
    for ( uint32_t i = 0; i < N; i++ ) pattern[i] = (uint8_t)( i * 7 + 3 );

    vhBuffer src = vhAllocBuffer();
    vhBuffer dst = vhAllocBuffer();
    vhMem* srcMem = vhAllocMem( N );
    memcpy( srcMem->data(), pattern, N );
    vhCreateStorageBuffer( src, "BlitSrc", srcMem, N, VRHI_BUFFER_COMPUTE_READ );
    vhCreateStorageBuffer( dst, "BlitDst", nullptr, N, VRHI_BUFFER_COMPUTE_READ_WRITE );
    vhFinish();

    vhBlitBuffer( dst, src );
    vhFinish();

    vhMem readData;
    vhReadBufferSlow( dst, 0, N, &readData );

    EXPECT_EQ( (int)readData.size(), (int)N );
    for ( uint32_t i = 0; i < N; i++ ) EXPECT_EQ( readData[i], pattern[i] );

    vhDestroyBuffer( src ); vhDestroyBuffer( dst );
    vhFinish();
}

// --------------------------------------------------------------------------
// CounterBufferTypedView
// --------------------------------------------------------------------------

UTEST_F( Compute, CounterBufferTypedView )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    // Create a typed UAV RWBuffer<float4> and write values to it, then read back.
    const int N = 4;
    vhBuffer buf = vhAllocBuffer();
    vhCreateStorageTypedBuffer( buf, "TypedUAV", nullptr, N * 16, nvrhi::Format::RGBA32_FLOAT );
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "TypedUAVOut", { N, 1 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        RWBuffer<float4> g_Out : register(u0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Tex : register(u1, VRHI_STAGE_SPACE);
        [numthreads(4,1,1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.x] = float4(float(id.x+1) / 4.0, 0, 0, 1);
            g_Tex[uint2(id.x,0)] = float(id.x+1) / 4.0;
        }
    )";
    std::vector< uint32_t > spirv; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_TypedUAV", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader(); vhCreateShader( cs, "CS_TypedUAV", VRHI_SHADER_STAGE_COMPUTE, spirv );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetBuffer( 0, { .name="g_Out", .buffer=buf, .computeUAV=true } );
    state.SetTexture( 1, { .name="g_Tex", .texture=outTex, .computeUAV=true } );
    vhStateId sid = 9800;
    vhSetState( sid, state ); vhDispatch( sid, {1,1,1} ); vhFinish();

    vhMem texData; vhReadTextureSlow( outTex, 0, 0, &texData ); vhFinish();
    EXPECT_EQ( (int)texData.size(), N );
    for ( int i = 0; i < N; i++ )
    {
        uint8_t expected = (uint8_t)( (float)(i+1)/4.0f * 255.0f + 0.5f );
        EXPECT_TRUE( abs( (int)texData[i] - (int)expected ) <= 2 );
    }

    vhDestroyBuffer( buf ); vhDestroyTexture( outTex ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

// --------------------------------------------------------------------------
// UAV_To_SRV_Barrier
// --------------------------------------------------------------------------

UTEST_F( Compute, UAV_To_SRV_Barrier )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    // Pass 1 writes to a UAV texture, then Pass 2 reads it as SRV.
    // Tests that VRHI properly inserts barriers between UAV write and SRV read.
    vhTexture intermediate = vhAllocTexture();
    vhCreateTexture2D( intermediate, "UAV2SRV", { 4, 4 }, 1, nvrhi::Format::R8_UNORM,
                       VRHI_TEXTURE_COMPUTE_WRITE | VRHI_TEXTURE_NONE );
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "SRVOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* cs1Src = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,4,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 0.75; }
    )";
    const char* cs2Src = R"(
        Texture2D<float> g_In : register(t0, VRHI_STAGE_SPACE);
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(4,4,1)] void main(uint3 id : SV_DispatchThreadID)
        {
            float v = g_In.Load(uint3(id.xy, 0));
            g_Out[id.xy] = v * 0.5;  // Halve the value
        }
    )";
    std::vector< uint32_t > spirv1, spirv2; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_UAV1", cs1Src, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv1, "main", {}, {}, &err ) );
    ASSERT_TRUE( vhCompileShader( "CS_SRV2", cs2Src, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv2, "main", {}, {}, &err ) );
    vhShader cs1 = vhAllocShader(); vhCreateShader( cs1, "CS_UAV1", VRHI_SHADER_STAGE_COMPUTE, spirv1 );
    vhShader cs2 = vhAllocShader(); vhCreateShader( cs2, "CS_SRV2", VRHI_SHADER_STAGE_COMPUTE, spirv2 );

    vhState s1 = g_state0;
    s1.SetProgram( vhCreateComputeProgram( cs1 ) );
    s1.SetTexture( 0, { .name="g_Out", .texture=intermediate, .computeUAV=true } );
    vhStateId sid1 = 9600;
    vhSetState( sid1, s1 ); vhDispatch( sid1, {1,1,1} ); vhFinish();

    vhState s2 = g_state0;
    s2.SetProgram( vhCreateComputeProgram( cs2 ) );
    s2.SetTexture( 0, { .name="g_In", .texture=intermediate } );
    s2.SetTexture( 1, { .name="g_Out", .texture=outTex, .computeUAV=true } );
    vhStateId sid2 = 9601;
    vhSetState( sid2, s2 ); vhDispatch( sid2, {1,1,1} ); vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    // 0.75 * 0.5 = 0.375 → R8 ~= 96
    EXPECT_EQ( (int)readData.size(), 16 );
    for ( int i = 0; i < 16; i++ ) EXPECT_TRUE( abs( (int)readData[i] - 96 ) <= 2 );

    vhDestroyTexture( intermediate ); vhDestroyTexture( outTex );
    vhDestroyShader( cs1 ); vhDestroyShader( cs2 );
    vhSetState( sid1, g_state0, VRHI_DIRTY_ALL ); vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// --------------------------------------------------------------------------
// DispatchIndirect_NonZeroOffset
// --------------------------------------------------------------------------

UTEST_F( Compute, DispatchIndirect_NonZeroOffset )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    // Indirect dispatch buffer with 2 entries: first is {0,0,0} (no-op), second is {1,1,1}
    struct DispatchArgs { uint32_t x, y, z; };
    DispatchArgs args[2] = { {0,0,0}, {1,1,1} };
    vhBuffer indBuf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( sizeof( args ) );
    memcpy( mem->data(), args, sizeof( args ) );
    vhCreateStorageBuffer( indBuf, "IndDsp", mem, sizeof( args ), VRHI_BUFFER_DRAW_INDIRECT );

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "IndDspOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhFinish();

    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(1,1,1)] void main() { g_Out[uint2(0,0)] = 1.0; }
    )";
    std::vector< uint32_t > spirv; std::string err;
    ASSERT_TRUE( vhCompileShader( "CS_Ind", csSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err ) );
    vhShader cs = vhAllocShader(); vhCreateShader( cs, "CS_Ind", VRHI_SHADER_STAGE_COMPUTE, spirv );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name="g_Out", .texture=outTex, .computeUAV=true } );
    vhStateId sid = 9700;
    vhSetState( sid, state );

    // Dispatch at byte offset = sizeof(DispatchArgs) = 12 → should hit {1,1,1}
    vhDispatchIndirect( sid, indBuf, sizeof( DispatchArgs ) );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( outTex, 0, 0, &readData );
    vhFinish();

    EXPECT_EQ( readData[0], 255u );  // Pixel (0,0) was written

    vhDestroyBuffer( indBuf ); vhDestroyTexture( outTex ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

// --------------------------------------------------------------------------
// BlitBuffer_PartialRange
// --------------------------------------------------------------------------

UTEST_F( Compute, BlitBuffer_PartialRange )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required" ); }
    vhFlush();

    const uint32_t total = 64;
    uint8_t srcData[total];
    for ( uint32_t i = 0; i < total; i++ ) srcData[i] = (uint8_t)( i + 1 );

    uint8_t dstInit[total];
    memset( dstInit, 0xEE, total );

    vhBuffer src = vhAllocBuffer();
    vhBuffer dst = vhAllocBuffer();
    vhMem* srcMem = vhAllocMem( total ); memcpy( srcMem->data(), srcData, total );
    vhMem* dstMem = vhAllocMem( total ); memcpy( dstMem->data(), dstInit, total );
    vhCreateStorageBuffer( src, "PSrc", srcMem, total, VRHI_BUFFER_COMPUTE_READ );
    vhCreateStorageBuffer( dst, "PDst", dstMem, total, VRHI_BUFFER_COMPUTE_READ_WRITE );
    vhFinish();

    vhBlitBuffer( dst, src, 32, 16, 16 );
    vhFinish();

    vhMem readData;
    vhReadBufferSlow( dst, 0, total, &readData );

    EXPECT_EQ( (int)readData.size(), (int)total );
    for ( int i = 0; i < 32; i++ ) EXPECT_EQ( readData[i], 0xEE );
    for ( int i = 0; i < 16; i++ ) EXPECT_EQ( readData[32+i], srcData[16+i] );
    for ( int i = 48; i < (int)total; i++ ) EXPECT_EQ( readData[i], 0xEE );

    vhDestroyBuffer( src ); vhDestroyBuffer( dst );
    vhFinish();
}

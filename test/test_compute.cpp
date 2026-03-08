/*
    -- Vrhi --

    Copyright 2026 UAA Software
*/

#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "test.h"
#include <vrhi.h>
#include <vrhi_internal.h>

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
        void main(uint v : SV_VertexID, out float4 p : SV_Position)
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

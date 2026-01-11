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
#include "utest.h"
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
    g_vhInit.renderdoc = true;
    g_vhInit.markers = true;
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhCaptureStart();
    vhBeginMarker( "Compute Test" );
}
UTEST_F_TEARDOWN( Compute )
{
    vhEndMarker();
    vhCaptureEnd();
}

UTEST_F( Compute, EndToEnd_TextureWrite )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Allocate and Create 8x8 R8 Texture
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Compute Shader Source
    // Writes (x+y)/255.0 to output
    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out;
        
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

    for ( int y = 0; y < 8; ++y )
    {
        for ( int x = 0; x < 8; ++x )
        {
            uint8_t expected = ( x + y ) % 256;
            uint8_t actual = readData[y * 8 + x];
            EXPECT_NEAR( actual, expected, 1 );
        }
    }

    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, ReadFromTexture )
{
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

    vhCreateTexture2D( inTex, { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_NONE, initData );
    vhCreateTexture2D( outTex, { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Shader: Copy Texture to Texture
    const char* csSource = R"(
        Texture2D<float> g_In;
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out;

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
    for ( int i = 0; i < 64; ++i )
    {
        EXPECT_NEAR( readData[i], hostData[i], 1 );
    }

    vhDestroyTexture( inTex );
    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, ReadFromBuffer )
{
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
    vhCreateTexture2D( outTex, { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Shader
    const char* csSource = R"(
        ByteAddressBuffer g_InRaw;
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out;
        
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
    for ( int i = 0; i < 64; ++i )
    {
        // Expected: i (since we wrote i/255.0 and R8 stores round(val*255))
        uint8_t expected = static_cast<uint8_t>( i );
        EXPECT_NEAR( expected, readData[i], 1 );
    }

    vhDestroyBuffer( inBuf );
    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}


UTEST_F( Compute, ReadFromBuffer_Unbound )
{
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
    vhCreateTexture2D( outTex, { width, height }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Shader
    const char* csSource = R"(
        ByteAddressBuffer g_InRaw;
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out;
        
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
    for ( int i = 0; i < 64; ++i )
    {
        // Expected: i (since we wrote i/255.0 and R8 stores round(val*255))
        uint8_t expected = 127;
        EXPECT_NEAR( expected, readData[i], 1 );
    }

    vhDestroyBuffer( inBuf );
    vhDestroyTexture( outTex );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, EndToEnd_UniformsAndConstants )
{
    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, { 1, 1 }, 1, nvrhi::Format::R32_FLOAT, VRHI_TEXTURE_COMPUTE_WRITE );

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
        cbuffer GlobalUniforms : register(b0)
        {
            float4 u_viewRect;
            float4 u_viewTexel;
            float4x4 u_view;
            float4x4 u_invView;
            float4x4 u_proj;
        };

        cbuffer WorldUniforms : register(b1)
        {
            float4x4 u_world[4];
        };

        cbuffer UserCB : register(b2)
        {
            float4 u_userData;
        };

        [[vk::image_format("r32f")]] RWTexture2D<float> g_Out;

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
    float* fData = reinterpret_cast< float* >( readData.data() );
    EXPECT_NEAR( 22.0f, fData[0], 0.001f );

    vhDestroyTexture( outTex );
    vhDestroyBuffer( userCB );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST_F( Compute, DispatchIndirect )
{
    vhFlush();
    int32_t startPSOs = g_vhPSOCompileCounter.load();

    // Resources
    // Output Texture: 8x8 R8
    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, { 8, 8 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

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
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out;
        
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
    for ( int i = 0; i < 64; ++i )
    {
        EXPECT_EQ( readData[i], 255 );
    }

    // Cleanup
    vhDestroyTexture( outTex );
    vhDestroyBuffer( indirectBuf );
    vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

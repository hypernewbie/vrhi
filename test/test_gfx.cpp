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

#include "utest.h"
#include "test.h"
#include <vrhi.h>
#include <vector>
#include <string>

extern bool g_testInit;
extern bool g_testInitQuiet;

// --------------------------------------------------------------------------
// Slang Shaders
// --------------------------------------------------------------------------

static const char* g_simpleVS = R"(
struct VSInput
{
    float3 pos : POSITION;
    float4 colour : COLOUR;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 colour : COLOUR;
};

[shader("vertex")]
VSOutput main( VSInput input )
{
    VSOutput output;
    output.pos = float4( input.pos, 1.0 );
    output.colour = input.colour;
    return output;
}
)";

static const char* g_solidPS = R"(
[shader("pixel")]
float4 main( float4 colour : COLOUR ) : SV_Target
{
    return colour;
}
)";

static const char* g_texPS = R"(
Texture2D t0 : register( t200, space1 );
SamplerState s0 : register( s100, space1 );

[shader("pixel")]
float4 main( float2 uv : TEXCOORD ) : SV_Target
{
    return t0.Sample( s0, uv );
}
)";

static const char* g_mrtPS = R"(
struct PSOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
};

[shader("pixel")]
PSOutput main()
{
    PSOutput output;
    output.target0 = float4( 1.0, 0.0, 0.0, 1.0 ); // Red
    output.target1 = float4( 0.0, 1.0, 0.0, 1.0 ); // Green
    return output;
}
)";

static const char* g_uvVS = R"(
struct VSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("vertex")]
VSOutput main( VSInput input )
{
    VSOutput output;
    output.pos = float4( input.pos, 1.0 );
    output.uv = input.uv;
    return output;
}
)";

static const char* g_multiTexPS = R"(
Texture2D t0 : register( t200, space1 );
Texture2D t1 : register( t201, space1 );
SamplerState s0 : register( s100, space1 );

[shader("pixel")]
float4 main( float2 uv : TEXCOORD ) : SV_Target
{
    float4 c0 = t0.Sample( s0, uv );
    float4 c1 = t1.Sample( s0, uv );
    return c0 + c1;
}
)";

static const char* g_uniformPS = R"(
struct MyUniforms
{
    float4 tint;
};

ConstantBuffer<MyUniforms> u0 : register( b300, space1 );

[shader("pixel")]
float4 main() : SV_Target
{
    return u0.tint;
}
)";

static const char* g_pushConstPS = R"(
struct PushConstants
{
    float4 tint;
    float4x4 mvp;
};
[shader("pixel")]
float4 main( uniform PushConstants pc : push_constant ) : SV_Target
{
    return pc.tint;
}
)";

static const char* g_instancedVS = R"(
struct VSInput
{
    float3 pos : POSITION;
    float4 colour : COLOUR;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float4 colour : COLOUR;
};

[shader("vertex")]
VSOutput main( VSInput input )
{
    VSOutput output;
    float offset = float( input.instanceID ) * 0.1;
    output.pos = float4( input.pos.x + offset, input.pos.yz, 1.0 );
    output.colour = input.colour;
    return output;
}
)";

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static vhTexture CreateTestTexture( int32_t w, int32_t h, nvrhi::Format format, uint64_t flags = VRHI_TEXTURE_RT )
{
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( w, h ), 1, format, flags );
    return tex;
}

static vhBuffer CreateTestVB( const char* layout, const void* data, uint32_t size )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    memcpy( mem->data(), data, size );
    vhCreateVertexBuffer( buf, "TestVB", mem, layout );
    return buf;
}

static vhBuffer CreateTestIB( const void* data, uint32_t size )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    memcpy( mem->data(), data, size );
    vhCreateIndexBuffer( buf, "TestIB", mem );
    return buf;
}

static vhShader CreateTestShader( const char* source, uint64_t stage )
{
    vhShader shader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( "TestShader", source, stage, spirv, "main", {}, {}, &error );
    if ( !ok )
    {
        UTEST_PRINTF( "Shader Compilation Error: %s\n", error.c_str() );
    }
    vhCreateShader( shader, "TestShader", stage, spirv );
    return shader;
}

static bool VerifyPixel( vhTexture rt, int32_t x, int32_t y, uint32_t expectedRGBA, uint32_t tolerance = 2 )
{
    vhMem readData;
    vhReadTextureSlow( rt, 0, 0, &readData );
    vhFinish();

    vhTexInfo info = vhGetTextureInfo( rt );
    if ( readData.size() == 0 ) return false;

    // RGBA8 assumed
    int32_t offset = ( y * info.dimensions.x + x ) * 4;
    
    uint8_t r = readData[offset + 0];
    uint8_t g = readData[offset + 1];
    uint8_t b = readData[offset + 2];
    uint8_t a = readData[offset + 3];

    uint8_t er = ( expectedRGBA >> 0 ) & 0xFF;
    uint8_t eg = ( expectedRGBA >> 8 ) & 0xFF;
    uint8_t eb = ( expectedRGBA >> 16 ) & 0xFF;
    uint8_t ea = ( expectedRGBA >> 24 ) & 0xFF;

    auto AbsDiff = []( uint8_t a, uint8_t b ) -> uint8_t { return a > b ? a - b : b - a; };

    if ( AbsDiff( r, er ) > tolerance ) return false;
    if ( AbsDiff( g, eg ) > tolerance ) return false;
    if ( AbsDiff( b, eb ) > tolerance ) return false;
    if ( AbsDiff( a, ea ) > tolerance ) return false;

    return true;
}

// --------------------------------------------------------------------------
// Tests
// --------------------------------------------------------------------------

struct Graphics{};
UTEST_F_SETUP( Graphics )
{
    // g_vhInit.logBackendCmds = true;
    // g_vhInit.logPSOCache = true;
    g_vhInit.markers = true;
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhBeginMarker( "Graphics Test" );
}
UTEST_F_TEARDOWN( Graphics )
{
    vhEndMarker();
}

UTEST_F( Graphics, DrawTriangle )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[3] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 3.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -1.0f, 3.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 200;
    vhSetState( sid, state );
    vhDraw( sid, 3 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF0000FF ) ); // Red

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, DrawIndexedTriangle )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[3] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { 3.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -1.0f, 3.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
    };
    uint32_t indices[3] = { 0, 1, 2 };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhBuffer ib = CreateTestIB( indices, sizeof( indices ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetIndexBuffer( ib )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 300;
    vhSetState( sid, state );
    vhDrawIndexed( sid, 3 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFFFF0000 ) ); // Blue

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( ib );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
}

UTEST_F( Graphics, DrawTriangleStrip )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[4] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetStateFlags( VRHI_STATE_PT_TRISTRIP )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 350;
    vhSetState( sid, state );
    vhDraw( sid, 4 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF00FFFF ) ); // Yellow

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, DepthTest )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    vhTexture ds = CreateTestTexture( 64, 64, nvrhi::Format::D24S8 );

    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    
    Vertex vertsFar[6] = 
    {
        { { -1.0f, -1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } }
    };

    Vertex vertsNear[6] = 
    {
        { { -1.0f, -1.0f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } }
    };

    vhBuffer vbFar = CreateTestVB( "float3 float4", vertsFar, sizeof( vertsFar ) );
    vhBuffer vbNear = CreateTestVB( "float3 float4", vertsNear, sizeof( vertsNear ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetDepthAttachment( ds )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR | VRHI_CLEAR_DEPTH, 0xFF000000, 1.0f )
         .SetStateFlags( VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    state.SetVertexBuffer( vbFar, 0 );
    
    vhStateId sidFar = 400;
    vhSetState( sidFar, state );
    vhDraw( sidFar, 6 );

    state.SetVertexBuffer( vbNear, 0 );
    
    vhStateId sidNear = 401;
    vhSetState( sidNear, state );
    vhDraw( sidNear, 6 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF00FF00 ) ); // Green

    vhDestroyTexture( rt );
    vhDestroyTexture( ds );
    vhDestroyBuffer( vbFar );
    vhDestroyBuffer( vbNear );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, StencilTest )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    vhTexture ds = CreateTestTexture( 64, 64, nvrhi::Format::D24S8 );

    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetDepthAttachment( ds )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR | VRHI_CLEAR_STENCIL, 0xFF000000, 1.0f, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) )
         .SetVertexBuffer( vb, 0 );

    // Pass 1: Write 1 to stencil
    state.SetStateFlags( VRHI_STATE_WRITE_MASK )
         .SetStencil( VRHI_STENCIL_TEST_ALWAYS | VRHI_STENCIL_OP_FAIL_S_REPLACE | VRHI_STENCIL_OP_FAIL_Z_REPLACE | VRHI_STENCIL_OP_PASS_Z_REPLACE | VRHI_STENCIL_FUNC_REF( 1 ) );
    
    vhStateId sid1 = 501;
    vhSetState( sid1, state );
    vhDraw( sid1, 6 );
    vhFinish();

    // Pass 2: Only draw if stencil is 0 (should draw nothing)
    state.SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetStencil( VRHI_STENCIL_TEST_EQUAL | VRHI_STENCIL_OP_FAIL_S_KEEP | VRHI_STENCIL_OP_FAIL_Z_KEEP | VRHI_STENCIL_OP_PASS_Z_KEEP | VRHI_STENCIL_FUNC_REF( 0 ) );
    
    vhStateId sid2 = 502;
    vhSetState( sid2, state );
    vhDraw( sid2, 6 );
    vhFinish();
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF000000 ) ); // Black

    // Pass 3: Only draw if stencil is 1 (should draw white)
    state.SetStencil( VRHI_STENCIL_TEST_EQUAL | VRHI_STENCIL_OP_FAIL_S_KEEP | VRHI_STENCIL_OP_FAIL_Z_KEEP | VRHI_STENCIL_OP_PASS_Z_KEEP | VRHI_STENCIL_FUNC_REF( 1 ) );
    
    vhStateId sid3 = 503;
    vhSetState( sid3, state );
    vhDraw( sid3, 6 );
    vhFinish();
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFFFFFFFF ) ); // White

    vhDestroyTexture( rt );
    vhDestroyTexture( ds );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, AlphaBlending )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetStateFlags( VRHI_STATE_WRITE_RGB | VRHI_STATE_BLEND_ALPHA )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 550;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF7F7F7F ) ); // Grey

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, Culling )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    
    // CW Triangle
    Vertex vertsCW[3] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }
    };

    // CCW Triangle
    Vertex vertsCCW[3] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }
    };

    vhBuffer vbCW = CreateTestVB( "float3 float4", vertsCW, sizeof( vertsCW ) );
    vhBuffer vbCCW = CreateTestVB( "float3 float4", vertsCCW, sizeof( vertsCCW ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    // Test 1: Cull CCW. CW should be visible (Red).
    state.SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetStateFlags( VRHI_STATE_WRITE_RGB | VRHI_STATE_CULL_CCW )
         .SetVertexBuffer( vbCW, 0 );
    
    vhStateId sidCW = 600;
    vhSetState( sidCW, state );
    vhDraw( sidCW, 3 );
    vhFinish();
    EXPECT_TRUE( VerifyPixel( rt, 16, 16, 0xFF0000FF ) ); // Red

    // Test 2: Cull CW. CCW should be visible (Green).
    state.SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetStateFlags( VRHI_STATE_WRITE_RGB | VRHI_STATE_CULL_CW )
         .SetVertexBuffer( vbCCW, 0 );
    
    vhStateId sidCCW = 601;
    vhSetState( sidCCW, state );
    vhDraw( sidCCW, 3 );
    vhFinish();
    EXPECT_TRUE( VerifyPixel( rt, 16, 16, 0xFF00FF00 ) ); // Green

    vhDestroyTexture( rt );
    vhDestroyBuffer( vbCW );
    vhDestroyBuffer( vbCCW );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, ScissorTest )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewScissor( glm::vec4( 16, 16, 32, 32 ) ) // Center 32x32
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetStateFlags( VRHI_STATE_WRITE_RGB )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 620;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    // Center should be White
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFFFFFFFF ) );
    // Corner should be Black (scissored out)
    EXPECT_TRUE( VerifyPixel( rt, 4, 4, 0xFF000000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
}

UTEST_F( Graphics, MultipleTextures )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    // Create two source textures
    vhTexture t0 = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    vhTexture t1 = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    // Fill t0 with Red, t1 with Green
    vhMem* redData = vhAllocMem( 64 * 64 * 4 ); std::fill( redData->begin(), redData->end(), 0 );
    for ( int i = 0; i < 64 * 64; i++ ) { ( *redData )[i * 4 + 0] = 255; ( *redData )[i * 4 + 3] = 255; }
    vhUpdateTexture( t0, 0, 0, 1, 1, redData );

    vhMem* greenData = vhAllocMem( 64 * 64 * 4 ); std::fill( greenData->begin(), greenData->end(), 0 );
    for ( int i = 0; i < 64 * 64; i++ ) { ( *greenData )[i * 4 + 1] = 255; ( *greenData )[i * 4 + 3] = 255; }
    vhUpdateTexture( t1, 0, 0, 1, 1, greenData );
    
    vhFinish();

    struct Vertex { glm::vec3 pos; glm::vec2 uv; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float2", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_uvVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_multiTexPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetTexture( 0, { "t0", -1, t0 } )
         .SetTexture( 1, { "t1", -1, t1 } )
         .SetSampler( 0, { "s0", -1, VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 770;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    // Red + Green = Yellow (0xFF00FFFF)
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF00FFFF ) );

    vhDestroyTexture( rt );
    vhDestroyTexture( t0 );
    vhDestroyTexture( t1 );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, TextureFormats )
{

    // RGBA16_FLOAT RT
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA16_FLOAT );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 2.0f, 0.5f, 0.1f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 2.0f, 0.5f, 0.1f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 2.0f, 0.5f, 0.1f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 2.0f, 0.5f, 0.1f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 2.0f, 0.5f, 0.1f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 2.0f, 0.5f, 0.1f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 820;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    // 2.0 -> 255, 0.5 -> 127, 0.1 -> 25
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF197FFF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, UniformBuffers )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_uniformPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetUniform( 0, { "u0", { glm::vec4( 0.0, 1.0, 1.0, 1.0 ) } } ) // Cyan tint
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 830;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFFFFFF00 ) ); // Cyan

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, PushConstants )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_pushConstPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetPushConstants( glm::vec4( 1.0, 0.0, 1.0, 1.0 ) ) // Magenta tint
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 650;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFFFF00FF ) ); // Magenta

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, MultipleRenderTargets )
{
    vhTexture rt0 = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    vhTexture rt1 = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_mrtPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt0 )
         .SetColourAttachment( 1, rt1 )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 700;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    EXPECT_TRUE( VerifyPixel( rt0, 32, 32, 0xFF0000FF ) ); // Red
    EXPECT_TRUE( VerifyPixel( rt1, 32, 32, 0xFF00FF00 ) ); // Green

    vhDestroyTexture( rt0 );
    vhDestroyTexture( rt1 );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
}

UTEST_F( Graphics, InstancedRendering )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    struct Vertex { glm::vec3 pos; glm::vec4 colour; };
    Vertex verts[3] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -0.9f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_instancedVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );

    vhStateId sid = 800;
    vhSetState( sid, state );
    vhDraw( sid, 3, 10 ); // 10 instances
    vhFinish();

    // Verify that multiple instances were drawn (some color at an offset)
    EXPECT_TRUE( VerifyPixel( rt, 4, 32, 0xFFFFFFFF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// -------------------------------------------------------------------------------------------------
// Sampler Modes Test
// -------------------------------------------------------------------------------------------------
UTEST_F( Graphics, SamplerModes )
{

    // Create a 2x2 texture with different colours in each quadrant
    uint32_t pixels[4] = {
        0xFF0000FF, 0xFF00FF00, // Red, Green
        0xFFFF0000, 0xFFFFFFFF  // Blue, White
    };
    
    vhTexture tex = CreateTestTexture( 2, 2, nvrhi::Format::RGBA8_UNORM );
    vhMem* data = vhAllocMem( sizeof( pixels ) );
    memcpy( data->data(), pixels, sizeof( pixels ) );
    vhUpdateTexture( tex, 0, 0, 1, 1, data );
    vhFinish();

    // Create shaders and program
    vhShader vs = CreateTestShader( g_uvVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_texPS, VRHI_SHADER_STAGE_PIXEL );
    vhProgram program = vhCreateGfxProgram( vs, ps );

    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    struct Vertex { glm::vec3 pos; glm::vec2 uv; };
    Vertex quad[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 2.0f, 0.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 2.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 2.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 2.0f, 0.0f } },
        { {  1.0f,  1.0f, 0.0f }, { 2.0f, 2.0f } }
    };
    vhBuffer vb = CreateTestVB( "float3 float2", quad, sizeof( quad ) );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetVertexBuffer( vb, 0 )
         .SetTexture( 0, { "t0", -1, tex } )
         .SetProgram( program );

    // Test Wrap mode
    state.SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetSampler( 0, { "s0", -1, VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_WRAP } );
    
    vhStateId sidWrap = 900;
    vhSetState( sidWrap, state );
    vhDraw( sidWrap, 6 );
    vhFinish();

    // Verify Wrap: Corner of each tiled quadrant should match (0,0) which is Red
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 32, 0, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 0, 32, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF0000FF ) );

    // Test Clamp mode
    state.SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetSampler( 0, { "s0", -1, VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } );
    
    vhStateId sidClamp = 901;
    vhSetState( sidClamp, state );
    vhDraw( sidClamp, 6 );
    vhFinish();

    // Verify Clamp: Edge colors should be stretched
    EXPECT_TRUE( VerifyPixel( rt, 63, 63, 0xFFFFFFFF ) ); // Clamped to White (bottom-right)

    vhDestroyTexture( rt );
    vhDestroyTexture( tex );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// -------------------------------------------------------------------------------------------------
// Mipmap Rendering Test
// -------------------------------------------------------------------------------------------------
UTEST_F( Graphics, MipmapRendering )
{

    // Create a texture with 2 mip levels (2x2 and 1x1)
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( 2, 2 ), 2, nvrhi::Format::RGBA8_UNORM );

    // Level 0: Red
    uint32_t pixels0[4] = { 0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF };
    vhMem* data0 = vhAllocMem( sizeof( pixels0 ) );
    memcpy( data0->data(), pixels0, sizeof( pixels0 ) );
    vhUpdateTexture( tex, 0, 0, 1, 1, data0 );

    // Level 1: Green
    uint32_t pixels1[1] = { 0xFF00FF00 };
    vhMem* data1 = vhAllocMem( sizeof( pixels1 ) );
    memcpy( data1->data(), pixels1, sizeof( pixels1 ) );
    vhUpdateTexture( tex, 1, 0, 1, 1, data1 );
    
    vhFinish();

    vhShader vs = CreateTestShader( g_uvVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_texPS, VRHI_SHADER_STAGE_PIXEL );
    vhProgram program = vhCreateGfxProgram( vs, ps );

    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    struct Vertex { glm::vec3 pos; glm::vec2 uv; };
    Vertex quad[6] = 
    {
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f } }
    };
    vhBuffer vb = CreateTestVB( "float3 float2", quad, sizeof( quad ) );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetTexture( 0, { "t0", -1, tex } )
         .SetSampler( 0, { "s0", -1, VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } )
         .SetProgram( program );

    vhStateId sid = 1000;
    vhSetState( sid, state );
    vhDraw( sid, 6 );
    vhFinish();

    // By default, it should sample Level 0 (Red)
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyTexture( tex );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// -------------------------------------------------------------------------------------------------
// Multiple Vertex Streams Test
// -------------------------------------------------------------------------------------------------
static const char* g_multiStreamVS = R"(
struct VSInput {
    float2 pos : POSITION;
    float4 col : COLOR;
};
struct VSOutput {
    float4 pos : SV_Position;
    float4 col : COLOR;
};
VSOutput main(VSInput input) {
    VSOutput output;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.col = input.col;
    return output;
}
)";

UTEST_F( Graphics, MultipleVertexStreams )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    // Stream 0: Positions
    float positions[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f };
    vhBuffer vb0 = CreateTestVB( "float2", positions, sizeof( positions ) );

    // Stream 1: Colours (Blue)
    float colours[] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    vhBuffer vb1 = CreateTestVB( "float4", colours, sizeof( colours ) );

    vhShader vs = CreateTestShader( g_multiStreamVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    vhProgram program = vhCreateGfxProgram( vs, ps );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb0, 0 )
         .SetVertexBuffer( vb1, 1 )
         .SetProgram( program );

    vhStateId sid = 1100;
    vhSetState( sid, state );
    vhDraw( sid, 3 );
    vhFinish();

    // Triangle should be Blue
    EXPECT_TRUE( VerifyPixel( rt, 16, 16, 0xFFFF0000 ) ); // Blue (0xFFRRGGBB)

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb0 );
    vhDestroyBuffer( vb1 );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// -------------------------------------------------------------------------------------------------
// Vertex Buffer Offset Test
// -------------------------------------------------------------------------------------------------
UTEST_F( Graphics, VertexBufferOffset )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    // One buffer with two triangles: Triangle 1 is Red, Triangle 2 is Green.
    // We'll draw only the second one using an offset.
    struct Vertex { glm::vec2 pos; glm::vec4 col; };
    Vertex verts[6] = {
        // Red Triangle
        { { -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -1.0f,  1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        // Green Triangle
        { { -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { {  1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -1.0f,  1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }
    };
    vhBuffer vb = CreateTestVB( "float2 float4", verts, sizeof( verts ) );

    vhShader vs = CreateTestShader( g_multiStreamVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    vhProgram program = vhCreateGfxProgram( vs, ps );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0, 3 * sizeof( Vertex ) ) // Offset to green triangle
         .SetProgram( program );

    vhStateId sid = 1200; // Unique ID
    vhSetState( sid, state );

    vhDraw( sid, 3 );
    vhFinish();

    // Should be Green
    EXPECT_TRUE( VerifyPixel( rt, 16, 16, 0xFF00FF00 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// -------------------------------------------------------------------------------------------------
// Indirect Draw Test
// -------------------------------------------------------------------------------------------------
UTEST_F( Graphics, IndirectDraw )
{
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );

    struct Vertex { glm::vec3 pos; glm::vec4 col; };
    Vertex verts[3] = {
        { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } }, // Yellow
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } },
        { { -1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } }
    };
    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );

    // Draw arguments
    nvrhi::DrawArguments args;
    args.vertexCount = 3;
    args.instanceCount = 1;
    args.startVertexLocation = 0;
    args.startInstanceLocation = 0;

    vhBuffer argBuffer = vhAllocBuffer();
    vhMem* argData = vhAllocMem( sizeof( args ) );
    memcpy( argData->data(), &args, sizeof( args ) );
    
    // Create buffer with indirect args usage
    vhCreateStorageBuffer( argBuffer, "IndirectArgs", argData, 0, VRHI_BUFFER_DRAW_INDIRECT );
    vhFinish();

    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    vhProgram program = vhCreateGfxProgram( vs, ps );

    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, 0xFF000000 )
         .SetVertexBuffer( vb, 0 )
         .SetIndirectParams( argBuffer )
         .SetProgram( program );

    vhStateId sid = 1201; // Unique ID
    vhSetState( sid, state );

    vhDrawIndirect( sid, 1 ); // 1 call
    vhFinish();

    // Should be Yellow
    EXPECT_TRUE( VerifyPixel( rt, 16, 16, 0xFF00FFFF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( argBuffer );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

UTEST_F( Graphics, ClearTexture )
{

    // Create colour render target
    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    
    // Create state and bind colour target
    vhState state;
    state.SetColourAttachment( 0, rt );
    
    vhStateId sid = 1202; // Unique ID
    vhSetState( sid, state );

    // Clear to red
    vhClear( sid, VRHI_CLEAR_COLOR, 0xFF0000FF );
    vhFlush();
    
    // Verify pixel is red
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF0000FF ) );
    
    // Clear to green
    vhClear( sid, VRHI_CLEAR_COLOR, 0xFF00FF00 );
    vhFlush();
    
    // Verify pixel is green
    EXPECT_TRUE( VerifyPixel( rt, 32, 32, 0xFF00FF00 ) );
    
    // Cleanup
    vhDestroyTexture( rt );
    
    // Create depth/stencil target
    vhTexture ds = CreateTestTexture( 64, 64, nvrhi::Format::D24S8 );
    
    // Create state and bind depth target
    vhState depthState;
    depthState.SetDepthAttachment( ds );
    
    vhStateId depthSid = 1203; // Unique ID
    vhSetState( depthSid, depthState );

    // Clear depth and stencil
    vhClear( depthSid, VRHI_CLEAR_DEPTH | VRHI_CLEAR_STENCIL, 0, 0.5f, 128 );
    vhFlush();
    
    // Cleanup
    vhDestroyTexture( ds );
    vhFinish();
}

UTEST_F( Graphics, Markers )
{

    // Test enabled markers
    g_vhInit.markers = true;
    vhBeginMarker( "Test_BeginMarker" );
    vhEndMarker();

    // Test nested markers
    vhBeginMarker( "OuterMarker" );
    vhBeginMarker( "InnerMarker" );
    vhEndMarker();
    vhEndMarker();

    // Test disabled markers
    g_vhInit.markers = false;
    vhBeginMarker( "Disabled_Marker" );
    vhEndMarker();

    // Reset to default
    g_vhInit.markers = true;

    vhFinish();
}

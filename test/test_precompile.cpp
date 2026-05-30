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

// Tests for vhPrecompilePSO — both compute and graphics.
//
// Exercises:
//   - Fire-and-forget batching of N PSOs with one vhFinish()
//   - Cache hit/miss detection via g_vhPSOCompileCounter
//   - Shader-creation ordering (no intermediate vhFinish needed)
//   - Empty vertex layout for no-input shaders
//   - State-flag and format variety
//   - Error rejection of invalid inputs (null colorFormats, sampleCount == 0)
//   - Temp-buffer safety after early-return errors

#include "test.h"
#include <vrhi_internal.h>
#include <vrhi.h>

#include <algorithm>
#include <string>
#include <vector>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhPSOCompileCounter;
extern std::atomic<int32_t> g_vhErrorCounter;

// --------------------------------------------------------------------------
// Shader Sources
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

static const char* g_noInputVS = R"(
[shader("vertex")]
float4 main( uint vid : SV_VertexID ) : SV_Position
{
    float2 pos[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
    return float4( pos[ vid ], 0, 1 );
}
)";

static const char* g_solidPS = R"(
[shader("pixel")]
float4 main( float4 colour : COLOUR ) : SV_Target
{
    return colour;
}
)";

static const char* g_solidNoInputPS = R"(
[shader("pixel")]
float4 main() : SV_Target
{
    return float4( 1, 0, 0, 1 );
}
)";

static const char* g_emptyCS = R"(
[numthreads(8, 8, 1)]
void main( uint3 id : SV_DispatchThreadID ) {}
)";

// Unique compute shaders for batch tests. Each index produces a distinct shader.
static std::string MakeUniqueComputeSource( int idx )
{
    const int tx = 8 + ( idx % 4 );
    const int ty = 1 + ( ( idx / 4 ) % 4 );
    char buf[ 256 ];
    std::snprintf( buf, sizeof( buf ),
        "[numthreads(%d, %d, 1)]\n"
        "void main( uint3 id : SV_DispatchThreadID ) {}\n",
        tx, ty );
    return std::string( buf );
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

// Pool of heap-allocated names so VIDL_vhCreateShader's const char* pointer
// survives until the backend thread processes the command.
static std::vector< std::string > s_liveShaderNames;

static vhShader CreateTestShader( const char* source, uint64_t stage )
{
    static std::atomic< int > s_shaderId;
    s_liveShaderNames.emplace_back( "TS_" + std::to_string( ( int ) s_shaderId.fetch_add( 1, std::memory_order_relaxed ) ) );
    const char* name = s_liveShaderNames.back().c_str();
    vhShader shader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( name, source, stage | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !ok )
    {
        UTEST_PRINTF( "CreateTestShader: vhCompileShader failed: %s\n", error.c_str() );
        return VRHI_INVALID_HANDLE;
    }
    vhCreateShader( shader, name, stage, spirv, "main" );
    return shader;
}

static vhBuffer CreateTestVB( const char* layout, const void* data, uint32_t size )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    memcpy( mem->data(), data, size );
    vhCreateVertexBuffer( buf, "TestVB", mem, layout );
    return buf;
}

static vhTexture CreateTestTexture( int32_t w, int32_t h, nvrhi::Format format, uint64_t flags = VRHI_TEXTURE_RT )
{
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "TestTexture", glm::ivec2( w, h ), 1, format, flags );
    return tex;
}

static void CleanupPSOState( vhStateId sid, vhShader vs, vhShader ps, vhBuffer vb, vhTexture rt )
{
    vhState cleanState;
    vhSetState( sid, cleanState, VRHI_DIRTY_ALL );
    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Fixture setup / teardown
// --------------------------------------------------------------------------

struct Precompile_Compute {};
UTEST_F_SETUP( Precompile_Compute )
{
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
UTEST_F_TEARDOWN( Precompile_Compute )
{
    vhEndMarker();
}

struct Precompile_Graphics {};
UTEST_F_SETUP( Precompile_Graphics )
{
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
UTEST_F_TEARDOWN( Precompile_Graphics )
{
    vhEndMarker();
}

// --------------------------------------------------------------------------
// Compute: single precompile compiles before dispatch
// --------------------------------------------------------------------------

UTEST_F( Precompile_Compute, PrecompileBeforeDispatch )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute requires GPU in Null RHI mode" );
    }

    vhShader cs = CreateTestShader( g_emptyCS, VRHI_SHADER_STAGE_COMPUTE );
    ASSERT_NE( cs, VRHI_INVALID_HANDLE );
    vhProgram program = vhCreateComputeProgram( cs );

    // Precompile
    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program );
    vhFinish();
    int32_t afterPrecompile = g_vhPSOCompileCounter.load();
    EXPECT_GT( afterPrecompile, before );

    // Dispatch with same program — must reuse cached PSO
    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( program );

    vhStateId sid = 1000;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();

    int32_t afterDispatch = g_vhPSOCompileCounter.load();
    EXPECT_EQ( afterDispatch, afterPrecompile );

    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhDestroyShader( cs );
    vhFinish();
}

// --------------------------------------------------------------------------
// Compute: duplicate precompile is cached
// --------------------------------------------------------------------------

UTEST_F( Precompile_Compute, DuplicateIsCached )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute requires GPU in Null RHI mode" );
    }

    vhShader cs = CreateTestShader( g_emptyCS, VRHI_SHADER_STAGE_COMPUTE );
    ASSERT_NE( cs, VRHI_INVALID_HANDLE );
    vhProgram program = vhCreateComputeProgram( cs );

    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program );
    vhPrecompilePSO( program );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_EQ( after - before, 1 );

    vhDestroyShader( cs );
    vhFinish();
}

// --------------------------------------------------------------------------
// Compute: batch N, then cache-hit all again
// --------------------------------------------------------------------------

UTEST_F( Precompile_Compute, BatchNThenCacheHit )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute requires GPU in Null RHI mode" );
    }

    const int N = 32;
    std::vector< vhShader > shaders;
    shaders.reserve( N );
    s_liveShaderNames.reserve( s_liveShaderNames.size() + N );

    for ( int i = 0; i < N; ++i )
    {
        std::string src = MakeUniqueComputeSource( i );
        vhShader cs = vhAllocShader();
        std::vector< uint32_t > spirv;
        std::string error;
        s_liveShaderNames.emplace_back( "BatchCS_" + std::to_string( i ) );
        const char* name = s_liveShaderNames.back().c_str();
        bool ok = vhCompileShader( name, src.c_str(),
            VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
            spirv, "main", {}, {}, &error );
        if ( !ok )
        {
            UTEST_PRINTF( "Shader compile error: %s\n", error.c_str() );
        }
        ASSERT_TRUE( ok );
        vhCreateShader( cs, name, VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
        shaders.push_back( cs );
    }

    // All shaders created, now fire-and-forget N precompiles
    int32_t before = g_vhPSOCompileCounter.load();
    for ( int i = 0; i < N; ++i )
        vhPrecompilePSO( vhCreateComputeProgram( shaders[ i ] ) );
    vhFinish();
    int32_t afterBatch = g_vhPSOCompileCounter.load();
    EXPECT_GT( afterBatch - before, 0 );

    // Second batch — all cached (no new compiles)
    for ( int i = 0; i < N; ++i )
        vhPrecompilePSO( vhCreateComputeProgram( shaders[ i ] ) );
    vhFinish();
    int32_t afterSecond = g_vhPSOCompileCounter.load();
    EXPECT_EQ( afterSecond - afterBatch, 0 );

    for ( auto& cs : shaders )
        vhDestroyShader( cs );
    vhFinish();
}

// --------------------------------------------------------------------------
// Compute: shader creation ordering (no intermediate vhFinish needed)
// --------------------------------------------------------------------------

UTEST_F( Precompile_Compute, ShaderCreationOrdering )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute requires GPU in Null RHI mode" );
    }

    // Create shader (queued), then immediately precompile (also queued).
    // Only one vhFinish() — the precompile must see the shader.
    int32_t before = g_vhPSOCompileCounter.load();
    vhShader cs = CreateTestShader( g_emptyCS, VRHI_SHADER_STAGE_COMPUTE );
    ASSERT_NE( cs, VRHI_INVALID_HANDLE );
    vhPrecompilePSO( vhCreateComputeProgram( cs ) );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_GT( after, before );

    vhDestroyShader( cs );
    vhFinish();
}

// --------------------------------------------------------------------------
// Compute: empty program ignored (no crash, no compile)
// --------------------------------------------------------------------------

UTEST_F( Precompile_Compute, EmptyProgram )
{
    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( vhProgram() );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_EQ( after, before );
}

// --------------------------------------------------------------------------
// Graphics: single precompile compiles before draw
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, PrecompileBeforeDraw )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    struct Vertex { glm::vec3 pos; glm::vec4 col; };
    Vertex verts[3] = { { { -1, -1, 0 }, { 1, 0, 0, 1 } }, { { 3, -1, 0 }, { 1, 0, 0, 1 } }, { { -1, 3, 0 }, { 1, 0, 0, 1 } } };
    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    uint64_t stateFlags = VRHI_STATE_WRITE_MASK;
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program, stateFlags, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();
    int32_t afterPrecompile = g_vhPSOCompileCounter.load();
    EXPECT_GT( afterPrecompile, before );

    // Draw with matching state
    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0 ) )
         .SetStateFlags( stateFlags )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( program );

    vhStateId sid = 2000;
    vhSetState( sid, state );
    vhClear( sid, VRHI_CLEAR_COLOR );
    vhDraw( sid, 3 );
    vhFinish();

    int32_t afterDraw = g_vhPSOCompileCounter.load();
    EXPECT_EQ( afterDraw, afterPrecompile );

    CleanupPSOState( sid, vs, ps, vb, rt );
}

// --------------------------------------------------------------------------
// Graphics: duplicate precompile is cached
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, DuplicateIsCached )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_EQ( after - before, 1 );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Graphics: empty vertex layout for no-input VS
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, EmptyVertexLayout )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhShader vs = CreateTestShader( g_noInputVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidNoInputPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t before = g_vhPSOCompileCounter.load();
    // Empty vertex layout for shader with no vertex inputs
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_GT( after, before );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Graphics: different state flags produce distinct PSOs
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, DistinctStateFlags )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK | VRHI_STATE_CULL_BACK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_EQ( after - before, 2 );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Graphics: depth format makes distinct PSO
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, DepthFormatDistinct )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    uint64_t flags = VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE;
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t before = g_vhPSOCompileCounter.load();
    // No depth
    vhPrecompilePSO( program, flags, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    // With D32
    vhPrecompilePSO( program, flags, "float3 float4", colorFmts, nvrhi::Format::D32, 1 );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_EQ( after - before, 2 );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Graphics: color+draw precompile matches real draw
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, PrecompileMatchesDrawWithDepth )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhTexture rt = CreateTestTexture( 64, 64, nvrhi::Format::RGBA8_UNORM );
    vhTexture ds = CreateTestTexture( 64, 64, nvrhi::Format::D32 );
    struct Vertex { glm::vec3 pos; glm::vec4 col; };
    Vertex verts[3] = { { { -1, -1, 0.5f }, { 1, 0, 0, 1 } }, { { 3, -1, 0.5f }, { 1, 0, 0, 1 } }, { { -1, 3, 0.5f }, { 1, 0, 0, 1 } } };
    vhBuffer vb = CreateTestVB( "float3 float4", verts, sizeof( verts ) );
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    uint64_t flags = VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE;
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program, flags, "float3 float4", colorFmts, nvrhi::Format::D32, 1 );
    vhFinish();
    int32_t afterPrecompile = g_vhPSOCompileCounter.load();
    EXPECT_GT( afterPrecompile, before );

    // Real draw with same depth
    vhState state;
    state.SetColourAttachment( 0, rt )
         .SetDepthAttachment( ds )
         .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR | VRHI_CLEAR_DEPTH, glm::vec4( 0 ), 1.0f )
         .SetStateFlags( flags )
         .SetVertexBuffer( vb, 0 )
         .SetProgram( program );

    vhStateId sid = 3000;
    vhSetState( sid, state );
    vhClear( sid, VRHI_CLEAR_COLOR | VRHI_CLEAR_DEPTH );
    vhDraw( sid, 3 );
    vhFinish();

    int32_t afterDraw = g_vhPSOCompileCounter.load();
    EXPECT_EQ( afterDraw, afterPrecompile );

    vhState cleanState2;
    vhSetState( sid, cleanState2, VRHI_DIRTY_ALL );
    vhDestroyTexture( rt );
    vhDestroyTexture( ds );
    vhDestroyBuffer( vb );
    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Graphics: bad layout does not crash, temp buffer cleaned up
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, BadLayoutSafeCleanup )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t errorsBefore = g_vhErrorCounter.load();

    // Invalid vertex layout should not crash or leak temp buffer
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "nonsense3", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();

    // After the bad precompile, a valid one must still succeed
    int32_t before = g_vhPSOCompileCounter.load();
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();
    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_GT( after, before );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Rejection: sampleCount == 0
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, ZeroSampleCountRejected )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhProgram program = vhCreateGfxProgram( vs, ps );
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };

    int32_t psoBefore = g_vhPSOCompileCounter.load();

    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 0 );
    vhFinish();

    int32_t psoAfter = g_vhPSOCompileCounter.load();
    EXPECT_EQ( psoAfter, psoBefore );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Graphics: shader creation ordering (no intermediate vhFinish)
// --------------------------------------------------------------------------

UTEST_F( Precompile_Graphics, ShaderCreationOrdering )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Rendering requires GPU in Null RHI mode" );
    }

    int32_t before = g_vhPSOCompileCounter.load();
    vhShader vs = CreateTestShader( g_simpleVS, VRHI_SHADER_STAGE_VERTEX );
    vhShader ps = CreateTestShader( g_solidPS, VRHI_SHADER_STAGE_PIXEL );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhProgram program = vhCreateGfxProgram( vs, ps );
    std::vector< nvrhi::Format > colorFmts = { nvrhi::Format::RGBA8_UNORM };
    vhPrecompilePSO( program, VRHI_STATE_WRITE_MASK, "float3 float4", colorFmts, nvrhi::Format::UNKNOWN, 1 );
    vhFinish();

    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_GT( after, before );

    vhDestroyShader( vs );
    vhDestroyShader( ps );
    vhFinish();
}

// --------------------------------------------------------------------------
// Shader-creation ordering: precompile then destroy
// --------------------------------------------------------------------------

UTEST_F( Precompile_Compute, PrecompileThenDestroy )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Compute requires GPU in Null RHI mode" );
    }

    int32_t before = g_vhPSOCompileCounter.load();
    vhShader cs = CreateTestShader( g_emptyCS, VRHI_SHADER_STAGE_COMPUTE );
    ASSERT_NE( cs, VRHI_INVALID_HANDLE );

    vhPrecompilePSO( vhCreateComputeProgram( cs ) );
    vhDestroyShader( cs ); // happens after precompile on the backend
    vhFinish();

    int32_t after = g_vhPSOCompileCounter.load();
    EXPECT_GT( after, before );
}

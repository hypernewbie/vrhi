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

#include "test.h"
#include <vrhi.h>
#include <vector>
#include <string>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern bool g_captureActive;

// --------------------------------------------------------------------------
// Shaders
// --------------------------------------------------------------------------

static const char* g_rayGenHLSL = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);

struct RayPayload {
    float4 color;
};

[shader("raygeneration")]
void main()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchSize = DispatchRaysDimensions().xy;

    float2 uv = (float2(launchIndex) + 0.5f) / float2(launchSize);
    
    RayDesc ray;
    ray.Origin = float3(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, -1.0f);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    RayPayload payload;
    payload.color = float4(0, 0, 0, 0);

    TraceRay(g_Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    g_Output[launchIndex] = payload.color;
}
)";

static const char* g_missHLSL = R"(
struct RayPayload {
    float4 color;
};

[shader("miss")]
void main(inout RayPayload payload)
{
    payload.color = float4(0.0, 0.0, 1.0, 1.0); // Blue
}
)";

static const char* g_hitHLSL = R"(
struct RayPayload {
    float4 color;
};

[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.color = float4(1.0, 0.0, 0.0, 1.0); // Red
}
)";

static const char* g_inlineRT_CS = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);

[numthreads(4, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 launchSize = uint2(4, 4);
    float2 uv = (float2(id.xy) + 0.5f) / float2(launchSize);

    RayDesc ray;
    ray.Origin = float3(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, -1.0f);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        g_Output[id.xy] = float4(1.0, 0.0, 0.0, 1.0); // Red
    else
        g_Output[id.xy] = float4(0.0, 0.0, 1.0, 1.0); // Blue
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

static vhShader CreateRTShader( const char* source, uint64_t stage )
{
    vhShader shader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    // RT Shaders need SM 6.5+ usually, but VRHI_SHADER_SM_6_5 is the default if 0.
    bool ok = vhCompileShader( "RTShader", source, stage | VRHI_SHADER_SM_6_5, spirv, "main", {}, {}, &error );
    if ( !ok )
    {
        UTEST_PRINTF( "Shader Compilation Error: %s\n", error.c_str() );
    }
    vhCreateShader( shader, "RTShader", stage, spirv );
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

    bool match = true;
    match &= ( AbsDiff( r, er ) <= tolerance );
    match &= ( AbsDiff( g, eg ) <= tolerance );
    match &= ( AbsDiff( b, eb ) <= tolerance );
    match &= ( AbsDiff( a, ea ) <= tolerance );

    if ( !match )
    {
        UTEST_PRINTF( "VerifyPixel Failed at (%d, %d):\n", x, y );
        UTEST_PRINTF( "  Expected: RGBA(%3d, %3d, %3d, %3d) [0x%08X]\n", er, eg, eb, ea, expectedRGBA );
        UTEST_PRINTF( "  Actual:   RGBA(%3d, %3d, %3d, %3d)\n", r, g, b, a );
        return false;
    }

    return true;
}

// --------------------------------------------------------------------------
// Fixture
// --------------------------------------------------------------------------

struct RT {};

UTEST_F_SETUP( RT )
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

UTEST_F_TEARDOWN( RT )
{
    vhEndMarker();
}

// --------------------------------------------------------------------------
// Tests
// --------------------------------------------------------------------------

UTEST_F( RT, Basic )
{
    if ( !g_vhInit.raytracing ) return;

    // Create Resources
    // Output UAV
    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // Triangle VB
    struct Vertex { float x, y, z; };
    Vertex triangle[] = {
        { -1.0f, -1.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f }
    };
    vhBuffer vb = CreateTestVB( "POSITION:float3", triangle, sizeof( triangle ) );

    // Build Acceleration Structures
    vhAccelStruct blas = vhAllocAS();
    nvrhi::rt::GeometryDesc geo;
    geo.geometryType = nvrhi::rt::GeometryType::Triangles;
    geo.geometryData.triangles.vertexBuffer = vhGetBufferNvrhiHandle( vb );
    geo.geometryData.triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    geo.geometryData.triangles.vertexStride = sizeof( Vertex );
    geo.geometryData.triangles.vertexCount = 3;
    geo.flags = nvrhi::rt::GeometryFlags::Opaque;
    vhCreateAS( blas, nvrhi::rt::AccelStructDesc().setIsTopLevel( false ).setDebugName( "TestBLAS" ) );
    vhBuildBLAS( blas, { geo } );

    vhAccelStruct tlas = vhAllocAS();
    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setDebugName( "TestTLAS" ) );
    vhBuildTLAS( tlas, { inst } );

    // Setup Shaders and Pipeline
    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc; mDesc.shader = vhGetShaderNvrhiHandle( miss ); mDesc.exportName = "main";
    
    nvrhi::rt::PipelineHitGroupDesc hgDesc;
    hgDesc.exportName = "hg_main";
    hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );

    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;
    
    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );

    // Create Shader Table
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "main" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    // Dispatch
    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args;
    args.width = 4;
    args.height = 4;
    
    vhStateId sid = 1500;
    vhSetState( sid, state );
    vhDispatchRays( sid, table, args );
    vhFinish();

    // Reset state to avoid polluting other tests
    vhState reset;
    vhSetState( sid, reset.DirtyAll() );

    // Verify
    // Triangle covers (-1, -1) to (1, 1) in our ray origin space.
    // Pixel (0,0) center is (0.5/4 * 2 - 1, 0.5/4 * 2 - 1) = (0.125 * 2 - 1, ...) = (-0.75, -0.75) -> Hit (Red)
    // Pixel (3,3) center is (3.5/4 * 2 - 1, 3.5/4 * 2 - 1) = (0.875 * 2 - 1, ...) = (0.75, 0.75) -> Miss (Blue) - actually diagonal triangle
    
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) ); // Red
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF0000 ) ); // Blue

    // Cleanup
    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( blas );
    vhDestroyAS( tlas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, Inline )
{
    if ( !g_vhInit.raytracing ) return;

    // Create Resources
    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    struct Vertex { float x, y, z; };
    Vertex triangle[] = {
        { -1.0f, -1.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f }
    };
    vhBuffer vb = CreateTestVB( "POSITION:float3", triangle, sizeof( triangle ) );

    // Build Acceleration Structures
    vhAccelStruct blas = vhAllocAS();
    nvrhi::rt::GeometryDesc geo;
    geo.geometryType = nvrhi::rt::GeometryType::Triangles;
    geo.geometryData.triangles.vertexBuffer = vhGetBufferNvrhiHandle( vb );
    geo.geometryData.triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    geo.geometryData.triangles.vertexStride = sizeof( Vertex );
    geo.geometryData.triangles.vertexCount = 3;
    geo.flags = nvrhi::rt::GeometryFlags::Opaque;
    vhCreateAS( blas, nvrhi::rt::AccelStructDesc().setIsTopLevel( false ) );
    vhBuildBLAS( blas, { geo } );

    vhAccelStruct tlas = vhAllocAS();
    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ) );
    vhBuildTLAS( tlas, { inst } );

    // Setup Shaders
    vhShader computeShader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    // Inline RT needs SM 6.5
    bool ok = vhCompileShader( "CS_Inline", g_inlineRT_CS, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_5, spirv, "main", {}, {}, &error );
    if ( !ok )
    {
        UTEST_PRINTF( "Shader Compilation Error: %s\n", error.c_str() );
    }
    vhCreateShader( computeShader, "CS_Inline", VRHI_SHADER_STAGE_COMPUTE, spirv );

    // Dispatch
    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateComputeProgram( computeShader ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    vhStateId sid = 1501;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } ); // 4x4 threads in groups
    vhFinish();

    // Reset state to avoid polluting other tests
    vhState reset;
    vhSetState( sid, reset.DirtyAll() );

    // Verify (same as above)
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) ); // Red
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF0000 ) ); // Blue

    // Cleanup
    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( blas );
    vhDestroyAS( tlas );
    vhDestroyShader( computeShader );
}

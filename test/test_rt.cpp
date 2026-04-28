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
#include <cstring>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern bool g_captureActive;

// --------------------------------------------------------------------------
// Shaders
// --------------------------------------------------------------------------

static const char* g_rayGenHLSL = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);

struct RayPayload { float4 color; };

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
struct RayPayload { float4 color; };
[shader("miss")]
void main(inout RayPayload payload) { payload.color = float4(0.0, 0.0, 1.0, 1.0); }
)";

static const char* g_missBlackHLSL = R"(
struct RayPayload { float4 color; };
[shader("miss")]
void main(inout RayPayload payload) { payload.color = float4(0.0, 0.0, 0.0, 1.0); }
)";

static const char* g_missMagentaHLSL = R"(
struct RayPayload { float4 color; };
[shader("miss")]
void main(inout RayPayload payload) { payload.color = float4(1.0, 0.0, 1.0, 1.0); }
)";

static const char* g_hitHLSL = R"(
struct RayPayload { float4 color; };
[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) { payload.color = float4(1.0, 0.0, 0.0, 1.0); }
)";

static const char* g_hitGreenHLSL = R"(
struct RayPayload { float4 color; };
[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) { payload.color = float4(0.0, 1.0, 0.0, 1.0); }
)";

static const char* g_hitWhiteHLSL = R"(
struct RayPayload { float4 color; };
[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) { payload.color = float4(1.0, 1.0, 1.0, 1.0); }
)";

static const char* g_anyHitAcceptHLSL = R"(
struct RayPayload { float4 color; };
[shader("anyhit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) { AcceptHitAndEndSearch(); }
)";

static const char* g_anyHitRejectHLSL = R"(
struct RayPayload { float4 color; };
cbuffer g_AnyHitParams : register(b0, VRHI_STAGE_SPACE) { uint g_Reject; };
[shader("anyhit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    if (g_Reject != 0) { IgnoreHit(); }
    else { AcceptHitAndEndSearch(); }
}
)";

static const char* g_anyHitSetGreenHLSL = R"(
struct RayPayload { float4 color; };
[shader("anyhit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.color = float4(0.0, 1.0, 0.0, 1.0);
    AcceptHitAndEndSearch();
}
)";

static const char* g_intersectionAABB_HLSL = R"(
struct CustomAABBAttributes { float4 color; };
[shader("intersection")]
void main()
{
    CustomAABBAttributes attr;
    attr.color = float4(0.0, 1.0, 0.0, 1.0);
    ReportHit(0.5f, 0, attr);
}
)";

static const char* g_closestHitFromAABB_HLSL = R"(
struct RayPayload { float4 color; };
struct CustomAABBAttributes { float4 color; };
[shader("closesthit")]
void main(inout RayPayload payload, in CustomAABBAttributes attr) { payload.color = attr.color; }
)";

static const char* g_closestHitRecurseHLSL = R"(
struct RayPayload { float4 color; };
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);
[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    RayDesc ray;
    ray.Origin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent() + float3(0.0, 0.1, 0.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    payload.color = float4(0, 0, 0, 0);
    TraceRay(g_Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 1, ray, payload);
}
)";

static const char* g_hitWriteInstanceID = R"(
struct RayPayload { float4 color; };
[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float r = float(InstanceID() & 1);
    float g = float((InstanceID() >> 1) & 1);
    payload.color = float4(r, g, 0.0, 1.0);
}
)";

static const char* g_rayGenWithFlags = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);
cbuffer g_RayFlags : register(b0, VRHI_STAGE_SPACE) { uint g_TraceFlags; };
struct RayPayload { float4 color; };
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
    TraceRay(g_Scene, g_TraceFlags, 0xFF, 0, 0, 0, ray, payload);
    g_Output[launchIndex] = payload.color;
}
)";

static const char* g_rayGenTwoScene = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_SceneA : register(t0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_SceneB : register(t1, VRHI_STAGE_SPACE);
struct RayPayload { float4 color; };
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
    RayPayload payloadA, payloadB;
    payloadA.color = float4(0, 0, 0, 0);
    payloadB.color = float4(0, 0, 0, 0);
    TraceRay(g_SceneA, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payloadA);
    TraceRay(g_SceneB, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payloadB);
    g_Output[launchIndex] = payloadA.color + payloadB.color;
}
)";

static const char* g_rayGenWithExtensionUAV = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);
RWTexture2D<float4> g_ExtUAV : register(u0, VRHI_STAGE_EXT_SPACE);
struct RayPayload { float4 color; };
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
    g_ExtUAV[launchIndex] = float4(1.0, 0.5, 0.0, 1.0);
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
        g_Output[id.xy] = float4(1.0, 0.0, 0.0, 1.0);
    else
        g_Output[id.xy] = float4(0.0, 0.0, 1.0, 1.0);
}
)";

static const char* g_inlineRTAllStatus_CS = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);
[numthreads(4, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 result = float4(0, 0, 0, 1);
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
    uint status = q.CommittedStatus();
    result.r = (status == COMMITTED_TRIANGLE_HIT) ? 1.0 : 0.0;
    result.g = (status == COMMITTED_NOTHING) ? 1.0 : 0.0;
    result.b = q.CommittedRayT() > 0.0 ? 0.5 : 0.0;
    g_Output[id.xy] = result;
}
)";

static const char* g_inlineRTNegZ_CS = R"(
RWTexture2D<float4> g_Output : register(u0, VRHI_STAGE_SPACE);
RaytracingAccelerationStructure g_Scene : register(t0, VRHI_STAGE_SPACE);
[numthreads(4, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 launchSize = uint2(4, 4);
    float2 uv = (float2(id.xy) + 0.5f) / float2(launchSize);
    RayDesc ray;
    ray.Origin = float3(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, 1.0f);
    ray.Direction = float3(0, 0, -1);
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(g_Scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);
    q.Proceed();
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        g_Output[id.xy] = float4(1.0, 0.0, 0.0, 1.0);
    else
        g_Output[id.xy] = float4(0.0, 0.0, 1.0, 1.0);
}
)";

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static vhTexture CreateTestTexture( int32_t w, int32_t h, nvrhi::Format format, uint64_t flags = VRHI_TEXTURE_RT )
{
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "RTTestTexture", glm::ivec2( w, h ), 1, format, flags );
    return tex;
}

static vhBuffer CreateTestVB( const char* layout, const void* data, uint32_t size, uint16_t flags = VRHI_BUFFER_ACCEL_INPUT )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    memcpy( mem->data(), data, size );
    vhCreateVertexBuffer( buf, "TestVB", mem, layout, 0, flags );
    return buf;
}

static vhBuffer CreateTestIB( const void* data, uint32_t size, uint16_t flags = VRHI_BUFFER_ACCEL_INPUT )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    memcpy( mem->data(), data, size );
    vhCreateIndexBuffer( buf, "TestIB", mem, 0, flags );
    return buf;
}

static vhBuffer CreateTestStorageBuffer( const void* data, uint64_t size, uint32_t stride = 0, uint32_t extraFlags = 0 )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    memcpy( mem->data(), data, size );
    vhCreateStorageBuffer( buf, "TestSB", mem, size, VRHI_BUFFER_COMPUTE_READ_WRITE | extraFlags, stride );
    return buf;
}

static vhBuffer CreateTestCB( const void* data, uint64_t size )
{
    vhBuffer buf = vhAllocBuffer();
    vhMem* mem = vhAllocMem( size );
    if ( data ) memcpy( mem->data(), data, size );
    vhCreateUniformBuffer( buf, "TestCB", mem, size );
    return buf;
}

static vhShader CreateRTShader( const char* source, uint64_t stage )
{
    vhShader shader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( "RTShader", source, stage | VRHI_SHADER_SM_6_5, spirv, "main", {}, {}, &error );
    if ( !ok )
    {
        UTEST_PRINTF( "Shader Compilation Error: %s\n", error.c_str() );
    }
    vhCreateShader( shader, "RTShader", stage, spirv );
    // Tests query the nvrhi shader handle synchronously when building RT pipelines, so the
    // backend must have processed the create command before we return.
    vhFlush( true );
    return shader;
}

static vhShader CreateComputeShaderRT( const char* source )
{
    vhShader shader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( "CS", source, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_5, spirv, "main", {}, {}, &error );
    if ( !ok )
    {
        UTEST_PRINTF( "Shader Compilation Error: %s\n", error.c_str() );
    }
    vhCreateShader( shader, "CS", VRHI_SHADER_STAGE_COMPUTE, spirv );
    vhFlush( true );
    return shader;
}

static bool VerifyPixel( vhTexture rt, int32_t x, int32_t y, uint32_t expectedRGBA, uint32_t tolerance = 2 )
{
    vhMem readData;
    vhReadTextureSlow( rt, 0, 0, &readData );
    vhFinish();

    if ( g_vhInit.nullMode ) return true;

    vhTexInfo info = vhGetTextureInfo( rt );
    if ( readData.size() == 0 ) return false;

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

    if ( AbsDiff( r, er ) > tolerance || AbsDiff( g, eg ) > tolerance ||
         AbsDiff( b, eb ) > tolerance || AbsDiff( a, ea ) > tolerance )
    {
        UTEST_PRINTF( "VerifyPixel Failed at (%d, %d):\n", x, y );
        UTEST_PRINTF( "  Expected: RGBA(%3d, %3d, %3d, %3d) [0x%08X]\n", er, eg, eb, ea, expectedRGBA );
        UTEST_PRINTF( "  Actual:   RGBA(%3d, %3d, %3d, %3d)\n", r, g, b, a );
        return false;
    }

    return true;
}

static bool VerifyAllPixels( vhTexture rt, uint32_t expectedRGBA, uint32_t tolerance = 2 )
{
    vhMem readData;
    vhReadTextureSlow( rt, 0, 0, &readData );
    vhFinish();

    if ( g_vhInit.nullMode ) return true;

    vhTexInfo info = vhGetTextureInfo( rt );
    if ( readData.size() == 0 ) return false;

    uint8_t er = ( expectedRGBA >> 0 ) & 0xFF;
    uint8_t eg = ( expectedRGBA >> 8 ) & 0xFF;
    uint8_t eb = ( expectedRGBA >> 16 ) & 0xFF;
    uint8_t ea = ( expectedRGBA >> 24 ) & 0xFF;

    auto AbsDiff = []( uint8_t a, uint8_t b ) -> uint8_t { return a > b ? a - b : b - a; };

    for ( int32_t y = 0; y < info.dimensions.y; ++y )
    {
        for ( int32_t x = 0; x < info.dimensions.x; ++x )
        {
            int32_t offset = ( y * info.dimensions.x + x ) * 4;
            if ( AbsDiff( readData[offset + 0], er ) > tolerance ||
                 AbsDiff( readData[offset + 1], eg ) > tolerance ||
                 AbsDiff( readData[offset + 2], eb ) > tolerance ||
                 AbsDiff( readData[offset + 3], ea ) > tolerance )
            {
                UTEST_PRINTF( "VerifyAllPixels Failed at (%d, %d): expected 0x%08X, got RGBA(%3d,%3d,%3d,%3d)\n",
                    x, y, expectedRGBA, readData[offset + 0], readData[offset + 1], readData[offset + 2], readData[offset + 3] );
                return false;
            }
        }
    }

    return true;
}

struct Vertex { float x, y, z; };

static Vertex kTriVertices[] = {
    { -1.0f, -1.0f, 0.0f },
    {  1.0f, -1.0f, 0.0f },
    { -1.0f,  1.0f, 0.0f }
};

static Vertex kTriVerticesCCW[] = {
    { -1.0f,  1.0f, 0.0f },
    {  1.0f, -1.0f, 0.0f },
    { -1.0f, -1.0f, 0.0f }
};

static nvrhi::rt::GeometryDesc MakeTriGeo( vhBuffer vb, uint32_t vertexCount, nvrhi::rt::GeometryFlags flags = nvrhi::rt::GeometryFlags::Opaque )
{
    nvrhi::rt::GeometryDesc geo;
    geo.geometryType = nvrhi::rt::GeometryType::Triangles;
    geo.geometryData.triangles.vertexBuffer = vhGetBufferNvrhiHandle( vb );
    geo.geometryData.triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    geo.geometryData.triangles.vertexStride = sizeof( Vertex );
    geo.geometryData.triangles.vertexCount = vertexCount;
    geo.flags = flags;
    return geo;
}

static nvrhi::rt::GeometryDesc MakeTriIndexedGeo( vhBuffer vb, vhBuffer ib, uint32_t vertexCount, uint32_t indexCount, nvrhi::rt::GeometryFlags flags = nvrhi::rt::GeometryFlags::Opaque )
{
    nvrhi::rt::GeometryDesc geo;
    geo.geometryType = nvrhi::rt::GeometryType::Triangles;
    geo.geometryData.triangles.vertexBuffer = vhGetBufferNvrhiHandle( vb );
    geo.geometryData.triangles.indexBuffer = vhGetBufferNvrhiHandle( ib );
    geo.geometryData.triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    geo.geometryData.triangles.indexFormat = nvrhi::Format::R16_UINT;
    geo.geometryData.triangles.vertexStride = sizeof( Vertex );
    geo.geometryData.triangles.vertexCount = vertexCount;
    geo.geometryData.triangles.indexCount = indexCount;
    geo.flags = flags;
    return geo;
}

static nvrhi::rt::GeometryDesc MakeAABBGeo( vhBuffer buf, uint32_t count )
{
    // Value-initialise to zero all bytes including union padding between Format fields
    // (Format is uint8_t so the 6 pad bytes before the next uint64_t would otherwise
    // carry stack garbage that NVRHI validation reads as aabbs.offset).
    nvrhi::rt::GeometryDesc geo = {};
    geo.geometryType = nvrhi::rt::GeometryType::AABBs;
    geo.geometryData.aabbs.buffer = vhGetBufferNvrhiHandle( buf );
    geo.geometryData.aabbs.offset = 0;
    geo.geometryData.aabbs.count = count;
    geo.geometryData.aabbs.stride = sizeof( float ) * 6;
    geo.flags = nvrhi::rt::GeometryFlags::Opaque;
    return geo;
}

static vhAccelStruct BuildTriBLAS( vhBuffer vb, uint32_t vertexCount, nvrhi::rt::GeometryFlags flags = nvrhi::rt::GeometryFlags::Opaque, nvrhi::rt::AccelStructBuildFlags buildFlags = nvrhi::rt::AccelStructBuildFlags::None )
{
    vhAccelStruct blas = vhAllocAS();
    vhCreateAS( blas, nvrhi::rt::AccelStructDesc().setIsTopLevel( false ).setBuildFlags( buildFlags ).setDebugName( "TestBLAS" ) );
    // Backend must have created the underlying nvrhi handles before we can read them via getter APIs.
    vhFlush( true );
    nvrhi::rt::GeometryDesc geo = MakeTriGeo( vb, vertexCount, flags );
    vhBuildBLAS( blas, { geo } );
    return blas;
}

static vhAccelStruct BuildTriIndexedBLAS( vhBuffer vb, vhBuffer ib, uint32_t vertexCount, uint32_t indexCount, nvrhi::rt::GeometryFlags flags = nvrhi::rt::GeometryFlags::Opaque )
{
    vhAccelStruct blas = vhAllocAS();
    vhCreateAS( blas, nvrhi::rt::AccelStructDesc().setIsTopLevel( false ).setDebugName( "TestBLAS" ) );
    vhFlush( true );
    nvrhi::rt::GeometryDesc geo = MakeTriIndexedGeo( vb, ib, vertexCount, indexCount, flags );
    vhBuildBLAS( blas, { geo } );
    return blas;
}

static vhAccelStruct BuildAABBBLAS( vhBuffer buf, uint32_t aabbCount )
{
    vhAccelStruct blas = vhAllocAS();
    vhCreateAS( blas, nvrhi::rt::AccelStructDesc().setIsTopLevel( false ).setDebugName( "TestBLAS" ) );
    vhFlush( true );
    nvrhi::rt::GeometryDesc geo = MakeAABBGeo( buf, aabbCount );
    vhBuildBLAS( blas, { geo } );
    return blas;
}

static vhAccelStruct BuildTriTLAS( vhAccelStruct blas, nvrhi::rt::AccelStructBuildFlags buildFlags = nvrhi::rt::AccelStructBuildFlags::None )
{
    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setBuildFlags( buildFlags ).setDebugName( "TestTLAS" ) );
    // Need the BLAS nvrhi handle, plus we want the BLAS build to have completed.
    vhFinish();
    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhBuildTLAS( tlas, { inst } );
    vhFinish();
    return tlas;
}

static void DispatchAndReset( vhStateId sid, vhState& state, vhShaderTable table, const nvrhi::rt::DispatchRaysArguments& args )
{
    vhSetState( sid, state );
    vhDispatchRays( sid, table, args );
    vhFinish();
    vhState reset;
    vhSetState( sid, reset.DirtyAll() );
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

// ==========================================================================
// Phase 1 — Foundational
// ==========================================================================

UTEST_F( RT, Basic )
{

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1500, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, Inline )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader cs = CreateComputeShaderRT( g_inlineRT_CS );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateComputeProgram( cs ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    vhStateId sid = 1501;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();
    vhState reset; vhSetState( sid, reset.DirtyAll() );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( cs );
}

UTEST_F( RT, AnyHit )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::None );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader chit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );
    vhShader anyHit = CreateRTShader( g_anyHitAcceptHLSL, VRHI_SHADER_STAGE_ANY_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main";
    hgDesc.closestHitShader = vhGetShaderNvrhiHandle( chit );
    hgDesc.anyHitShader = vhGetShaderNvrhiHandle( anyHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, chit, anyHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1502, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( chit );
    vhDestroyShader( anyHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, AnyHitReject )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::None );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader chit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );
    vhShader anyHit = CreateRTShader( g_anyHitRejectHLSL, VRHI_SHADER_STAGE_ANY_HIT );

    uint32_t reject = 1;
    vhBuffer cb = CreateTestCB( &reject, sizeof( reject ) );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main";
    hgDesc.closestHitShader = vhGetShaderNvrhiHandle( chit );
    hgDesc.anyHitShader = vhGetShaderNvrhiHandle( anyHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, chit, anyHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetBuffer( 0, { .name = "g_AnyHitParams", .buffer = cb } )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1503, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( cb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( chit );
    vhDestroyShader( anyHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, ProceduralAABB )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    // AABB covering (-1,-1,0) to (1,1,0)
    float aabb[6] = { -1.0f, -1.0f, -0.1f, 1.0f, 1.0f, 0.1f };
    vhBuffer buf = CreateTestStorageBuffer( aabb, sizeof( aabb ), 0, VRHI_BUFFER_ACCEL_INPUT );
    vhAccelStruct blas = BuildAABBBLAS( buf, 1 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen    = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss      = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader chit      = CreateRTShader( g_closestHitFromAABB_HLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );
    vhShader isect     = CreateRTShader( g_intersectionAABB_HLSL, VRHI_SHADER_STAGE_INTERSECTION );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main";
    hgDesc.closestHitShader = vhGetShaderNvrhiHandle( chit );
    hgDesc.intersectionShader = vhGetShaderNvrhiHandle( isect );
    hgDesc.isProceduralPrimitive = true;
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;
    pipeDesc.maxAttributeSize = sizeof( float ) * 4; // CustomAABBAttributes = float4 = 16 bytes

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, chit, VRHI_INVALID_HANDLE, isect ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1504, state, table, args );

    // The AABB covers the full [-1,1]x[-1,1] XY range so all pixels hit.
    // Intersection shader writes attr.color = green (0,1,0,1). ClosestHit copies it
    // to payload. Green = R=0,G=255,B=0,A=255 = 0xFF00FF00 in VerifyPixel packed format.
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF00FF00 ) );
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFF00FF00 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( buf );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( chit );
    vhDestroyShader( isect );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, MultiHitGroup )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );
    vhBuffer vb2 = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas0 = BuildTriBLAS( vb, 3 );
    vhAccelStruct blas1 = BuildTriBLAS( vb2, 3 );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 2 ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc inst0;
    inst0.bottomLevelAS = vhGetASNvrhiHandle( blas0 );
    inst0.instanceMask = 0xFF;
    inst0.instanceContributionToHitGroupIndex = 0;
    inst0.flags = nvrhi::rt::InstanceFlags::None;
    inst0.setTransform( nvrhi::rt::c_IdentityTransform );

    nvrhi::rt::InstanceDesc inst1;
    inst1.bottomLevelAS = vhGetASNvrhiHandle( blas1 );
    inst1.instanceMask = 0xFF;
    inst1.instanceContributionToHitGroupIndex = 1;
    inst1.flags = nvrhi::rt::InstanceFlags::None;
    inst1.setTransform( nvrhi::rt::c_IdentityTransform );

    vhBuildTLAS( tlas, { inst0, inst1 } );

    vhShader rayGen  = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss    = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader chitRed = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );
    vhShader chitGrn = CreateRTShader( g_hitGreenHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hg0; hg0.exportName = "hg0"; hg0.closestHitShader = vhGetShaderNvrhiHandle( chitRed );
    nvrhi::rt::PipelineHitGroupDesc hg1; hg1.exportName = "hg1"; hg1.closestHitShader = vhGetShaderNvrhiHandle( chitGrn );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hg0, hg1 };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg0" );
    vhShaderTableAddHitGroup( table, "hg1" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, chitRed ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1505, state, table, args );

    // All rays land on both instances; whichever is closer sets the colour.
    // Both instances have the same geometry, so result is the closest (both overlap fully).
    // With inst0 at hg0 (red), inst1 at hg1 (green), and overlap, we expect red (first/closest).
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( vb2 );
    vhDestroyAS( tlas );
    vhDestroyAS( blas0 );
    vhDestroyAS( blas1 );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( chitRed );
    vhDestroyShader( chitGrn );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, Recursion )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen          = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader missMagenta     = CreateRTShader( g_missMagentaHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader chitRecurse     = CreateRTShader( g_closestHitRecurseHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    vhShader missBlue = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc0; mDesc0.shader = vhGetShaderNvrhiHandle( missMagenta ); mDesc0.exportName = "miss_m";
    nvrhi::rt::PipelineShaderDesc mDesc1; mDesc1.shader = vhGetShaderNvrhiHandle( missBlue );    mDesc1.exportName = "miss_b";
    pipeDesc.shaders = { rgDesc, mDesc0, mDesc1 };

    nvrhi::rt::PipelineHitGroupDesc hg0; hg0.exportName = "hg_main"; hg0.closestHitShader = vhGetShaderNvrhiHandle( chitRecurse );
    pipeDesc.hitGroups = { hg0 };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;
    pipeDesc.maxRecursionDepth = 2;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss_m" );
    vhShaderTableAddMiss( table, "miss_b" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, missMagenta, chitRecurse ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1506, state, table, args );

    // Pixel (0,0): ray hits triangle, closesthit traces a recursive ray that misses using
    // miss index 1 (missBlue) -> payload = blue (0xFFFF0000).
    // Pixel (3,3): original ray misses immediately using miss index 0 (missMagenta) ->
    // payload = magenta (0xFFFF00FF).
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFFFF0000 ) );
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF00FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( missMagenta );
    vhDestroyShader( missBlue );
    vhDestroyShader( chitRecurse );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, IndexedTriangles )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );
    uint16_t indices[3] = { 0, 1, 2 };
    vhBuffer ib = CreateTestIB( indices, sizeof( indices ) );

    vhAccelStruct blas = BuildTriIndexedBLAS( vb, ib, 3, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1507, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( ib );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, InstanceTransform )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::AffineTransform xform;
    // Translate triangle far to +Y so all our rays miss it
    xform[0] = 1.f; xform[1] = 0.f; xform[2] = 0.f; xform[3] = 0.f;
    xform[4] = 0.f; xform[5] = 1.f; xform[6] = 0.f; xform[7] = 10.f;
    xform[8] = 0.f; xform[9] = 0.f; xform[10] = 1.f; xform[11] = 0.f;

    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( xform );
    vhBuildTLAS( tlas, { inst } );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1508, state, table, args );

    // All rays should miss because the triangle is translated +Y by 10
    EXPECT_TRUE( VerifyAllPixels( rt, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, InstanceMask )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0x01;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhBuildTLAS( tlas, { inst } );

    vhShader rayGen = CreateRTShader( g_rayGenWithFlags, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    uint32_t rayFlags = 0; // RAY_FLAG_NONE
    vhBuffer cbFlags = CreateTestCB( &rayFlags, sizeof( rayFlags ) );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    // Test 1: mask 0xFF in TraceRay matches instanceMask 0x01 => hit
    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetBuffer( 0, { .name = "g_RayFlags", .buffer = cbFlags } )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1509, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( cbFlags );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, Dispatch3D )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4; args.depth = 2;
    DispatchAndReset( 1510, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, TwoScene )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vbA = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );
    vhBuffer vbB = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blasA = BuildTriBLAS( vbA, 3 );
    vhAccelStruct blasB = BuildTriBLAS( vbB, 3 );
    vhAccelStruct tlasA = BuildTriTLAS( blasA );
    vhAccelStruct tlasB = BuildTriTLAS( blasB );

    vhShader rayGen = CreateRTShader( g_rayGenTwoScene, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlasA, -1, "g_SceneA" )
         .SetAccelStruct( 1, tlasB, -1, "g_SceneB" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 1511, state, table, args );

    // Both scenes hit red, sum = red+red = (2,0,0,2) clamped -> (2,0,0,1) or (255,0,0,255)
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vbA );
    vhDestroyBuffer( vbB );
    vhDestroyAS( tlasA );
    vhDestroyAS( tlasB );
    vhDestroyAS( blasA );
    vhDestroyAS( blasB );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

// ==========================================================================
// Phase 2 — Intermediate
// ==========================================================================

UTEST_F( RT, BuildFlagsPreferFastTrace )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::Opaque, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace );
    vhAccelStruct tlas = BuildTriTLAS( blas, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2000, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, BuildFlagsPreferFastBuild )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::Opaque, nvrhi::rt::AccelStructBuildFlags::PreferFastBuild );
    vhAccelStruct tlas = BuildTriTLAS( blas, nvrhi::rt::AccelStructBuildFlags::PreferFastBuild );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2001, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, ForceOpaqueInstance )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::None );
    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::ForceOpaque;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhBuildTLAS( tlas, { inst } );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2002, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, MinimizeMemory )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::Opaque, nvrhi::rt::AccelStructBuildFlags::MinimizeMemory );
    vhAccelStruct tlas = BuildTriTLAS( blas, nvrhi::rt::AccelStructBuildFlags::MinimizeMemory );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2003, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, TriangleCullDisable )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::TriangleCullDisable;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhBuildTLAS( tlas, { inst } );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missBlackHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2004, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, FrontCCW )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVerticesCCW, sizeof( kTriVerticesCCW ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::TriangleFrontCounterclockwise;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhBuildTLAS( tlas, { inst } );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2005, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, FullHitGroup )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::None );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader chit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );
    vhShader ahit = CreateRTShader( g_anyHitSetGreenHLSL, VRHI_SHADER_STAGE_ANY_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main";
    hgDesc.closestHitShader = vhGetShaderNvrhiHandle( chit );
    hgDesc.anyHitShader = vhGetShaderNvrhiHandle( ahit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, chit, ahit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2006, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( chit );
    vhDestroyShader( ahit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, GlobalBindingLayouts )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    // Build a matching globalBindingLayout: an empty set-0 placeholder plus the
    // rayGen layout (set 1: AS@200, UAV@400). visibility must be AllRayTracing for global layouts.
    nvrhi::BindingLayoutDesc emptyDesc;
    emptyDesc.visibility = nvrhi::ShaderType::AllRayTracing;
    nvrhi::BindingLayoutHandle emptyLayout = g_vhDevice->createBindingLayout( emptyDesc );

    // The visibility must exactly match the auto-reflected raygen shader layout
    // (ShaderType::RayGeneration, not AllRayTracing) so that vrhi's hash-based
    // PSO-layout-to-shader matching succeeds in BE_PreSubmitCommon_State.
    nvrhi::BindingLayoutDesc rgLayoutDesc;
    rgLayoutDesc.visibility = nvrhi::ShaderType::RayGeneration;
    rgLayoutDesc.bindingOffsets = { 0, 0, 0, 0 };
    rgLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::RayTracingAccelStruct( 200 ),
        nvrhi::BindingLayoutItem::Texture_UAV( 400 ),
    };
    nvrhi::BindingLayoutHandle rgLayout = g_vhDevice->createBindingLayout( rgLayoutDesc );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;
    pipeDesc.globalBindingLayouts = { emptyLayout, rgLayout };

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 2007, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

// ==========================================================================
// Phase 3 — Advanced / Edge Cases
// ==========================================================================

UTEST_F( RT, EmptyTLAS )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setDebugName( "EmptyTLAS" ) );
    vhBuildTLAS( tlas, {} );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 3000, state, table, args );

    EXPECT_TRUE( VerifyAllPixels( rt, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyAS( tlas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, RepeatedDispatch )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rtA = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhTexture rtB = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blasA = BuildTriBLAS( vb, 3 );
    vhAccelStruct blasB = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlasA = BuildTriTLAS( blasA );
    vhAccelStruct tlasB = BuildTriTLAS( blasB );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    vhStateId sid = 3001;

    vhState stateA;
    stateA.DirtyAll()
          .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
          .SetTexture( 0, { .name = "g_Output", .texture = rtA, .computeUAV = true } )
          .SetAccelStruct( 0, tlasA, -1, "g_Scene" )
          .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );
    DispatchAndReset( sid, stateA, table, args );

    vhState stateB;
    stateB.DirtyAll()
          .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
          .SetTexture( 0, { .name = "g_Output", .texture = rtB, .computeUAV = true } )
          .SetAccelStruct( 0, tlasB, -1, "g_Scene" )
          .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );
    DispatchAndReset( sid, stateB, table, args );

    EXPECT_TRUE( VerifyPixel( rtA, 0, 0, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rtB, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rtA );
    vhDestroyTexture( rtB );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlasA );
    vhDestroyAS( tlasB );
    vhDestroyAS( blasA );
    vhDestroyAS( blasB );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, StateAccelStructDirty )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhStateId sid = 3002;
    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;

    vhState state;
    state.SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" );
    state.DirtyAll();
    vhSetState( sid, state );
    vhDispatchRays( sid, table, args );
    vhFinish();

    vhState state2;
    state2.SetAccelStruct( 0, tlas, -1, "g_Scene" );
    state2.dirty = VRHI_DIRTY_ACCEL_STRUCT;
    state2.SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
          .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
          .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );
    vhSetState( sid, state2 );
    vhDispatchRays( sid, table, args );
    vhFinish();
    vhState reset; vhSetState( sid, reset.DirtyAll() );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, ExtensionsUAV )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;
    pipeDesc.hlslExtensionsUAV = 1;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 3004, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

// ==========================================================================
// Phase 4 — Destructive / Negative
// ==========================================================================

UTEST_F( RT, ShaderCompileFail )
{
    if ( !g_vhInit.raytracing ) return;

    const char* badSrc = R"(
[shader("raygeneration")]
void main() { this is not valid HLSL }
)";

    vhShader shader = vhAllocShader();
    std::vector< uint32_t > spirv;
    std::string error;
    bool ok = vhCompileShader( "Bad", badSrc, VRHI_SHADER_STAGE_RAYGEN | VRHI_SHADER_SM_6_5, spirv, "main", {}, {}, &error );
    EXPECT_FALSE( ok );
    EXPECT_TRUE( !error.empty() );

    vhDestroyShader( shader );
}

UTEST_F( RT, DestroyBoundAS )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 4000, state, table, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyAS( tlas );
    vhDestroyAS( blas );

    vhAccelStruct blas2 = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas2 = BuildTriTLAS( blas2 );

    vhState state2;
    state2.DirtyAll()
          .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
          .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
          .SetAccelStruct( 0, tlas2, -1, "g_Scene" )
          .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    DispatchAndReset( 4000, state2, table, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas2 );
    vhDestroyAS( blas2 );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, StateReset )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhStateId sid = 4001;
    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );
    vhSetState( sid, state );
    vhDispatchRays( sid, table, args );
    vhFinish();

    vhState reset;
    reset.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );
    vhSetState( sid, reset );
    vhDispatchRays( sid, table, args );
    vhFinish();
    vhSetState( sid, vhState{}.DirtyAll() );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, ShaderTableRebuild )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 4002, state, table, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyShaderTable( table );
    vhShaderTable table2 = vhAllocShaderTable();
    vhCreateShaderTable( table2, pipeline );
    vhShaderTableSetRayGen( table2, "main" );
    vhShaderTableAddMiss( table2, "miss" );
    vhShaderTableAddHitGroup( table2, "hg_main" );

    state.DirtyAll(); // vhSetState clears dirty bits; must re-mark before second dispatch.
    DispatchAndReset( 4002, state, table2, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table2 );
}

UTEST_F( RT, NewPipelineNewTable )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipelineA = vhAllocRTPipeline();
    vhCreateRTPipeline( pipelineA, pipeDesc );
    vhShaderTable tableA = vhAllocShaderTable();
    vhCreateShaderTable( tableA, pipelineA );
    vhShaderTableSetRayGen( tableA, "main" );
    vhShaderTableAddMiss( tableA, "miss" );
    vhShaderTableAddHitGroup( tableA, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 4003, state, tableA, args );

    vhDestroyShaderTable( tableA );
    vhDestroyRTPipeline( pipelineA );

    vhRTPipeline pipelineB = vhAllocRTPipeline();
    vhCreateRTPipeline( pipelineB, pipeDesc );
    vhShaderTable tableB = vhAllocShaderTable();
    vhCreateShaderTable( tableB, pipelineB );
    vhShaderTableSetRayGen( tableB, "main" );
    vhShaderTableAddMiss( tableB, "miss" );
    vhShaderTableAddHitGroup( tableB, "hg_main" );

    state.DirtyAll(); // vhSetState clears dirty bits; must re-mark before second dispatch.
    DispatchAndReset( 4003, state, tableB, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipelineB );
    vhDestroyShaderTable( tableB );
}

// ==========================================================================
// Phase 5 — Inline RT Deep Dive
// ==========================================================================

UTEST_F( RT, InlineCommittedStatus )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader cs = CreateComputeShaderRT( g_inlineRTAllStatus_CS );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateComputeProgram( cs ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    vhStateId sid = 5000;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();
    vhState reset; vhSetState( sid, reset.DirtyAll() );

    EXPECT_TRUE( true );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( cs );
}

UTEST_F( RT, InlineNegZDirection )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVerticesCCW, sizeof( kTriVerticesCCW ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::None );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader cs = CreateComputeShaderRT( g_inlineRTNegZ_CS );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateComputeProgram( cs ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    vhStateId sid = 5001;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );
    vhFinish();
    vhState reset; vhSetState( sid, reset.DirtyAll() );

     EXPECT_TRUE( VerifyPixel( rt, 1, 1, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( cs );
}

// ==========================================================================
// Gap Coverage — AllowEmptyInstances, Sphere, Compaction, Refit, ShaderTableBindings, TLASFromBuffer
// ==========================================================================

static vhAccelStruct BuildSphereBLAS( float x, float y, float z, float radius )
{
    float data[4] = { x, y, z, radius };
    vhBuffer buf = CreateTestStorageBuffer( data, sizeof( data ) );

    nvrhi::rt::GeometryDesc geo;
    geo.geometryType = nvrhi::rt::GeometryType::Spheres;
    geo.geometryData.spheres.vertexBuffer = vhGetBufferNvrhiHandle( buf );
    geo.geometryData.spheres.vertexPositionFormat = nvrhi::Format::RGB32_FLOAT;
    geo.geometryData.spheres.vertexRadiusFormat = nvrhi::Format::R32_FLOAT;
    geo.geometryData.spheres.vertexPositionOffset = 0;
    geo.geometryData.spheres.vertexRadiusOffset = 3 * sizeof( float );
    geo.geometryData.spheres.vertexPositionStride = 4 * sizeof( float );
    geo.geometryData.spheres.vertexRadiusStride = 4 * sizeof( float );
    geo.geometryData.spheres.vertexCount = 1;
    geo.flags = nvrhi::rt::GeometryFlags::Opaque;

    vhAccelStruct blas = vhAllocAS();
    vhCreateAS( blas, nvrhi::rt::AccelStructDesc().setIsTopLevel( false ).setDebugName( "SphereBLAS" ) );
    vhBuildBLAS( blas, { geo } );
    vhDestroyBuffer( buf );
    return blas;
}

UTEST_F( RT, AllowEmptyInstances )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 2 ).setBuildFlags( nvrhi::rt::AccelStructBuildFlags::AllowEmptyInstances ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc instValid;
    instValid.bottomLevelAS = vhGetASNvrhiHandle( blas );
    instValid.instanceMask = 0xFF;
    instValid.instanceContributionToHitGroupIndex = 0;
    instValid.flags = nvrhi::rt::InstanceFlags::None;
    instValid.setTransform( nvrhi::rt::c_IdentityTransform );

    nvrhi::rt::InstanceDesc instNull;
    instNull.bottomLevelAS = nullptr;
    instNull.instanceMask = 0xFF;
    instNull.instanceContributionToHitGroupIndex = 0;
    instNull.flags = nvrhi::rt::InstanceFlags::None;
    instNull.setTransform( nvrhi::rt::c_IdentityTransform );

    vhBuildTLAS( tlas, { instNull, instValid } );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 6000, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, SphereGeometry )
{
    if ( !g_vhInit.raytracing ) return;
    if ( !g_vhDevice->queryFeatureSupport( nvrhi::Feature::Spheres ) ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhAccelStruct blas = BuildSphereBLAS( 0.0f, 0.0f, 0.0f, 0.5f );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 6001, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 1, 1, 0xFF0000FF ) );
    EXPECT_TRUE( VerifyPixel( rt, 3, 3, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, BLASCompaction )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3, nvrhi::rt::GeometryFlags::Opaque, nvrhi::rt::AccelStructBuildFlags::AllowCompaction );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;

    DispatchAndReset( 6002, state, table, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhCompactBLAS();
    vhFinish();

    // Verify the BLAS reports as compacted (only true when RTXMU is enabled; otherwise no-op).
    nvrhi::rt::AccelStructHandle nvAS = vhGetASNvrhiHandle( blas );
    if ( nvAS ) ( void ) nvAS->isCompacted(); // Don't assert — RTXMU may be disabled.

    // After compaction the BLAS GPU device address may have changed; rebuild the TLAS so
    // it picks up the new address before dispatching again.
    nvrhi::rt::InstanceDesc reinstInst;
    reinstInst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    reinstInst.instanceMask = 0xFF;
    reinstInst.instanceContributionToHitGroupIndex = 0;
    reinstInst.flags = nvrhi::rt::InstanceFlags::None;
    reinstInst.setTransform( nvrhi::rt::c_IdentityTransform );
    vhBuildTLAS( tlas, { reinstInst } );
    vhFinish();

    // Re-mark all dirty so the second dispatch re-uploads the full state.
    state.DirtyAll();
    DispatchAndReset( 6002, state, table, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, TLASRefit )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setBuildFlags( nvrhi::rt::AccelStructBuildFlags::AllowUpdate ).setDebugName( "TestTLAS" ) );

    nvrhi::rt::InstanceDesc inst;
    inst.bottomLevelAS = vhGetASNvrhiHandle( blas );
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );

    vhBuildTLAS( tlas, { inst } );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 6003, state, table, args );
    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    nvrhi::rt::AffineTransform xform;
    xform[0] = 1.f; xform[1] = 0.f; xform[2] = 0.f; xform[3] = 0.f;
    xform[4] = 0.f; xform[5] = 1.f; xform[6] = 0.f; xform[7] = 10.f;
    xform[8] = 0.f; xform[9] = 0.f; xform[10] = 1.f; xform[11] = 0.f;
    inst.setTransform( xform );

    vhBuildTLAS( tlas, { inst }, nvrhi::rt::AccelStructBuildFlags::PerformUpdate );

    state.DirtyAll(); // vhSetState clears dirty bits; must re-mark before second dispatch.
    DispatchAndReset( 6003, state, table, args );
    EXPECT_TRUE( VerifyAllPixels( rt, 0xFFFF0000 ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, ShaderTableBindings )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );
    vhAccelStruct tlas = BuildTriTLAS( blas );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );

    // Per-entry SBT binding sets are not supported by this NVRHI version; use global bindings.
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 6004, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

UTEST_F( RT, TLASFromBuffer )
{
    if ( !g_vhInit.raytracing ) return;

    vhTexture rt = CreateTestTexture( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhBuffer vb = CreateTestVB( "float3", kTriVertices, sizeof( kTriVertices ) );

    vhAccelStruct blas = BuildTriBLAS( vb, 3 );

    // For buildTopLevelAccelStructFromBuffer, the buffer must contain GPU-side InstanceDesc
    // with blasDeviceAddress (not the CPU-side bottomLevelAS pointer). Flush so the BLAS
    // nvrhi handle is valid before reading its device address.
    vhFlush( true );
    nvrhi::rt::InstanceDesc inst;
    inst.blasDeviceAddress = vhGetASNvrhiHandle( blas )->getDeviceAddress();
    inst.instanceMask = 0xFF;
    inst.instanceContributionToHitGroupIndex = 0;
    inst.flags = nvrhi::rt::InstanceFlags::None;
    inst.setTransform( nvrhi::rt::c_IdentityTransform );

    vhBuffer instanceBuf = CreateTestStorageBuffer( &inst, sizeof( inst ), 0, VRHI_BUFFER_ACCEL_INPUT );

    vhAccelStruct tlas = vhAllocAS();
    vhCreateAS( tlas, nvrhi::rt::AccelStructDesc().setIsTopLevel( true ).setTopLevelMaxInstances( 1 ).setDebugName( "TestTLAS" ) );
    vhBuildTLASFromBuffer( tlas, instanceBuf, 1 );

    vhShader rayGen = CreateRTShader( g_rayGenHLSL, VRHI_SHADER_STAGE_RAYGEN );
    vhShader miss = CreateRTShader( g_missHLSL, VRHI_SHADER_STAGE_MISS );
    vhShader closestHit = CreateRTShader( g_hitHLSL, VRHI_SHADER_STAGE_CLOSEST_HIT );

    nvrhi::rt::PipelineDesc pipeDesc;
    nvrhi::rt::PipelineShaderDesc rgDesc; rgDesc.shader = vhGetShaderNvrhiHandle( rayGen ); rgDesc.exportName = "main";
    nvrhi::rt::PipelineShaderDesc mDesc;  mDesc.shader  = vhGetShaderNvrhiHandle( miss );  mDesc.exportName = "miss";
    nvrhi::rt::PipelineHitGroupDesc hgDesc; hgDesc.exportName = "hg_main"; hgDesc.closestHitShader = vhGetShaderNvrhiHandle( closestHit );
    pipeDesc.shaders = { rgDesc, mDesc };
    pipeDesc.hitGroups = { hgDesc };
    pipeDesc.maxPayloadSize = sizeof( float ) * 4;

    vhRTPipeline pipeline = vhAllocRTPipeline();
    vhCreateRTPipeline( pipeline, pipeDesc );
    vhShaderTable table = vhAllocShaderTable();
    vhCreateShaderTable( table, pipeline );
    vhShaderTableSetRayGen( table, "main" );
    vhShaderTableAddMiss( table, "miss" );
    vhShaderTableAddHitGroup( table, "hg_main" );

    vhState state;
    state.DirtyAll()
         .SetProgram( vhCreateRTProgram( rayGen, miss, closestHit ) )
         .SetTexture( 0, { .name = "g_Output", .texture = rt, .computeUAV = true } )
         .SetAccelStruct( 0, tlas, -1, "g_Scene" )
         .SetViewRect( glm::vec4( 0, 0, 4, 4 ) );

    nvrhi::rt::DispatchRaysArguments args; args.width = 4; args.height = 4;
    DispatchAndReset( 6005, state, table, args );

    EXPECT_TRUE( VerifyPixel( rt, 0, 0, 0xFF0000FF ) );

    vhDestroyTexture( rt );
    vhDestroyBuffer( vb );
    vhDestroyBuffer( instanceBuf );
    vhDestroyAS( tlas );
    vhDestroyAS( blas );
    vhDestroyShader( rayGen );
    vhDestroyShader( miss );
    vhDestroyShader( closestHit );
    vhDestroyRTPipeline( pipeline );
    vhDestroyShaderTable( table );
}

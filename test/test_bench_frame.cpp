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
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
    OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Mixed depth/lit/sprite/UI/compute frame benchmark with changing globals
// and a backend drain per frame.
// Set VRHI_BENCH_FRAMES and VRHI_BENCH_GC_INTERVAL to adjust the run.
// Set VRHI_BENCH_HEAVY=1 to enable the 500/1000-draw benchmarks.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <numeric>
#include <string>
#include <vector>

#include "test.h"
#include <vrhi.h>

extern bool g_testInit;
extern bool g_testInitQuiet;

// ---------------------------------------------------------------------------
// Config / stats
// ---------------------------------------------------------------------------

struct FrameBenchConfig
{
    int frames = 300;

    FrameBenchConfig()
    {
        const char* env = getenv( "VRHI_BENCH_FRAMES" );
        if ( !env ) return;
        if ( strcmp( env, "light" ) == 0 ) frames = 64;
        else if ( strcmp( env, "long" ) == 0 ) frames = 1000;
        else { int n = atoi( env ); if ( n > 0 ) frames = n; }
    }
};
static FrameBenchConfig g_frameBenchConfig;

struct FrameTimer
{
    std::vector< double > samples;
    std::chrono::steady_clock::time_point t0;

    void Begin() { t0 = std::chrono::steady_clock::now(); }
    void Sample()
    {
        auto now = std::chrono::steady_clock::now();
        samples.push_back( std::chrono::duration< double, std::milli >( now - t0 ).count() );
        t0 = now;
    }
    double Percentile( double pct ) const
    {
        if ( samples.empty() ) return 0.0;
        std::vector< double > s = samples;
        std::sort( s.begin(), s.end() );
        int idx = (int)( pct / 100.0 * ( s.size() - 1 ) + 0.5 );
        return s[std::max( 0, std::min( idx, (int)s.size() - 1 ) )];
    }
    double Mean() const { return samples.empty() ? 0.0 : std::accumulate( samples.begin(), samples.end(), 0.0 ) / samples.size(); }
};

static void FramePrint( const char* label, const FrameTimer& t, int draws, int dispatches )
{
    UTEST_PRINTF( "[VRHI_BENCH] %-32s frames=%-5d draws=%-4d dispatches=%-3d | frame avg=%8.3fms p50=%8.3fms p95=%8.3fms p99=%8.3fms max=%8.3fms\n",
        label, (int)t.samples.size(), draws, dispatches,
        t.Mean(), t.Percentile( 50.0 ), t.Percentile( 95.0 ), t.Percentile( 99.0 ), t.Percentile( 100.0 ) );
}

// ---------------------------------------------------------------------------
// Standard cbuffers, loose global uniforms, named lighting resources,
// bindless material textures, sprite instance buffer.
// ---------------------------------------------------------------------------

static const char* kFrameStandardCbuffers = R"(
cbuffer GlobalUniforms
{
    float4 u_viewRect;
    float4 u_viewTexel;
    float4x4 u_view;
    float4x4 u_invView;
    float4x4 u_proj;
    float4x4 u_invProj;
    float4x4 u_viewProj;
    float4x4 u_invViewProj;
    float4 u_alphaRef4;
    float4 u_global[21];
};
cbuffer WorldUniforms
{
    float4x4 u_world[4];
    float4x4 u_worldView;
    float4x4 u_worldViewProj;
    float4 _worldPad[8];
};
)";

// The frame counter changes every frame.
static const char* kFrameGlobalUniforms = R"(
uniform float4 g_globalUniformViewport;
uniform float4 g_globalUniformCameraPos;
uniform float4x4 g_globalUniformInvViewProj;
uniform float4x4 g_globalUniformPrevViewProj;
uniform float4 g_globalUniformDirectionalLightData[22];
uniform float4 g_globalUniformRaytracingData;
uniform float4 g_globalUniformFogData[9];
uniform float4 g_globalUniformDebug;
uniform float4 g_globalUniformFrame;
uniform float4 g_globalUniformSunshadowFallback;
uniform float4 g_globalUniformPostProcess;
uniform float4 g_globalUniformVFX;
)";

static const char* kFrameVS_Depth = R"(
struct VSIn { float3 pos : POSITION; float4 col : COLOUR; };
[shader("vertex")] float4 main( VSIn i ) : SV_Position
{
    float4 wp = mul( u_world[0], float4( i.pos, 1.0 ) );
    wp.x += i.col.x * 0.000001;
    return mul( u_viewProj, wp );
}
)";

static const char* kFrameVS_Lit = R"(
struct VSIn { float3 pos : POSITION; float4 col : COLOUR; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOUR; float2 uv : TEXCOORD0; };
[shader("vertex")] VSOut main( VSIn i )
{
    VSOut o;
    float4 wp = mul( u_world[0], float4( i.pos, 1.0 ) );
    o.pos = mul( u_viewProj, wp );
    o.col = i.col;
    o.uv = i.pos.xy * 0.5 + 0.5;
    return o;
}
)";

static const char* kFramePS_Depth = R"(
[shader("pixel")] void main() {}
)";

static const char* kFramePS_Lit = R"(
Texture2D g_Textures[] : register(t0, VRHI_BINDLESS_SPACE);
SamplerState matPBRSampler : register(s0, VRHI_STAGE_SPACE);
StructuredBuffer<float4> lights : register(t8, VRHI_STAGE_SPACE);
[[vk::image_format("r16ui")]] Texture2D<uint> visLightTile : register(t9, VRHI_STAGE_SPACE);
Texture2D envBRDFLUTTexture : register(t10, VRHI_STAGE_SPACE);
SamplerState envBRDFLUTTexture_sampler : register(s1, VRHI_STAGE_SPACE);
Texture2D deferredSunShadow : register(t11, VRHI_STAGE_SPACE);
SamplerState deferredSunShadow_sampler : register(s2, VRHI_STAGE_SPACE);
Texture2D spotShadowMap : register(t12, VRHI_STAGE_SPACE);
SamplerState spotShadowMap_sampler : register(s3, VRHI_STAGE_SPACE);
Texture2D sunshadowMapFallback : register(t13, VRHI_STAGE_SPACE);
SamplerState sunshadowMapFallback_sampler : register(s4, VRHI_STAGE_SPACE);
Texture2D gtaoTexture : register(t14, VRHI_STAGE_SPACE);
SamplerState gtaoTexture_sampler : register(s5, VRHI_STAGE_SPACE);

uniform float4 diffuseTint;
uniform float4 emissiveTint;
uniform float4 emissiveColour;
uniform float4 roughnessMinMax;
uniform float4 normalStrength;
uniform float4 specularF0;
uniform float4 alpha;
uniform float4 metallic;
uniform float4 occlusion;
uniform float4 customData1;
uniform float4 customData2;
uniform float4 uvScroll;
uniform float4 pbrTexIdx;
uniform float4 pbrTexIdx2;

[shader("pixel")] float4 main( float4 col : COLOUR, float2 uv : TEXCOORD0 ) : SV_Target
{
    float3 alb = g_Textures[(uint) pbrTexIdx.x].Sample( matPBRSampler, uv + uvScroll.xy ).rgb * diffuseTint.rgb;
    float3 nrm = normalize( g_Textures[(uint) pbrTexIdx.y].Sample( matPBRSampler, uv ).rgb * 2.0 - 1.0 );
    float3 rom = g_Textures[(uint) pbrTexIdx.z].Sample( matPBRSampler, uv ).rgb;
    float3 emi = g_Textures[(uint) pbrTexIdx2.x].Sample( matPBRSampler, uv ).rgb * emissiveColour.rgb + emissiveTint.rgb;
    float3 sh = gtaoTexture.Sample( gtaoTexture_sampler, uv ).rrr;
    float3 ss = deferredSunShadow.Sample( deferredSunShadow_sampler, uv ).rrr;
    float3 sp = spotShadowMap.Sample( spotShadowMap_sampler, uv ).rrr;
    float3 sf = sunshadowMapFallback.Sample( sunshadowMapFallback_sampler, uv ).rrr;
    float3 brdf = envBRDFLUTTexture.Sample( envBRDFLUTTexture_sampler, uv ).rgb;
    float4 lc = lights[0];
    float lt = (float) visLightTile.Load( int3( 0, 0, 0 ) ).x;
    return float4( alb * sh * ss * sp * sf * brdf * lc.rgb + emi + nrm * normalStrength.x
                     + rom * roughnessMinMax.x + specularF0.rgb + metallic.xxx + occlusion.xxx
                     + col.rgb + g_globalUniformVFX.rgb + lt, alpha.x );
}
)";

static const char* kFrameVS_Sprite = R"(
struct InstanceData
{
    float4 data0;
    float4 data1;
    float4 data2;
    float4 data3;
};
StructuredBuffer<InstanceData> u_instanceData : register(t0, VRHI_STAGE_SPACE);
Texture2D tex0 : register(t1, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
Texture2D autoExposureTex : register(t2, VRHI_STAGE_SPACE);
uniform float4 u_spriteExposure;
struct VsInput
{
    [[vk::location(0)]] float3 position : ATTR0;
    [[vk::location(1)]] float2 uv : ATTR1;
    uint instanceID : SV_VulkanInstanceID;
};
struct PsInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float invExposure : TEXCOORD1;
};
[shader("vertex")] PsInput main( VsInput input )
{
    PsInput o;
    InstanceData inst = u_instanceData[NonUniformResourceIndex( input.instanceID )];
    float2 world = float2( inst.data0.x * input.position.x + inst.data0.z * input.position.y,
                           inst.data0.y * input.position.x + inst.data0.w * input.position.y ) + inst.data1.xy;
    o.position = mul( u_viewProj, float4( world, 0.0, 1.0 ) );
    o.uv = inst.data2.xy + input.uv * inst.data2.zw;
    o.color = inst.data3;
    o.invExposure = 1.0 / max( u_spriteExposure.x, 0.0001 );
    return o;
}
)";

static const char* kFramePS_Sprite = R"(
Texture2D tex0 : register(t1, VRHI_STAGE_SPACE);
Texture2D autoExposureTex : register(t2, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
[shader("pixel")] float4 main( float2 uv : TEXCOORD0, float4 color : COLOR0, nointerpolation float invExposure : TEXCOORD1 ) : SV_Target
{
    return tex0.Sample( s0, uv ) * color * invExposure + autoExposureTex.Sample( s0, uv ) * 0.01;
}
)";

static const char* kFrameVS_UI = R"(
struct VSIn { float3 pos : POSITION; float4 col : COLOUR; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOUR; float2 uv : TEXCOORD0; };
[shader("vertex")] VSOut main( VSIn i )
{
    VSOut o;
    o.pos = mul( u_viewProj, float4( i.pos, 1.0 ) );
    o.col = i.col;
    o.uv = i.pos.xy * 0.5 + 0.5;
    return o;
}
)";

static const char* kFramePS_UI = R"(
Texture2D glyphTex : register(t0, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
uniform float4 u_uiColourMul;
uniform float4 u_uiUVScroll;
[shader("pixel")] float4 main( float4 col : COLOUR, float2 uv : TEXCOORD0 ) : SV_Target
{
    return glyphTex.Sample( s0, uv + u_uiUVScroll.xy ) * col * u_uiColourMul;
}
)";

// Compute passes also pack the global uniform block.
static const char* kFrameCS_Base = R"(
[[vk::image_format("rgba8")]] RWTexture2D<float4> g_Out : register(u0, VRHI_STAGE_SPACE);
Texture2D<float4> g_In : register(t0, VRHI_STAGE_SPACE);
)";

static const char* kFrameCS_A = R"(
[numthreads(8,8,1)] void main( uint3 id : SV_DispatchThreadID )
{
    float4 c = g_In.Load( int3( id.xy, 0 ) );
    g_Out[id.xy] = c * u_params0 + g_globalUniformFrame;
}
)";

static const char* kFrameCS_B = R"(
uniform float4 u_params0;
uniform float4 u_params1;
uniform float4 u_params2;
uniform float4 u_params3;
[numthreads(8,8,1)] void main( uint3 id : SV_DispatchThreadID )
{
    float4 c = g_In.Load( int3( id.xy, 0 ) );
    g_Out[id.xy] = ( c + u_params0 + u_params1 + u_params2 + u_params3 ) * g_globalUniformPostProcess;
}
)";

static const char* kFrameCS_C = R"(
uniform float4 u_params0;
uniform float4 u_params1;
[numthreads(8,8,1)] void main( uint3 id : SV_DispatchThreadID )
{
    float4 c = g_In.Load( int3( id.xy, 0 ) );
    g_Out[id.xy] = dot( c, u_params0 ) + dot( c, u_params1 ) + g_globalUniformVFX;
}
)";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static vhShader FrameCompile( const char* name, const std::string& src, uint64_t stage )
{
    std::vector< uint32_t > spirv;
    std::string err;
    bool ok = vhCompileShader( name, src.c_str(), stage | VRHI_SHADER_SM_6_0 | VRHI_SHADER_PATCH_DSET0, spirv, "main", {}, {}, &err );
    if ( !ok )
    {
        UTEST_PRINTF( "FrameCompile FAILED (%s): %s\n", name, err.c_str() );
        return VRHI_INVALID_HANDLE;
    }
    vhShader sh = vhAllocShader();
    vhCreateShader( sh, name, stage, spirv, "main" );
    return sh;
}

static vhTexture FrameMakeTex( int w, int h, nvrhi::Format fmt, uint64_t flags = VRHI_TEXTURE_NONE )
{
    vhTexture t = vhAllocTexture();
    vhCreateTexture2D( t, "BenchTex", glm::ivec2( w, h ), 1, fmt, flags );
    return t;
}

static vhBuffer FrameMakeVB( int numVerts, bool rasterize = false )
{
    vhBuffer b = vhAllocBuffer();
    struct V { glm::vec3 p; glm::vec4 c; };
    vhMem* mem = vhAllocMem( numVerts * sizeof( V ) );
    memset( mem->data(), 0, mem->size() );
    if ( rasterize )
    {
        V vertices[4] = { { { -1, -1, 0.5f }, glm::vec4( 1 ) }, { { 1, -1, 0.5f }, glm::vec4( 1 ) },
                          { { -1, 1, 0.5f }, glm::vec4( 1 ) }, { { 1, 1, 0.5f }, glm::vec4( 1 ) } };
        memcpy( mem->data(), vertices, sizeof( vertices ) );
    }
    vhCreateVertexBuffer( b, "BenchVB", mem, "float3 float4", numVerts );
    return b;
}

struct FrameRng
{
    uint32_t s = 0x12345678u;
    float Next01() { s = s * 1664525u + 1013904223u; return ( float ) ( s >> 8 ) / 16777216.0f; }
};

// ---------------------------------------------------------------------------
// Frame workload
// ---------------------------------------------------------------------------

struct FrameWorkload
{
    int depthDraws = 45;      // depth prepass records
    int litBatches = 22;      // instanced material batches
    int litSingles = 14;      // below-instance-threshold draws
    int spriteBatches = 12;   // sprite batches (~160 instances each)
    int uiDraws = 12;         // HUD elements
    int computeWaveA = 8;     // depthmips/gtao/sunshadow/lighttile/spotshadow
    int computeWaveB = 9;     // autoexposure/veil/lensflare/tonemapLUT
    float worldRange = 64.0f;
    uint64_t litFlags = VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE;
};

struct FrameResources
{
    vhTexture rt;
    vhTexture ds;
    vhBuffer vb;
    vhBuffer spriteVB;
    vhBuffer ib;
    vhBuffer lightsUB;
    vhBuffer spriteInstanceSB;
    vhTexture lightingTex[7];     // visLightTile / envBRDF / sunshadow / spot / fallback / gtao (+spare)
    vhTexture atlasTex[8];
    vhTexture glyphTex[4];
    vhTexture computeIn;
    vhTexture computeOut;
    vhDescriptorTable bindlessTable;
    vhShader vsDepth, psDepth, vsLit, psLit, vsSprite, psSprite, vsUI, psUI;
    vhShader csA, csB, csC;
    vhProgram progDepth, progLit, progSprite, progUI, progCA, progCB, progCC;

    // Reused per-pass state objects.
    vhState stDepth, stLit, stSprite, stUI, stCompute;

    glm::ivec2 resolution = glm::ivec2( 64 );
    glm::uvec3 computeGroups = glm::uvec3( 8, 8, 1 );

    bool Init( bool heavy = false );
    void Shutdown();
};

bool FrameResources::Init( bool heavy )
{
    if ( heavy )
    {
        resolution = glm::ivec2( 960, 1280 );
        computeGroups = glm::uvec3( resolution.x / 8, resolution.y / 8, 1 );
    }
    rt = FrameMakeTex( resolution.x, resolution.y, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    ds = FrameMakeTex( resolution.x, resolution.y, nvrhi::Format::D32, VRHI_TEXTURE_RT );
    vb = FrameMakeVB( 4, heavy );
    spriteVB = vhAllocBuffer();
    {
        struct V { glm::vec3 p; glm::vec2 uv; };
        vhMem* mem = vhAllocMem( 4 * sizeof( V ) );
        memset( mem->data(), 0, mem->size() );
        if ( heavy )
        {
            V vertices[4] = { { { -0.03f, -0.03f, 0 }, { 0, 0 } }, { { 0.03f, -0.03f, 0 }, { 1, 0 } },
                              { { -0.03f, 0.03f, 0 }, { 0, 1 } }, { { 0.03f, 0.03f, 0 }, { 1, 1 } } };
            memcpy( mem->data(), vertices, sizeof( vertices ) );
        }
        vhCreateVertexBuffer( spriteVB, "BenchSpriteVB", mem, "float3 float2", 4 );
    }
    ib = vhAllocBuffer();
    {
        uint32_t idx[6] = { 0, 1, 2, 1, 3, 2 };
        vhMem* m = vhAllocMem( sizeof( idx ) );
        memcpy( m->data(), idx, sizeof( idx ) );
        vhCreateIndexBuffer( ib, "BenchIB", m, 0, VRHI_BUFFER_INDEX32 );
    }
    lightsUB = vhAllocBuffer();
    {
        vhMem* m = vhAllocMem( 2048 );
        memset( m->data(), 0, m->size() );
        vhCreateStorageBuffer( lightsUB, "BenchLights", m, 2048 );
    }
    spriteInstanceSB = vhAllocBuffer();
    {
        vhMem* m = vhAllocMem( 512 * 1024 );
        memset( m->data(), 0, m->size() );
        vhCreateStorageBuffer( spriteInstanceSB, "BenchSpriteInst", m, 512 * 1024 );
    }
    lightingTex[0] = FrameMakeTex( 16, 16, nvrhi::Format::R16_UINT );
    for ( int i = 1; i < 7; i++ ) lightingTex[i] = FrameMakeTex( 16, 16, nvrhi::Format::RGBA8_UNORM );
    for ( int i = 0; i < 8; i++ ) atlasTex[i] = FrameMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM );
    for ( int i = 0; i < 4; i++ ) glyphTex[i] = FrameMakeTex( 32, 32, nvrhi::Format::RGBA8_UNORM );
    glm::ivec2 computeSize = heavy ? resolution : glm::ivec2( 16 );
    computeIn = FrameMakeTex( computeSize.x, computeSize.y, nvrhi::Format::RGBA8_UNORM );
    computeOut = FrameMakeTex( computeSize.x, computeSize.y, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    if ( heavy )
    {
        auto fill = []( vhTexture texture, int bytes, int value )
        {
            vhMem* mem = vhAllocMem( bytes );
            memset( mem->data(), value, bytes );
            vhUpdateTexture( texture, 0, 0, 1, 1, mem );
        };
        fill( lightingTex[0], 16 * 16 * 2, 0 );
        for ( int i = 1; i < 7; i++ ) fill( lightingTex[i], 16 * 16 * 4, 255 );
        for ( int i = 0; i < 8; i++ ) fill( atlasTex[i], 64 * 64 * 4, 255 );
        for ( int i = 0; i < 4; i++ ) fill( glyphTex[i], 32 * 32 * 4, 255 );
        fill( computeIn, computeSize.x * computeSize.y * 4, 64 );
    }
    vhFinish();

    bindlessTable = vhCreateDescriptorTableSimple( nvrhi::ResourceType::Texture_SRV, 64 );
    for ( int i = 0; i < 8; i++ ) vhDescriptorTableSetTexture( bindlessTable, i, atlasTex[i] );
    vhFinish();

    vsDepth = FrameCompile( "Frame_VS_Depth", std::string( kFrameStandardCbuffers ) + kFrameVS_Depth, VRHI_SHADER_STAGE_VERTEX );
    psDepth = FrameCompile( "Frame_PS_Depth", kFramePS_Depth, VRHI_SHADER_STAGE_PIXEL );
    vsLit = FrameCompile( "Frame_VS_Lit", std::string( kFrameStandardCbuffers ) + kFrameVS_Lit, VRHI_SHADER_STAGE_VERTEX );
    psLit = FrameCompile( "Frame_PS_Lit", std::string( kFrameStandardCbuffers ) + kFrameGlobalUniforms + kFramePS_Lit, VRHI_SHADER_STAGE_PIXEL );
    vsSprite = FrameCompile( "Frame_VS_Sprite", std::string( kFrameStandardCbuffers ) + kFrameVS_Sprite, VRHI_SHADER_STAGE_VERTEX );
    psSprite = FrameCompile( "Frame_PS_Sprite", kFramePS_Sprite, VRHI_SHADER_STAGE_PIXEL );
    vsUI = FrameCompile( "Frame_VS_UI", std::string( kFrameStandardCbuffers ) + kFrameVS_UI, VRHI_SHADER_STAGE_VERTEX );
    psUI = FrameCompile( "Frame_PS_UI", std::string( kFrameStandardCbuffers ) + kFrameGlobalUniforms + kFramePS_UI, VRHI_SHADER_STAGE_PIXEL );
    csA = FrameCompile( "Frame_CS_A", std::string( kFrameStandardCbuffers ) + kFrameGlobalUniforms + kFrameCS_Base + "uniform float4 u_params0;\n" + kFrameCS_A, VRHI_SHADER_STAGE_COMPUTE );
    csB = FrameCompile( "Frame_CS_B", std::string( kFrameStandardCbuffers ) + kFrameGlobalUniforms + kFrameCS_Base + kFrameCS_B, VRHI_SHADER_STAGE_COMPUTE );
    csC = FrameCompile( "Frame_CS_C", std::string( kFrameStandardCbuffers ) + kFrameGlobalUniforms + kFrameCS_Base + kFrameCS_C, VRHI_SHADER_STAGE_COMPUTE );
    if ( vsDepth == VRHI_INVALID_HANDLE || psLit == VRHI_INVALID_HANDLE || csB == VRHI_INVALID_HANDLE )
        return false;

    progDepth = vhCreateGfxProgram( vsDepth, psDepth );
    progLit = vhCreateGfxProgram( vsLit, psLit );
    progSprite = vhCreateGfxProgram( vsSprite, psSprite );
    progUI = vhCreateGfxProgram( vsUI, psUI );
    progCA = vhCreateComputeProgram( csA );
    progCB = vhCreateComputeProgram( csB );
    progCC = vhCreateComputeProgram( csC );
    vhFinish();

    stDepth.SetColourAttachment( 0, rt ).SetDepthAttachment( ds )
        .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
        .SetViewClear( VRHI_CLEAR_COLOR | VRHI_CLEAR_DEPTH, glm::vec4( 0 ), 1.0f, 0 )
        .SetStateFlags( VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE )
        .SetVertexBuffer( vb, 0 ).SetIndexBuffer( ib ).SetProgram( progDepth );

    stLit.SetColourAttachment( 0, rt ).SetDepthAttachment( ds )
        .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
        .SetStateFlags( VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE )
        .SetVertexBuffer( vb, 0 ).SetIndexBuffer( ib ).SetProgram( progLit );

    stSprite.SetColourAttachment( 0, rt )
        .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
        .SetStateFlags( VRHI_STATE_WRITE_RGB | VRHI_STATE_CULL_NONE | VRHI_STATE_BLEND_ALPHA )
        .SetVertexBuffer( spriteVB, 0 ).SetIndexBuffer( ib ).SetProgram( progSprite );

    stUI.SetColourAttachment( 0, rt )
        .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
        .SetStateFlags( VRHI_STATE_WRITE_MASK | VRHI_STATE_BLEND_ALPHA )
        .SetVertexBuffer( vb, 0 ).SetIndexBuffer( ib ).SetProgram( progUI );

    stCompute.SetProgram( progCB ); // placeholder; program swapped per dispatch below

    if ( heavy )
    {
        glm::vec4 rect( 0, 0, resolution.x, resolution.y );
        stDepth.SetViewRect( rect ).SetStateFlags( VRHI_STATE_WRITE_Z | VRHI_STATE_CULL_NONE | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE );
        stLit.SetViewRect( rect );
        stSprite.SetViewRect( rect );
        stUI.SetViewRect( rect );
        stCompute.SetViewRect( rect );
    }
    return true;
}

void FrameResources::Shutdown()
{
    // Wait for GPU idle before destroying resources.
    vhFinish();
    vhDestroyTexture( rt ); vhDestroyTexture( ds ); vhDestroyBuffer( vb ); vhDestroyBuffer( spriteVB ); vhDestroyBuffer( ib );
    vhDestroyBuffer( lightsUB ); vhDestroyBuffer( spriteInstanceSB );
    for ( int i = 0; i < 7; i++ ) vhDestroyTexture( lightingTex[i] );
    for ( int i = 0; i < 8; i++ ) vhDestroyTexture( atlasTex[i] );
    for ( int i = 0; i < 4; i++ ) vhDestroyTexture( glyphTex[i] );
    vhDestroyTexture( computeIn ); vhDestroyTexture( computeOut );
    vhDestroyDescriptorTable( bindlessTable );
    vhDestroyShader( vsDepth ); vhDestroyShader( psDepth ); vhDestroyShader( vsLit ); vhDestroyShader( psLit );
    vhDestroyShader( vsSprite ); vhDestroyShader( psSprite ); vhDestroyShader( vsUI ); vhDestroyShader( psUI );
    vhDestroyShader( csA ); vhDestroyShader( csB ); vhDestroyShader( csC );
    vhFinish();
}

// View/global uniforms for the benchmark's lit, compute and UI passes.
static void FrameSetGlobalUniforms( vhState& s, uint32_t frame, glm::ivec2 resolution )
{
    glm::vec4 viewportU( resolution.x, resolution.y, 1.0f / resolution.x, 1.0f / resolution.y );
    glm::vec4 camPos( 0, 0, -3, 1 );
    glm::mat4 invViewProj( 1.0f );
    glm::mat4 prevViewProj( 1.0f );
    glm::vec4 dirLightData[22];
    for ( int i = 0; i < 22; i++ ) dirLightData[i] = glm::vec4( float( i ), 0.5f, 0.25f, 0.125f );
    glm::vec4 rtData( 0, 1, 2, 3 );
    glm::vec4 fogData[9];
    for ( int i = 0; i < 9; i++ ) fogData[i] = glm::vec4( 0.1f * i, 0.2f, 0.3f, 0.4f );
    glm::vec4 debugU( 0 );
    // Frame counter: changes every frame.
    glm::vec4 frameU( float( frame & 0xFF ), 0, 0, 0 );
    glm::vec4 sunshadowFallbackU( 0, 0, -3, 64 );
    glm::vec4 postProcessU( 0.8f, 0.6f, 0, 0 );
    glm::vec4 vfxU( 1.0f, 1.05f, 1.1f, 1.85f );

    s.SetUniform( s.uniforms.size(), "g_globalUniformViewport", &viewportU );
    s.SetUniform( s.uniforms.size(), "g_globalUniformCameraPos", &camPos );
    s.SetUniform( s.uniforms.size(), "g_globalUniformInvViewProj", ( const glm::vec4* ) &invViewProj, 4 );
    s.SetUniform( s.uniforms.size(), "g_globalUniformPrevViewProj", ( const glm::vec4* ) &prevViewProj, 4 );
    s.SetUniform( s.uniforms.size(), "g_globalUniformDirectionalLightData", dirLightData, 22 );
    s.SetUniform( s.uniforms.size(), "g_globalUniformRaytracingData", &rtData );
    s.SetUniform( s.uniforms.size(), "g_globalUniformFogData", fogData, 9 );
    s.SetUniform( s.uniforms.size(), "g_globalUniformDebug", &debugU );
    s.SetUniform( s.uniforms.size(), "g_globalUniformFrame", &frameU );
    s.SetUniform( s.uniforms.size(), "g_globalUniformSunshadowFallback", &sunshadowFallbackU );
    s.SetUniform( s.uniforms.size(), "g_globalUniformPostProcess", &postProcessU );
    s.SetUniform( s.uniforms.size(), "g_globalUniformVFX", &vfxU );
}

static void FrameSetMaterialUniforms( vhState& s, FrameRng& rng, int material )
{
    glm::vec4 diffuseTint( rng.Next01(), rng.Next01(), rng.Next01(), 1 );
    glm::vec4 emissiveTint( rng.Next01() * 0.1f, 0, 0, 1 );
    glm::vec4 emissiveColour( rng.Next01(), rng.Next01(), rng.Next01(), 1 );
    glm::vec4 roughnessMinMax( rng.Next01(), 1, 0, 0 );
    glm::vec4 normalStrength( rng.Next01(), 0, 0, 0 );
    glm::vec4 specularF0( 0.04f, 0, 0, 0 );
    glm::vec4 alpha( 1, 0, 0, 0 );
    glm::vec4 metallic( rng.Next01(), 0, 0, 0 );
    glm::vec4 occlusion( 1, 0, 0, 0 );
    glm::vec4 customData1( rng.Next01(), 0, 0, 0 );
    glm::vec4 customData2( rng.Next01(), 0, 0, 0 );
    glm::vec4 uvScroll( rng.Next01() * 0.01f, 0, 0, 0 );
    glm::vec4 pbrTexIdx( ( float ) ( material % 8 ), ( float ) ( ( material + 1 ) % 8 ), ( float ) ( ( material + 2 ) % 8 ), 0 );
    glm::vec4 pbrTexIdx2( ( float ) ( ( material + 3 ) % 8 ), 0, 0, 0 );
    s.SetUniform( s.uniforms.size(), "diffuseTint", &diffuseTint );
    s.SetUniform( s.uniforms.size(), "emissiveTint", &emissiveTint );
    s.SetUniform( s.uniforms.size(), "emissiveColour", &emissiveColour );
    s.SetUniform( s.uniforms.size(), "roughnessMinMax", &roughnessMinMax );
    s.SetUniform( s.uniforms.size(), "normalStrength", &normalStrength );
    s.SetUniform( s.uniforms.size(), "specularF0", &specularF0 );
    s.SetUniform( s.uniforms.size(), "alpha", &alpha );
    s.SetUniform( s.uniforms.size(), "metallic", &metallic );
    s.SetUniform( s.uniforms.size(), "occlusion", &occlusion );
    s.SetUniform( s.uniforms.size(), "customData1", &customData1 );
    s.SetUniform( s.uniforms.size(), "customData2", &customData2 );
    s.SetUniform( s.uniforms.size(), "uvScroll", &uvScroll );
    s.SetUniform( s.uniforms.size(), "pbrTexIdx", &pbrTexIdx );
    s.SetUniform( s.uniforms.size(), "pbrTexIdx2", &pbrTexIdx2 );
}

// Runs one benchmark frame.
static void RunFrameBench( FrameResources& R, const FrameWorkload& W, uint32_t frame, vhStateId sidBase )
{
    FrameRng rng;
    rng.s = 0x9E3779B9u * ( frame + 1 );
    const vhStateId SID_DEPTH = sidBase + 1, SID_LIT = sidBase + 2, SID_SPRITE = sidBase + 3, SID_UI = sidBase + 4, SID_COMPUTE = sidBase + 5;

    // Depth prepass.
    for ( int i = 0; i < W.depthDraws; i++ )
    {
        float a = rng.Next01() * 6.2831f;
        glm::mat4 m( 1.0f );
        m[3][0] = rng.Next01() * W.worldRange;
        m[3][1] = rng.Next01() * W.worldRange;
        m[0][0] = cosf( a ); m[0][1] = sinf( a ); m[1][0] = -sinf( a ); m[1][1] = cosf( a );
        R.stDepth.SetWorldTransform( m );
        vhSetState( SID_DEPTH, R.stDepth );
        vhDrawIndexed( SID_DEPTH, 6, 1, 0, 0, 0 );
    }

    // Depth, lighting and shadow compute passes.
    for ( int i = 0; i < W.computeWaveA; i++ )
    {
        R.stCompute.ClearAllBindings().SetProgram( ( i % 3 ) == 0 ? R.progCA : ( ( i % 3 ) == 1 ? R.progCB : R.progCC ) )
            .SetTexture( 0, R.computeIn, -1, "g_In" )
            .SetTexture( 1, R.computeOut, -1, "g_Out", true );
        FrameSetGlobalUniforms( R.stCompute, frame, R.resolution );
        glm::vec4 p0( rng.Next01(), rng.Next01(), rng.Next01(), rng.Next01() );
        glm::vec4 p1( rng.Next01(), 0, 0, 0 );
        glm::vec4 p2( R.computeGroups.x, R.computeGroups.y, 0, 0 );
        glm::vec4 p3( float( frame ), 0, 0, 0 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params0", &p0 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params1", &p1 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params2", &p2 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params3", &p3 );
        vhSetState( SID_COMPUTE, R.stCompute );
        vhDispatch( SID_COMPUTE, R.computeGroups );
    }

    // Lit pass batches.
    for ( int b = 0; b < W.litBatches; b++ )
    {
        auto& s = R.stLit;
        s.ClearAllBindings()
            .SetVertexBuffer( R.vb, 0 ).SetIndexBuffer( R.ib )
            .SetStateFlags( W.litFlags )
            .SetProgram( R.progLit )
            .SetBuffer( 0, R.lightsUB, -1, "lights" )
            .SetTexture( 1, R.lightingTex[0], -1, "visLightTile" )
            .SetTexture( 2, R.lightingTex[1], -1, "envBRDFLUTTexture" )
            .SetSampler( 0, VRHI_SAMPLER_UVW_CLAMP | VRHI_SAMPLER_MIN_LINEAR | VRHI_SAMPLER_MAG_LINEAR | VRHI_SAMPLER_MIP_LINEAR, -1, "matPBRSampler" )
            .SetSampler( 1, VRHI_SAMPLER_UVW_CLAMP, -1, "envBRDFLUTTexture_sampler" )
            .SetTexture( 3, R.lightingTex[2], -1, "deferredSunShadow" )
            .SetSampler( 2, VRHI_SAMPLER_UVW_CLAMP | VRHI_SAMPLER_MIPBIAS( -0.5f ), -1, "deferredSunShadow_sampler" )
            .SetTexture( 4, R.lightingTex[3], -1, "spotShadowMap" )
            .SetSampler( 3, VRHI_SAMPLER_UVW_CLAMP | VRHI_SAMPLER_MIPBIAS( -0.5f ), -1, "spotShadowMap_sampler" )
            .SetTexture( 5, R.lightingTex[4], -1, "sunshadowMapFallback" )
            .SetSampler( 4, VRHI_SAMPLER_UVW_CLAMP | VRHI_SAMPLER_MIPBIAS( -1.0f ), -1, "sunshadowMapFallback_sampler" )
            .SetTexture( 6, R.lightingTex[5], -1, "gtaoTexture" )
            .SetSampler( 5, VRHI_SAMPLER_UVW_CLAMP, -1, "gtaoTexture_sampler" )
            .SetDescriptorTable( 0, R.bindlessTable );
        FrameSetGlobalUniforms( s, frame, R.resolution );
        FrameSetMaterialUniforms( s, rng, b );
        vhSetState( SID_LIT, s );
        vhDrawIndexed( SID_LIT, 6, 1 + ( b % 32 ), 0, 0, 0 );
    }

    // Non-instanced lit draws.
    for ( int i = 0; i < W.litSingles; i++ )
    {
        glm::mat4 m( 1.0f );
        m[3][0] = rng.Next01() * W.worldRange;
        m[3][1] = rng.Next01() * W.worldRange;
        R.stLit.SetWorldTransform( m );
        vhSetState( SID_LIT, R.stLit );
        vhDrawIndexed( SID_LIT, 6, 1, 0, 0, 0 );
    }

    // Sprite batches.
    for ( int b = 0; b < W.spriteBatches; b++ )
    {
        const int kInstances = 160;
        vhMem* mem = vhAllocMem( kInstances * 64 );
        for ( int i = 0; i < kInstances; i++ )
        {
            float* f = ( float* ) ( mem->data() + i * 64 );
            for ( int j = 0; j < 16; j++ ) f[j] = rng.Next01();
        }
        vhUpdateStorageBuffer( R.spriteInstanceSB, mem, ( uint64_t ) ( b % 4 ) * ( kInstances * 64 ), kInstances * 64 );

        auto& s = R.stSprite;
        s.ClearAllBindings()
            .SetVertexBuffer( R.spriteVB, 0 ).SetIndexBuffer( R.ib )
            .SetProgram( R.progSprite )
            .SetTexture( 0, R.atlasTex[b % 8], -1, "tex0" )
            .SetTexture( 1, R.lightingTex[6], -1, "autoExposureTex" )
            .SetSampler( 0, VRHI_SAMPLER_UVW_CLAMP | VRHI_SAMPLER_MIN_LINEAR | VRHI_SAMPLER_MAG_LINEAR | VRHI_SAMPLER_MIP_LINEAR, -1, "s0" )
            .SetBuffer( 1, R.spriteInstanceSB, -1, "u_instanceData", ( uint64_t ) ( b % 4 ) * ( kInstances * 64 ), kInstances * 64 );
        glm::vec4 exposure( 1.0f + rng.Next01() * 0.5f, 0, 1, -1 );
        s.SetUniform( s.uniforms.size(), "u_spriteExposure", &exposure );
        vhSetState( SID_SPRITE, s );
        vhDrawIndexed( SID_SPRITE, 6, kInstances, 0, 0, 0 );
    }

    // Post-process compute passes.
    for ( int i = 0; i < W.computeWaveB; i++ )
    {
        R.stCompute.ClearAllBindings().SetProgram( ( i % 3 ) == 0 ? R.progCB : ( ( i % 3 ) == 1 ? R.progCC : R.progCA ) )
            .SetTexture( 0, R.computeIn, -1, "g_In" )
            .SetTexture( 1, R.computeOut, -1, "g_Out", true );
        FrameSetGlobalUniforms( R.stCompute, frame, R.resolution );
        glm::vec4 p0( rng.Next01(), rng.Next01(), rng.Next01(), rng.Next01() );
        glm::vec4 p1( rng.Next01(), 0, 0, 0 );
        glm::vec4 p2( R.computeGroups.x, R.computeGroups.y, 0, 0 );
        glm::vec4 p3( float( frame ), 0, 0, 0 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params0", &p0 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params1", &p1 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params2", &p2 );
        R.stCompute.SetUniform( R.stCompute.uniforms.size(), "u_params3", &p3 );
        vhSetState( SID_COMPUTE, R.stCompute );
        vhDispatch( SID_COMPUTE, R.computeGroups );
    }

    // UI draws.
    for ( int i = 0; i < W.uiDraws; i++ )
    {
        auto& s = R.stUI;
        s.ClearAllBindings()
            .SetVertexBuffer( R.vb, 0 ).SetIndexBuffer( R.ib )
            .SetStateFlags( VRHI_STATE_WRITE_MASK | VRHI_STATE_BLEND_ALPHA )
            .SetProgram( R.progUI );
        s.SetTexture( 0, R.glyphTex[i % 4], -1, "glyphTex" )
            .SetSampler( 0, VRHI_SAMPLER_UVW_CLAMP | VRHI_SAMPLER_MIN_LINEAR | VRHI_SAMPLER_MAG_LINEAR, -1, "s0" );
        glm::vec4 colourMul( rng.Next01(), rng.Next01(), rng.Next01(), 1 );
        glm::vec4 uvScroll( rng.Next01() * 0.05f, 0, 0, 0 );
        s.SetUniform( s.uniforms.size(), "u_uiColourMul", &colourMul );
        s.SetUniform( s.uniforms.size(), "u_uiUVScroll", &uvScroll );
        FrameSetGlobalUniforms( s, frame, R.resolution );
        vhSetState( SID_UI, s );
        vhDrawIndexed( SID_UI, 6, 1, 0, 0, 0 );
    }
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

struct FrameBench
{
    int placeholder; // utest fixtures require a struct body
};

UTEST_F_SETUP( FrameBench )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmarks require a real device" ); } // per-test bodies skip on null device
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    const char* gc = getenv( "VRHI_BENCH_GC_INTERVAL" );
    if ( gc && atoi( gc ) > 0 ) g_vhInit.gcFlushInterval = ( uint32_t ) atoi( gc );
    vhBeginMarker( utest_test_name );
    vhFinish();
}

UTEST_F_TEARDOWN( FrameBench )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmarks require a real device" ); }
    vhEndMarker();
    vhFinish();
    if ( g_testInit )
    {
        vhShutdown( g_testInitQuiet );
        g_testInit = false;
    }
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

// Dense frame.
UTEST_F( FrameBench, Bench_Frame_Dense )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmark needs a real device" ); }
    FrameResources R;
    ASSERT_TRUE( R.Init() );

    FrameWorkload W;
    const vhStateId sidBase = 80000;
    const int totalDraws = W.depthDraws + W.litBatches + W.litSingles + W.spriteBatches + W.uiDraws;

    // Warm up PSOs, binding sets and transient buffers.
    for ( int f = 0; f < 32; f++ ) { RunFrameBench( R, W, f, sidBase ); vhFlush( true ); }

    FrameTimer tWhole, tEnqueue;
    for ( int f = 0; f < g_frameBenchConfig.frames; f++ )
    {
        tWhole.Begin();
        tEnqueue.Begin();
        RunFrameBench( R, W, f, sidBase );
        tEnqueue.Sample();
        vhFlush( true ); // wait for the backend to drain
        tWhole.Sample();
    }
    FramePrint( "Frame_Dense_Whole", tWhole, totalDraws, W.computeWaveA + W.computeWaveB );
    FramePrint( "Frame_Dense_EnqueueOnly", tEnqueue, totalDraws, W.computeWaveA + W.computeWaveB );

    R.Shutdown();
}

// Sparse frame.
UTEST_F( FrameBench, Bench_Frame_Sparse )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmark needs a real device" ); }
    FrameResources R;
    ASSERT_TRUE( R.Init() );

    FrameWorkload W;
    const vhStateId sidBase = 81000;
    W.depthDraws = 6;
    W.litBatches = 5;
    W.litSingles = 3;
    W.spriteBatches = 0;
    W.uiDraws = 12;
    const int totalDraws = W.depthDraws + W.litBatches + W.litSingles + W.spriteBatches + W.uiDraws;

    for ( int f = 0; f < 32; f++ ) { RunFrameBench( R, W, f, sidBase ); vhFlush( true ); }

    FrameTimer tWhole;
    for ( int f = 0; f < g_frameBenchConfig.frames; f++ )
    {
        tWhole.Begin();
        RunFrameBench( R, W, f, sidBase );
        vhFlush( true );
        tWhole.Sample();
    }
    FramePrint( "Frame_Sparse_Whole", tWhole, totalDraws, W.computeWaveA + W.computeWaveB );

    R.Shutdown();
}

// Measure the dense frame with a selected GC interval.
UTEST_F( FrameBench, Bench_Frame_GCInterval )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmark needs a real device" ); }
    const char* gcEnv = getenv( "VRHI_BENCH_GC_INTERVAL" );
    if ( !gcEnv ) { UTEST_SKIP( "set VRHI_BENCH_GC_INTERVAL to compare" ); }
    FrameResources R;
    ASSERT_TRUE( R.Init() );

    FrameWorkload W;
    const vhStateId sidBase = 82000;
    const int totalDraws = W.depthDraws + W.litBatches + W.litSingles + W.spriteBatches + W.uiDraws;

    for ( int f = 0; f < 32; f++ ) { RunFrameBench( R, W, f, sidBase ); vhFlush( true ); }

    FrameTimer tWhole;
    for ( int f = 0; f < g_frameBenchConfig.frames; f++ )
    {
        tWhole.Begin();
        RunFrameBench( R, W, f, sidBase );
        vhFlush( true );
        tWhole.Sample();
    }
    char label[64];
    snprintf( label, sizeof( label ), "Frame_GC_%s", gcEnv );
    FramePrint( label, tWhole, totalDraws, W.computeWaveA + W.computeWaveB );

    R.Shutdown();
}

static bool RunHeavyFrameBench( const char* label, int scale )
{
    FrameResources R;
    if ( !R.Init( true ) ) return false;

    FrameWorkload W;
    W.worldRange = 0.5f;
    W.litFlags = VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LEQUAL | VRHI_STATE_DEPTH_TEST_ENABLE;
    W.depthDraws = 200 * scale;
    W.litBatches = 200 * scale;
    W.litSingles = 60 * scale;
    W.spriteBatches = 12 * scale;
    W.uiDraws = 28 * scale;
    const vhStateId sidBase = 85000;
    const int totalDraws = W.depthDraws + W.litBatches + W.litSingles + W.spriteBatches + W.uiDraws;

    for ( int f = 0; f < 32; f++ ) { RunFrameBench( R, W, f, sidBase ); vhFlush( true ); }

    FrameTimer tWhole, tEnqueue, tDrain, tCPU;
    for ( int f = 0; f < g_frameBenchConfig.frames; f++ )
    {
        tWhole.Begin();
        std::clock_t cpuStart = std::clock();
        tEnqueue.Begin();
        RunFrameBench( R, W, f, sidBase );
        tEnqueue.Sample();
        vhFlush( true );
        tWhole.Sample();
        tCPU.samples.push_back( double( std::clock() - cpuStart ) * 1000.0 / CLOCKS_PER_SEC );
    }
    tDrain.Begin();
    vhFinish();
    tDrain.Sample();

    UTEST_PRINTF( "[VRHI_BENCH] %s_CPU avg=%.0f p50=%.0f p95=%.0f p99=%.0f max=%.0f us | per_draw=%.2f us\n",
        label, tCPU.Mean() * 1000, tCPU.Percentile( 50 ) * 1000, tCPU.Percentile( 95 ) * 1000,
        tCPU.Percentile( 99 ) * 1000, tCPU.Percentile( 100 ) * 1000, tCPU.Mean() * 1000 / totalDraws );
    UTEST_PRINTF( "[VRHI_BENCH] %s_Whole target=%dx%d frames=%d draws=%d dispatches=%d | avg=%.0f p50=%.0f p95=%.0f p99=%.0f max=%.0f us\n",
        label, R.resolution.x, R.resolution.y, g_frameBenchConfig.frames, totalDraws, W.computeWaveA + W.computeWaveB,
        tWhole.Mean() * 1000, tWhole.Percentile( 50 ) * 1000, tWhole.Percentile( 95 ) * 1000,
        tWhole.Percentile( 99 ) * 1000, tWhole.Percentile( 100 ) * 1000 );
    UTEST_PRINTF( "[VRHI_BENCH] %s enqueue_avg=%.0f us finish_after_samples=%.0f us\n",
        label, tEnqueue.Mean() * 1000, tDrain.Mean() * 1000 );

    vhMem colour, compute;
    vhReadTextureSlow( R.rt, 0, 0, &colour );
    vhReadTextureSlow( R.computeOut, 0, 0, &compute );
    vhFinish();
    const size_t centre = ( size_t( R.resolution.y / 2 ) * R.resolution.x + R.resolution.x / 2 ) * 4;
    const size_t bytes = size_t( R.resolution.x ) * R.resolution.y * 4;
    R.Shutdown();
    if ( colour.size() != bytes || compute.size() != bytes ) return false;
    auto checksum = []( const vhMem& mem )
    {
        uint64_t hash = 14695981039346656037ULL;
        for ( size_t i = 0; i < mem.size(); i++ ) hash = ( hash ^ mem.data()[i] ) * 1099511628211ULL;
        return hash;
    };
    vhPerfCheck( false );
    UTEST_PRINTF( "[VRHI_BENCH] %s pixels colour=%llu compute=%llu centre=%u,%u,%u / %u\n",
        label, (unsigned long long) checksum( colour ), (unsigned long long) checksum( compute ),
        (unsigned) colour.data()[centre], (unsigned) colour.data()[centre + 1],
        (unsigned) colour.data()[centre + 2], (unsigned) compute.data()[centre] );
    return int( colour.data()[centre] ) + int( colour.data()[centre + 1] ) + int( colour.data()[centre + 2] ) > 0 && compute.data()[centre] > 0;
}

UTEST_F( FrameBench, Bench_Frame_Heavy_500 )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmark needs a real device" ); }
    const char* heavy = getenv( "VRHI_BENCH_HEAVY" );
    if ( !heavy || atoi( heavy ) != 1 ) { UTEST_SKIP( "set VRHI_BENCH_HEAVY=1 to run the 500/1000-draw benchmarks" ); }
    ASSERT_TRUE( RunHeavyFrameBench( "Frame_Heavy_500", 1 ) );
}

UTEST_F( FrameBench, Bench_Frame_Heavy_1000 )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Frame benchmark needs a real device" ); }
    const char* heavy = getenv( "VRHI_BENCH_HEAVY" );
    if ( !heavy || atoi( heavy ) != 1 ) { UTEST_SKIP( "set VRHI_BENCH_HEAVY=1 to run the 500/1000-draw benchmarks" ); }
    ASSERT_TRUE( RunHeavyFrameBench( "Frame_Heavy_1000", 2 ) );
}

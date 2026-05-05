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

#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <functional>
#include <thread>

#include "test.h"
#include <vrhi.h>
#include <glm/gtc/matrix_transform.hpp>

extern bool g_testInit;
extern bool g_testInitQuiet;

// --------------------------------------------------------------------------
// BenchConfig — parse VRHI_BENCH_ITERS once, expose iter counts
// --------------------------------------------------------------------------

struct BenchConfig
{
    int iters      = 2000;
    int warmup     = 32;
    int longIters  = 20000;
    int lightIters = 256;

    BenchConfig()
    {
        const char* env = getenv( "VRHI_BENCH_ITERS" );
        if ( !env ) return;
        if ( strcmp( env, "light" ) == 0 )       { iters = lightIters; warmup = 8; }
        else if ( strcmp( env, "long" ) == 0 )   { iters = longIters;  warmup = 64; }
        else
        {
            int n = atoi( env );
            if ( n > 0 ) { iters = n; warmup = std::min( 64, std::max( 8, n / 64 ) ); }
        }
    }
};
static BenchConfig g_benchConfig;

// --------------------------------------------------------------------------
// BenchRegistry — collect results, print summary at exit
// --------------------------------------------------------------------------

struct BenchRegistry
{
    struct Entry { std::string name; double avgUs; double p50Us; double p99Us; int submissionsPerIter; };
    static std::vector< Entry > s_entries;
    static bool s_registered;

    static void Record( const char* name, double avg, double p50, double p99, int submissionsPerIter )
    {
        s_entries.push_back( { name, avg, p50, p99, submissionsPerIter } );
        if ( !s_registered )
        {
            atexit( PrintSummary );
            s_registered = true;
        }
    }

    static void PrintSummary()
    {
        if ( s_entries.empty() ) return;
        std::sort( s_entries.begin(), s_entries.end(), []( const Entry& a, const Entry& b ) { return a.name < b.name; } );
        UTEST_PRINTF( "\n=================== VRHI BENCHMARK SUMMARY ===================\n" );
        UTEST_PRINTF( "%-48s %10s %10s %10s %12s\n", "name", "avg_us", "p50_us", "p99_us", "us/draw" );
        UTEST_PRINTF( "------------------------------------------------------------------------\n" );
        static const char* kHeadlines[] = { "Bench_Realistic_PerDrawUniform", "Bench_Frame_GBufferPass", "Bench_Frame_FullScene", nullptr };
        for ( auto& e : s_entries )
        {
            bool headline = false;
            for ( int i = 0; kHeadlines[i]; i++ ) if ( e.name == kHeadlines[i] ) { headline = true; break; }
            double usPerDraw = e.submissionsPerIter > 0 ? e.avgUs / e.submissionsPerIter : e.avgUs;
            UTEST_PRINTF( "%-48s %10.2f %10.2f %10.2f %12.2f%s\n",
                e.name.c_str(), e.avgUs, e.p50Us, e.p99Us, usPerDraw, headline ? "  ***" : "" );
        }
        UTEST_PRINTF( "========================================================================\n" );
    }
};

// --------------------------------------------------------------------------
// BenchTimer — per-iter sample collection + statistics
// --------------------------------------------------------------------------

struct BenchTimer
{
    std::vector< double > samples;
    std::chrono::steady_clock::time_point t0;
    double flushWaitUs = 0.0;

    void Begin() { t0 = std::chrono::steady_clock::now(); }

    void Sample()
    {
        auto now = std::chrono::steady_clock::now();
        samples.push_back( std::chrono::duration< double, std::micro >( now - t0 ).count() );
        t0 = now;
    }

    void MeasureFlushWait()
    {
        auto fw0 = std::chrono::steady_clock::now();
        vhFlush( true );
        flushWaitUs = std::chrono::duration< double, std::micro >( std::chrono::steady_clock::now() - fw0 ).count();
    }

    double Percentile( double pct ) const
    {
        if ( samples.empty() ) return 0.0;
        std::vector< double > s = samples;
        std::sort( s.begin(), s.end() );
        int idx = (int)( pct / 100.0 * ( s.size() - 1 ) + 0.5 );
        idx = std::max( 0, std::min( idx, (int)s.size() - 1 ) );
        return s[idx];
    }

    double Mean() const
    {
        if ( samples.empty() ) return 0.0;
        return std::accumulate( samples.begin(), samples.end(), 0.0 ) / samples.size();
    }

    double Min() const { return samples.empty() ? 0.0 : *std::min_element( samples.begin(), samples.end() ); }
    double Max() const { return samples.empty() ? 0.0 : *std::max_element( samples.begin(), samples.end() ); }

    int CountAbove( double thresholdUs ) const
    {
        int n = 0;
        for ( double s : samples ) if ( s > thresholdUs ) n++;
        return n;
    }

    void Print( const char* label, int draws = 1, int dispatches = 0 ) const
    {
        double avg = Mean();
        double p50 = Percentile( 50.0 );
        double p90 = Percentile( 90.0 );
        double p99 = Percentile( 99.0 );
        double p999 = Percentile( 99.9 );
        double mn  = Min();
        double mx  = Max();
        int spikes1ms  = CountAbove( 1000.0 );
        int spikes5ms  = CountAbove( 5000.0 );
        int subs = draws + dispatches;
        double usPerDraw = subs > 0 ? avg / subs : avg;
        UTEST_PRINTF( "[VRHI_BENCH] %-48s iters=%-5d draws=%-5d dispatches=%-3d avg=%8.2fus p50=%8.2fus p90=%8.2fus p99=%8.2fus p999=%8.2fus min=%8.2fus max=%8.2fus spikes>1ms=%d >5ms=%d us/draw=%6.2f flushWait=%.0fus\n",
            label, (int)samples.size(), draws * (int)samples.size(), dispatches * (int)samples.size(),
            avg, p50, p90, p99, p999, mn, mx, spikes1ms, spikes5ms, usPerDraw, flushWaitUs );
        BenchRegistry::Record( label, avg, p50, p99, subs );
    }

    // Prints three labelled lines: frame time, flush-wait time, GPU-finish time.
    static void PrintThree( const char* prefix,
        const BenchTimer& tFrame, const BenchTimer& tFlush, const BenchTimer& tGPU,
        int draws, int dispatches )
    {
        char buf[128];
        snprintf( buf, sizeof(buf), "%s_FrameTime",   prefix ); tFrame.Print( buf, draws, dispatches );
        snprintf( buf, sizeof(buf), "%s_FlushWait",   prefix ); tFlush.Print( buf );
        snprintf( buf, sizeof(buf), "%s_GPUFinish",   prefix ); tGPU.Print( buf );
    }
};
std::vector< BenchRegistry::Entry > BenchRegistry::s_entries;
bool BenchRegistry::s_registered = false;

// --------------------------------------------------------------------------
// Shared shaders
// --------------------------------------------------------------------------

static const char* kBenchVS = R"(
struct VSIn { float3 pos : POSITION; float4 col : COLOUR; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOUR; };
[shader("vertex")] VSOut main( VSIn i ) { VSOut o; o.pos = float4(i.pos,1); o.col = i.col; return o; }
)";

static const char* kBenchPS_Solid = R"(
[shader("pixel")] float4 main( float4 col : COLOUR ) : SV_Target { return col; }
)";

static const char* kBenchPS_Textured = R"(
Texture2D t0 : register(t0, VRHI_STAGE_SPACE);
Texture2D t1 : register(t1, VRHI_STAGE_SPACE);
Texture2D t2 : register(t2, VRHI_STAGE_SPACE);
Texture2D t3 : register(t3, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
cbuffer Material : register(b0, VRHI_STAGE_SPACE) { float4 u_tint; };
[shader("pixel")]
float4 main( float4 col : COLOUR ) : SV_Target
{
    float2 uv = col.xy * 0.5 + 0.5;
    return t0.SampleLevel(s0,uv,0) + t1.SampleLevel(s0,uv,0) + t2.SampleLevel(s0,uv,0) + t3.SampleLevel(s0,uv,0) + u_tint + col;
}
)";

static const char* kBenchPS_GBuffer = R"(
Texture2D t0 : register(t0, VRHI_STAGE_SPACE);
Texture2D t1 : register(t1, VRHI_STAGE_SPACE);
Texture2D t2 : register(t2, VRHI_STAGE_SPACE);
Texture2D t3 : register(t3, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
cbuffer Material : register(b0, VRHI_STAGE_SPACE) { float4 u_tint; };
struct PSOut { float4 alb : SV_Target0; float4 nrm : SV_Target1; float4 orm : SV_Target2; float4 emi : SV_Target3; };
[shader("pixel")]
PSOut main( float4 col : COLOUR )
{
    float2 uv = col.xy * 0.5 + 0.5;
    float4 a = t0.SampleLevel(s0,uv,0) + u_tint;
    float4 n = t1.SampleLevel(s0,uv,0);
    float4 o = t2.SampleLevel(s0,uv,0);
    float4 e = t3.SampleLevel(s0,uv,0) + col;
    PSOut p; p.alb = a; p.nrm = n; p.orm = o; p.emi = e; return p;
}
)";

static const char* kBenchVS_PosOnly = R"(
struct VSIn { float3 pos : POSITION; float4 col : COLOUR; };
[shader("vertex")] float4 main( VSIn i ) : SV_Position { return float4(i.pos,1); }
)";

static const char* kBenchPS_Shadow = R"(
[shader("pixel")] float4 main() : SV_Target { return float4(0,0,0,0); }
)";

static const char* kBenchCS_Noop = R"(
[[vk::image_format("rgba8")]] RWTexture2D<float4> g_Out : register(u0, VRHI_STAGE_SPACE);
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = float4(0,0,0,0); }
)";

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static vhShader BenchCompileShader( const char* src, uint64_t stage, const char* name )
{
    std::vector< uint32_t > spirv; std::string err;
    bool ok = vhCompileShader( name, src, stage | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &err );
    if ( !ok ) { UTEST_PRINTF( "BenchCompileShader FAILED (%s): %s\n", name, err.c_str() ); return VRHI_INVALID_HANDLE; }
    vhShader sh = vhAllocShader();
    vhCreateShader( sh, name, stage, spirv, "main" );
    return sh;
}

static vhTexture BenchMakeTex( int w, int h, nvrhi::Format fmt, uint64_t flags = VRHI_TEXTURE_NONE )
{
    vhTexture t = vhAllocTexture();
    vhCreateTexture2D( t, "BenchTex", glm::ivec2( w, h ), 1, fmt, flags );
    return t;
}

static vhBuffer BenchMakeVB( int numVerts )
{
    vhBuffer b = vhAllocBuffer();
    struct V { glm::vec3 p; glm::vec4 c; };
    vhMem* mem = vhAllocMem( numVerts * sizeof( V ) );
    memset( mem->data(), 0, mem->size() );
    vhCreateVertexBuffer( b, "BenchVB", mem, "float3 float4", numVerts );
    return b;
}

static vhBuffer BenchMakeUB( uint64_t size )
{
    vhBuffer b = vhAllocBuffer(); vhMem* m = vhAllocMem( size ); memset( m->data(), 0, size );
    vhCreateUniformBuffer( b, "BenchUB", m, size );
    return b;
}

// --------------------------------------------------------------------------
// Bench fixture
// --------------------------------------------------------------------------

struct Bench{};

UTEST_F_SETUP( Bench )
{
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhBeginMarker( utest_test_name );
    vhFinish();
}

UTEST_F_TEARDOWN( Bench )
{
    vhEndMarker();
    vhFinish();
    if ( getenv( "VRHI_BENCH_PERFCHECK" ) ) vhPerfCheck();
}

// ==========================================================================
// §A — Atomic submission micro-benchmarks (run in null mode too)
// ==========================================================================

UTEST_F( Bench, Bench_Submit_NoOpDraw )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4( 0,0,4,4 ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70000;
    vhSetState( sid, state );
    for ( int i = 0; i < g_benchConfig.warmup; i++ ) vhDraw( sid, 3 );
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ ) { t.Begin(); vhDraw( sid, 3 ); t.Sample(); }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_NoOpDraw", 1 );

    vhDestroyTexture( rt ); vhDestroyBuffer( vb ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_NoOpDrawIndexed )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 4 );
    vhBuffer ib  = vhAllocBuffer();
    { uint32_t idx[6] = {0,1,2,1,3,2}; vhMem* m = vhAllocMem( sizeof(idx) ); memcpy(m->data(),idx,sizeof(idx)); vhCreateIndexBuffer( ib, "BenchIB", m, 0, VRHI_BUFFER_INDEX32 ); }
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS2" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS2" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4( 0,0,4,4 ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetVertexBuffer( vb, 0 ).SetIndexBuffer( ib )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70001;
    vhSetState( sid, state );
    for ( int i = 0; i < g_benchConfig.warmup; i++ ) vhDrawIndexed( sid, 6 );
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ ) { t.Begin(); vhDrawIndexed( sid, 6 ); t.Sample(); }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_NoOpDrawIndexed", 1 );

    vhDestroyTexture( rt ); vhDestroyBuffer( vb ); vhDestroyBuffer( ib );
    vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_NoOpDispatch )
{
    vhTexture outTex = BenchMakeTex( 8, 8, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    vhShader cs = BenchCompileShader( kBenchCS_Noop, VRHI_SHADER_STAGE_COMPUTE, "BS_CS" );
    ASSERT_NE( cs, VRHI_INVALID_HANDLE );

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name="g_Out", .texture=outTex, .computeUAV=true } );
    vhStateId sid = 70002;
    vhSetState( sid, state );
    for ( int i = 0; i < g_benchConfig.warmup; i++ ) vhDispatch( sid, {1,1,1} );
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ ) { t.Begin(); vhDispatch( sid, {1,1,1} ); t.Sample(); }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_NoOpDispatch", 0, 1 );

    vhDestroyTexture( outTex ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

// ==========================================================================
// §D — Upload micro-benchmarks
// ==========================================================================

UTEST_F( Bench, Bench_Upload_VertexBuffer_1KB )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Upload requires real backend" ); }
    vhBuffer vb = vhAllocBuffer();
    vhMem* init = vhAllocMem( 1024 );
    memset( init->data(), 0, 1024 );
    vhCreateVertexBuffer( vb, "UVB", init, "float3 float4", 0 );
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        vhMem* m = vhAllocMem( 1024 );
        memset( m->data(), i & 0xFF, 1024 );
        t.Begin(); vhUpdateVertexBuffer( vb, m ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Upload_VertexBuffer_1KB" );
    vhDestroyBuffer( vb ); vhFinish();
}

UTEST_F( Bench, Bench_Upload_UniformBuffer_128B )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Upload requires real backend" ); }
    vhBuffer ub = vhAllocBuffer();
    vhMem* init = vhAllocMem( 128 );
    memset( init->data(), 0, 128 );
    vhCreateUniformBuffer( ub, "UUB", init, 128 );
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        vhMem* m = vhAllocMem( 128 );
        memset( m->data(), i & 0xFF, 128 );
        t.Begin(); vhUpdateUniformBuffer( ub, m ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Upload_UniformBuffer_128B" );
    vhDestroyBuffer( ub ); vhFinish();
}

UTEST_F( Bench, Bench_Upload_Texture_64KB )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Upload requires real backend" ); }
    vhTexture tex = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM );
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        vhMem* m = vhAllocMem( 128 * 128 * 4 );
        memset( m->data(), i & 0xFF, m->size() );
        t.Begin(); vhUpdateTexture( tex, 0, 0, 1, 1, m ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Upload_Texture_64KB" );
    vhDestroyTexture( tex ); vhFinish();
}

// ==========================================================================
// §E — Multi-material state churn
//   N distinct (VS+PS) pairs, same total draw count.
// ==========================================================================

static void RunMultiMaterialBench( const char* label, int numMaterials, bool sortedByMaterial )
{
    if ( g_vhInit.nullMode ) return;

    const int kDrawsTotal = 400;
    // Cap iters: 400 draws each is expensive; use at most 200 iters.
    const int kIters = std::min( g_benchConfig.iters, 200 );

    vhTexture rt = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture ds = BenchMakeTex( 64, 64, nvrhi::Format::D32, VRHI_TEXTURE_RT );
    vhBuffer  vb = BenchMakeVB( 64 );
    vhBuffer  ib = vhAllocBuffer();
    {
        uint32_t idx[6]; for ( int i = 0; i < 6; i++ ) idx[i] = (uint32_t)i % 4;
        vhMem* m = vhAllocMem( sizeof(idx) ); memcpy( m->data(), idx, sizeof(idx) );
        vhCreateIndexBuffer( ib, "MMI", m, 0, VRHI_BUFFER_INDEX32 );
    }
    vhTexture texPool[8];
    for ( int i = 0; i < 8; i++ ) texPool[i] = BenchMakeTex( 16, 16, nvrhi::Format::RGBA8_UNORM );
    vhFinish();

    // Two shader variants: Textured (1-RT) and GBuffer (4-MRT) — both share the same
    // kBenchPS_Textured source so bytecode is identical, but different names give different
    // NVRHI shader handles → distinct PSOs and BindingSet layouts.
    vhShader vs   = BenchCompileShader( kBenchVS,          VRHI_SHADER_STAGE_VERTEX, "MM_VS"  );
    vhShader ps0  = BenchCompileShader( kBenchPS_Textured, VRHI_SHADER_STAGE_PIXEL,  "MM_PS0" );
    vhShader ps1  = BenchCompileShader( kBenchPS_GBuffer,  VRHI_SHADER_STAGE_PIXEL,  "MM_PS1" );
    vhFinish();

    // Extra RT slots for GBuffer materials (shared across all GBuffer-type states).
    vhTexture rtExtra[3];
    for ( int j = 0; j < 3; j++ ) rtExtra[j] = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhFinish();

    std::vector< vhStateId > sids( numMaterials );
    std::vector< vhState   > states( numMaterials );
    for ( int m = 0; m < numMaterials; m++ )
    {
        sids[m] = (vhStateId)( 76000 + m );
        vhState& s = states[m];
        bool gbuf = ( m & 1 );
        if ( gbuf )
            s.SetColourAttachment(0,rt).SetColourAttachment(1,rtExtra[0])
             .SetColourAttachment(2,rtExtra[1]).SetColourAttachment(3,rtExtra[2])
             .SetDepthAttachment(ds);
        else
            s.SetColourAttachment(0,rt).SetDepthAttachment(ds);
        s.SetViewRect( glm::vec4(0,0,64,64) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK | VRHI_STATE_DEPTH_TEST_LESS | VRHI_STATE_DEPTH_TEST_ENABLE )
         .SetVertexBuffer(vb,0).SetIndexBuffer(ib)
         .SetProgram( vhCreateGfxProgram( vs, gbuf ? ps1 : ps0 ) )
         .SetTexture(0,{.name="t0",.texture=texPool[0]})
         .SetTexture(1,{.name="t1",.texture=texPool[1]})
         .SetTexture(2,{.name="t2",.texture=texPool[2]})
         .SetTexture(3,{.name="t3",.texture=texPool[3]})
         .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP});
        vhSetState( sids[m], s );
    }
    vhFinish();

    const int kWarmup = std::min( g_benchConfig.warmup, 4 );
    for ( int w = 0; w < kWarmup; w++ )
    {
        for ( int d = 0; d < kDrawsTotal; d++ )
        {
            int mi = d % numMaterials;
            glm::mat4 mtx(1.0f); mtx[3][0] = float(d)*0.001f;
            glm::vec4 u( float(d)*0.001f, 0, 0, 1 );
            states[mi].SetWorldTransform(mtx).SetUniform(0,"Material",&u,1);
            vhSetState( sids[mi], states[mi] ); vhDrawIndexed( sids[mi], 6 );
        }
        vhFlush( true );
    }

    auto submitDraw = [&]( int mi, int d, int iter )
    {
        int tb = (d + iter) % 4;
        glm::mat4 mtx(1.0f); mtx[3][0] = float(iter*kDrawsTotal+d)*0.00001f;
        glm::vec4 u( float(iter*kDrawsTotal+d)*0.001f, 0, 0, 1 );
        states[mi].SetWorldTransform(mtx).SetUniform(0,"Material",&u,1)
                  .SetTexture(0,{.name="t0",.texture=texPool[tb+0]})
                  .SetTexture(1,{.name="t1",.texture=texPool[tb+1]})
                  .SetTexture(2,{.name="t2",.texture=texPool[tb+2]})
                  .SetTexture(3,{.name="t3",.texture=texPool[tb+3]});
        vhSetState( sids[mi], states[mi] ); vhDrawIndexed( sids[mi], 6 );
    };

    BenchTimer t;
    for ( int iter = 0; iter < kIters; iter++ )
    {
        t.Begin();
        if ( sortedByMaterial )
        {
            const int drawsPerMat = kDrawsTotal / numMaterials;
            for ( int mi = 0; mi < numMaterials; mi++ )
                for ( int j = 0; j < drawsPerMat; j++ )
                    submitDraw( mi, mi * drawsPerMat + j, iter );
        }
        else
        {
            for ( int d = 0; d < kDrawsTotal; d++ )
                submitDraw( d % numMaterials, d, iter );
        }
        t.Sample();
        vhFlush( true );  // drain per-iter so command buffer stays bounded
    }
    t.Print( label, kDrawsTotal );

    for ( int m = 0; m < numMaterials; m++ ) vhSetState(sids[m],g_state0,VRHI_DIRTY_ALL);
    vhDestroyShader(vs); vhDestroyShader(ps0); vhDestroyShader(ps1);
    vhDestroyTexture(rt); vhDestroyTexture(ds);
    for ( int j = 0; j < 3; j++ ) vhDestroyTexture(rtExtra[j]);
    vhDestroyBuffer(vb); vhDestroyBuffer(ib);
    for ( int i = 0; i < 8; i++ ) vhDestroyTexture(texPool[i]);
    vhFinish();
}

UTEST_F( Bench, Bench_MultiMaterial_1_unsorted  ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_1mat_400draws_unsorted",   1, false); }
UTEST_F( Bench, Bench_MultiMaterial_4_unsorted  ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_4mat_400draws_unsorted",   4, false); }
UTEST_F( Bench, Bench_MultiMaterial_8_unsorted  ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_8mat_400draws_unsorted",   8, false); }
UTEST_F( Bench, Bench_MultiMaterial_16_unsorted ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_16mat_400draws_unsorted", 16, false); }
UTEST_F( Bench, Bench_MultiMaterial_1_sorted    ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_1mat_400draws_sorted",     1, true ); }
UTEST_F( Bench, Bench_MultiMaterial_4_sorted    ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_4mat_400draws_sorted",     4, true ); }
UTEST_F( Bench, Bench_MultiMaterial_8_sorted    ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_8mat_400draws_sorted",     8, true ); }
UTEST_F( Bench, Bench_MultiMaterial_16_sorted   ) { if(g_vhInit.nullMode){UTEST_SKIP("GPU required");} RunMultiMaterialBench("Bench_MultiMaterial_16mat_400draws_sorted",   16, true ); }

// ==========================================================================
// §F — Full frame cycle
//   Shadow → depth pre-pass → GBuffer (lit + unlit) → compute light-cull →
//   forward transparent → vhFrame().
//   Per frame: 4 dynamic VB uploads, 2 texture uploads, 1 instance-buf upload.
//   Three timers recorded per frame:
//     FrameTime — wall time of the frame body up to vhFrame() return
//     FlushWait — vhFlush(true): backend drain (no GPU wait)
//     GPUFinish — vhFinish(): full GPU idle
// ==========================================================================

static const char* kFF_PS_Lit = R"(
Texture2D t_alb : register(t0, VRHI_STAGE_SPACE);
Texture2D t_nrm : register(t1, VRHI_STAGE_SPACE);
Texture2D t_orm : register(t2, VRHI_STAGE_SPACE);
Texture2D t_emi : register(t3, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
cbuffer Material : register(b0, VRHI_STAGE_SPACE) { float4 u_data[8]; };
struct PSOut { float4 alb:SV_Target0; float4 nrm:SV_Target1; float4 orm:SV_Target2; float4 emi:SV_Target3; };
[shader("pixel")]
PSOut main( float4 col : COLOUR )
{
    float2 uv = col.xy * 0.5 + 0.5;
    PSOut p;
    p.alb = t_alb.SampleLevel(s0,uv,0) + u_data[0];
    p.nrm = t_nrm.SampleLevel(s0,uv,0) + u_data[1];
    p.orm = t_orm.SampleLevel(s0,uv,0) + u_data[2];
    p.emi = t_emi.SampleLevel(s0,uv,0) + u_data[3] + col;
    return p;
}
)";

static const char* kFF_PS_Unlit = R"(
Texture2D t_alb : register(t0, VRHI_STAGE_SPACE);
SamplerState s0 : register(s0, VRHI_STAGE_SPACE);
cbuffer Material : register(b0, VRHI_STAGE_SPACE) { float4 u_data[8]; };
[shader("pixel")]
float4 main( float4 col : COLOUR ) : SV_Target
{
    float2 uv = col.xy * 0.5 + 0.5;
    return t_alb.SampleLevel(s0,uv,0) + u_data[0] + col;
}
)";

static const char* kFF_CS_LightCull = R"(
StructuredBuffer<float4>  g_Lights  : register(t0, VRHI_STAGE_SPACE);
[[vk::image_format("rgba8")]] RWTexture2D<float4> g_TileOut : register(u0, VRHI_STAGE_SPACE);
[numthreads(8,8,1)]
void main( uint3 id : SV_DispatchThreadID )
{
    float4 acc = float4(0,0,0,0);
    for ( int i = 0; i < 8; i++ ) acc += g_Lights[i];
    g_TileOut[id.xy] = acc;
}
)";

static void RunFullCycleBench( const char* label, bool sortedGBuffer )
{
    const int kFrames          = std::min( 64, g_benchConfig.iters );
    const int kShadowDraws     = 100;
    const int kDepthDraws      = 80;
    const int kGBufferDraws    = 200;
    const int kForwardDraws    = 30;
    const int kLightDispatches = 8;
    const int kNumMeshes       = 16;
    const int kNumTex          = 24;
    const int kDynVBCount      = 4;
    const int kDynTexCount     = 2;
    const int kVBVerts         = 32;
    const int kTexW = 128, kTexH = 128;
    const int kInstBufSize     = 64 * 1024;

    vhTexture rtAlb  = BenchMakeTex(128,128,nvrhi::Format::RGBA8_UNORM,  VRHI_TEXTURE_RT);
    vhTexture rtNrm  = BenchMakeTex(128,128,nvrhi::Format::RGBA8_UNORM,  VRHI_TEXTURE_RT);
    vhTexture rtOrm  = BenchMakeTex(128,128,nvrhi::Format::RGBA8_UNORM,  VRHI_TEXTURE_RT);
    vhTexture rtEmi  = BenchMakeTex(128,128,nvrhi::Format::RGBA8_UNORM,  VRHI_TEXTURE_RT);
    vhTexture rtFwd  = BenchMakeTex(128,128,nvrhi::Format::RGBA16_FLOAT, VRHI_TEXTURE_RT);
    vhTexture rtDS   = BenchMakeTex(128,128,nvrhi::Format::D32,          VRHI_TEXTURE_RT);
    vhTexture tileUAV= BenchMakeTex(16, 16, nvrhi::Format::RGBA8_UNORM,  VRHI_TEXTURE_COMPUTE_WRITE);

    vhTexture texPool[kNumTex];
    for ( int i = 0; i < kNumTex; i++ ) texPool[i] = BenchMakeTex(kTexW,kTexH,nvrhi::Format::RGBA8_UNORM);

    vhBuffer meshVB[kNumMeshes];
    for ( int i = 0; i < kNumMeshes; i++ ) meshVB[i] = BenchMakeVB(kVBVerts);
    vhBuffer ib = vhAllocBuffer();
    {
        uint32_t idx[6]; for(int i=0;i<6;i++) idx[i]=(uint32_t)i%4;
        vhMem* m = vhAllocMem(sizeof(idx)); memcpy(m->data(),idx,sizeof(idx));
        vhCreateIndexBuffer(ib,"FFI",m,0,VRHI_BUFFER_INDEX32);
    }

    vhBuffer instBufs[3];
    for ( int i = 0; i < 3; i++ )
    {
        instBufs[i] = vhAllocBuffer();
        vhMem* m = vhAllocMem(kInstBufSize); memset(m->data(),0,kInstBufSize);
        vhCreateStorageStructuredBuffer(instBufs[i],"Inst",m,kInstBufSize,16);
    }
    vhTransientAllocator instAlloc;
    instAlloc.Init(instBufs,kInstBufSize);

    vhBuffer lightBuf = vhAllocBuffer();
    {
        vhMem* m = vhAllocMem(8*16); memset(m->data(),0,m->size());
        vhCreateStorageStructuredBuffer(lightBuf,"Lights",m,8*16,16,VRHI_BUFFER_COMPUTE_READ);
    }

    vhFinish();

    vhShader vs_mesh  = BenchCompileShader(kBenchVS,         VRHI_SHADER_STAGE_VERTEX,  "FF_VMesh" );
    vhShader vs_depth = BenchCompileShader(kBenchVS_PosOnly, VRHI_SHADER_STAGE_VERTEX,  "FF_VDepth");
    vhShader ps_lit   = BenchCompileShader(kFF_PS_Lit,       VRHI_SHADER_STAGE_PIXEL,   "FF_PLit"  );
    vhShader ps_unlit = BenchCompileShader(kFF_PS_Unlit,     VRHI_SHADER_STAGE_PIXEL,   "FF_PUnlit");
    vhShader ps_shd   = BenchCompileShader(kBenchPS_Shadow,  VRHI_SHADER_STAGE_PIXEL,   "FF_PSh"   );
    vhShader cs_light = BenchCompileShader(kFF_CS_LightCull, VRHI_SHADER_STAGE_COMPUTE, "FF_CSL"   );
    if ( vs_mesh == VRHI_INVALID_HANDLE || ps_lit == VRHI_INVALID_HANDLE
      || ps_unlit == VRHI_INVALID_HANDLE || cs_light == VRHI_INVALID_HANDLE )
    {
        UTEST_PRINTF( "FullCycle: shader compile failed\n" );
        return;
    }
    vhFinish();

    vhState sShadow;
    sShadow.SetDepthAttachment(rtDS).SetViewRect(glm::vec4(0,0,128,128))
           .SetViewClear(VRHI_CLEAR_DEPTH,glm::vec4(0),1.0f)
           .SetStateFlags(VRHI_STATE_WRITE_Z|VRHI_STATE_DEPTH_TEST_LESS|VRHI_STATE_DEPTH_TEST_ENABLE)
           .SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_depth,ps_shd));
    const vhStateId sidShadow = 77000;
    vhSetState(sidShadow,sShadow);

    vhState sDepth;
    sDepth.SetDepthAttachment(rtDS).SetViewRect(glm::vec4(0,0,128,128))
          .SetViewClear(VRHI_CLEAR_DEPTH,glm::vec4(0),1.0f)
          .SetStateFlags(VRHI_STATE_WRITE_Z|VRHI_STATE_DEPTH_TEST_LESS|VRHI_STATE_DEPTH_TEST_ENABLE)
          .SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_depth,ps_shd));
    const vhStateId sidDepth = 77001;
    vhSetState(sidDepth,sDepth);

    vhState sLit;
    sLit.SetColourAttachment(0,rtAlb).SetColourAttachment(1,rtNrm)
        .SetColourAttachment(2,rtOrm).SetColourAttachment(3,rtEmi)
        .SetDepthAttachment(rtDS).SetViewRect(glm::vec4(0,0,128,128))
        .SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0))
        .SetStateFlags(VRHI_STATE_WRITE_MASK|VRHI_STATE_DEPTH_TEST_LEQUAL|VRHI_STATE_DEPTH_TEST_ENABLE)
        .SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_lit))
        .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP});
    const vhStateId sidLit = 77002;
    vhSetState(sidLit,sLit);

    vhState sUnlit;
    sUnlit.SetColourAttachment(0,rtAlb).SetDepthAttachment(rtDS)
          .SetViewRect(glm::vec4(0,0,128,128))
          .SetStateFlags(VRHI_STATE_WRITE_MASK|VRHI_STATE_DEPTH_TEST_LEQUAL|VRHI_STATE_DEPTH_TEST_ENABLE)
          .SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_unlit))
          .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP});
    const vhStateId sidUnlit = 77003;
    vhSetState(sidUnlit,sUnlit);

    vhState sLight = g_state0;
    sLight.SetProgram(vhCreateComputeProgram(cs_light))
          .SetBuffer(0,{.name="g_Lights",.buffer=lightBuf})
          .SetTexture(0,{.name="g_TileOut",.texture=tileUAV,.computeUAV=true});
    const vhStateId sidLight = 77004;
    vhSetState(sidLight,sLight);

    vhState sFwd;
    sFwd.SetColourAttachment(0,rtFwd).SetDepthAttachment(rtDS)
        .SetViewRect(glm::vec4(0,0,128,128))
        .SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0))
        .SetStateFlags(VRHI_STATE_WRITE_RGB|VRHI_STATE_WRITE_A|VRHI_STATE_BLEND_ALPHA|
                       VRHI_STATE_DEPTH_TEST_LEQUAL|VRHI_STATE_DEPTH_TEST_ENABLE)
        .SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_unlit))
        .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP});
    const vhStateId sidFwd = 77005;
    vhSetState(sidFwd,sFwd);
    vhFinish();

    const glm::vec3 kUp(0,1,0);

    for ( int w = 0; w < g_benchConfig.warmup; w++ )
    {
        vhFrame();
        instAlloc.Reset(); instAlloc.Step();
    }

    BenchTimer tFrame, tFlush, tGPU;

    for ( int frame = 0; frame < kFrames; frame++ )
    {
        tFrame.Begin();
        vhFrame();

        instAlloc.Reset();
        instAlloc.Step();

        // dynamic VB uploads
        for ( int v = 0; v < kDynVBCount; v++ )
        {
            int vi = (frame + v) % kNumMeshes;
            vhMem* m = vhAllocMem(kVBVerts * sizeof(float)*7);
            uint8_t fill = (uint8_t)((frame*kDynVBCount+v) & 0xFF);
            memset(m->data(), fill, m->size());
            vhUpdateVertexBuffer(meshVB[vi], m);
        }

        // streaming texture uploads
        for ( int tx = 0; tx < kDynTexCount; tx++ )
        {
            int ti = (frame + tx) % kNumTex;
            vhMem* m = vhAllocMem(kTexW*kTexH*4);
            memset(m->data(), (frame*kDynTexCount+tx)&0xFF, m->size());
            vhUpdateTexture(texPool[ti], 0, 0, 1, 1, m);
        }

        // instance storage upload
        {
            const int kTotalD = kShadowDraws+kDepthDraws+kGBufferDraws+kForwardDraws;
            const int kInstStride = 128;
            int64_t off = instAlloc.Alloc(kTotalD * kInstStride);
            if ( off >= 0 )
            {
                vhMem* m = vhAllocMem(kTotalD * kInstStride);
                uint64_t* p = (uint64_t*)m->data();
                for ( int d = 0; d < kTotalD * kInstStride / 8; d++ )
                    p[d] = (uint64_t)(frame*kTotalD+d) * 0x9E3779B97F4A7C15ULL;
                vhUpdateStorageBuffer(instAlloc.GetBuffer(), m, (uint64_t)off);
            }
        }

        // shadow pass
        {
            glm::mat4 lv = glm::lookAt(glm::vec3(2,4,2)+glm::vec3(frame*0.001f,0,0),glm::vec3(0),kUp);
            glm::mat4 lp = glm::ortho(-5.0f,5.0f,-5.0f,5.0f,0.1f,20.0f);
            sShadow.SetViewTransform(lv,lp);
            vhSetState(sidShadow,sShadow);
            vhClear(sidShadow,VRHI_CLEAR_DEPTH);
            for ( int d = 0; d < kShadowDraws; d++ )
            {
                glm::mat4 mtx(1.0f); mtx[3][0] = float(d)*0.01f + float(frame)*0.0001f;
                sShadow.SetVertexBuffer(meshVB[d%kNumMeshes],0).SetWorldTransform(mtx);
                vhSetState(sidShadow,sShadow); vhDrawIndexed(sidShadow,6);
            }
        }

        // depth pre-pass
        {
            glm::mat4 cv = glm::lookAt(glm::vec3(0,2,5)+glm::vec3(frame*0.0001f,0,0),glm::vec3(0),kUp);
            glm::mat4 cp = glm::perspective(glm::radians(60.0f),1.0f,0.1f,100.0f);
            sDepth.SetViewTransform(cv,cp);
            vhSetState(sidDepth,sDepth);
            vhClear(sidDepth,VRHI_CLEAR_DEPTH);
            for ( int d = 0; d < kDepthDraws; d++ )
            {
                float a = float(d+frame)*0.01f;
                glm::mat4 mtx(1.0f); mtx[0][0]=cosf(a); mtx[0][2]=sinf(a); mtx[2][0]=-sinf(a); mtx[2][2]=cosf(a);
                sDepth.SetVertexBuffer(meshVB[d%kNumMeshes],0).SetWorldTransform(mtx);
                vhSetState(sidDepth,sDepth); vhDrawIndexed(sidDepth,6);
            }
        }

        // GBuffer pass. When sortedGBuffer, draws are grouped by material (lit then unlit)
        // to avoid render-encoder churn. When unsorted, lit/unlit interleave per draw —
        // realistic worst case for a naive engine that does no draw sorting.
        {
            glm::mat4 cv = glm::lookAt(glm::vec3(0,2,5)+glm::vec3(frame*0.0001f,0,0),glm::vec3(0),kUp);
            glm::mat4 cp = glm::perspective(glm::radians(60.0f),1.0f,0.1f,100.0f);
            sLit.SetViewTransform(cv,cp); sUnlit.SetViewTransform(cv,cp);
            vhSetState(sidLit,sLit); vhClear(sidLit,VRHI_CLEAR_COLOR);
            vhSetState(sidUnlit,sUnlit);
            auto submit = [&]( int d, bool isLit )
            {
                float a = float(d+frame)*0.007f;
                glm::mat4 mtx(1.0f); mtx[3] = glm::vec4(cosf(a)*2.0f,0.0f,sinf(a)*2.0f,1.0f);
                vhState& s = isLit ? sLit : sUnlit;
                vhStateId sid = isLit ? sidLit : sidUnlit;
                int tb = (d + frame) % (kNumTex - 4);
                s.SetTexture(0,{.name="t_alb",.texture=texPool[tb+0]});
                if ( isLit )
                {
                    s.SetTexture(1,{.name="t_nrm",.texture=texPool[tb+1]});
                    s.SetTexture(2,{.name="t_orm",.texture=texPool[tb+2]});
                    s.SetTexture(3,{.name="t_emi",.texture=texPool[tb+3]});
                }
                s.SetVertexBuffer(meshVB[d%kNumMeshes],0).SetWorldTransform(mtx);
                glm::vec4 md[8];
                for(int k=0;k<8;k++) md[k]=glm::vec4(float(d*8+k+frame)*0.001f,0,0,1);
                s.SetUniform(0,"Material",md,8);
                vhSetState(sid,s); vhDrawIndexed(sid,6);
            };
            if ( sortedGBuffer )
            {
                for ( int d = 0; d < kGBufferDraws; d++ ) if ( d % 3 != 0 ) submit( d, true  );
                for ( int d = 0; d < kGBufferDraws; d++ ) if ( d % 3 == 0 ) submit( d, false );
            }
            else
            {
                for ( int d = 0; d < kGBufferDraws; d++ ) submit( d, d % 3 != 0 );
            }
        }

        // compute light cull
        for ( int c = 0; c < kLightDispatches; c++ ) vhDispatch(sidLight,{2,2,1});

        // forward transparent pass
        {
            glm::mat4 cv = glm::lookAt(glm::vec3(0,2,5)+glm::vec3(frame*0.0001f,0,0),glm::vec3(0),kUp);
            glm::mat4 cp = glm::perspective(glm::radians(60.0f),1.0f,0.1f,100.0f);
            sFwd.SetViewTransform(cv,cp);
            vhSetState(sidFwd,sFwd); vhClear(sidFwd,VRHI_CLEAR_COLOR);
            for ( int d = 0; d < kForwardDraws; d++ )
            {
                glm::mat4 mtx(1.0f); mtx[3]=glm::vec4(float(d)*0.1f,0.5f,float(frame)*0.001f,1.0f);
                int tb = (d+frame+8) % (kNumTex-1);
                sFwd.SetTexture(0,{.name="t_alb",.texture=texPool[tb]});
                sFwd.SetVertexBuffer(meshVB[d%kNumMeshes],0).SetWorldTransform(mtx);
                glm::vec4 md[8];
                for(int k=0;k<8;k++) md[k]=glm::vec4(float(d*8+k+frame)*0.001f,0,0,1);
                sFwd.SetUniform(0,"Material",md,8);
                vhSetState(sidFwd,sFwd); vhDrawIndexed(sidFwd,6);
            }
        }

        tFrame.Sample();

        tFlush.Begin(); vhFlush(true);   tFlush.Sample();
        tGPU.Begin();   vhFinish();      tGPU.Sample();
    }

    const int kTotalDraws = kShadowDraws+kDepthDraws+kGBufferDraws+kForwardDraws;
    BenchTimer::PrintThree(label,tFrame,tFlush,tGPU,kTotalDraws,kLightDispatches);

    vhSetState(sidShadow,g_state0,VRHI_DIRTY_ALL); vhSetState(sidDepth,g_state0,VRHI_DIRTY_ALL);
    vhSetState(sidLit,   g_state0,VRHI_DIRTY_ALL); vhSetState(sidUnlit,g_state0,VRHI_DIRTY_ALL);
    vhSetState(sidLight, g_state0,VRHI_DIRTY_ALL); vhSetState(sidFwd,  g_state0,VRHI_DIRTY_ALL);
    vhDestroyTexture(rtAlb); vhDestroyTexture(rtNrm); vhDestroyTexture(rtOrm); vhDestroyTexture(rtEmi);
    vhDestroyTexture(rtFwd); vhDestroyTexture(rtDS);  vhDestroyTexture(tileUAV);
    for(int i=0;i<kNumTex;   i++) vhDestroyTexture(texPool[i]);
    for(int i=0;i<kNumMeshes;i++) vhDestroyBuffer(meshVB[i]);
    vhDestroyBuffer(ib);
    for(int i=0;i<3;i++) vhDestroyBuffer(instBufs[i]);
    vhDestroyBuffer(lightBuf);
    vhDestroyShader(vs_mesh); vhDestroyShader(vs_depth);
    vhDestroyShader(ps_lit);  vhDestroyShader(ps_unlit);
    vhDestroyShader(ps_shd);  vhDestroyShader(cs_light);
    vhFinish();
}

UTEST_F( Bench, Bench_Frame_FullCycle_Unsorted )
{
    if ( g_vhInit.nullMode )      { UTEST_SKIP( "GPU required" ); }
    if ( TestIsSoftwareVulkan() ) { UTEST_SKIP( "Skipped on software Vulkan" ); }
    RunFullCycleBench( "Bench_Frame_FullCycle_Unsorted", false );
}

UTEST_F( Bench, Bench_Frame_FullCycle_Sorted )
{
    if ( g_vhInit.nullMode )      { UTEST_SKIP( "GPU required" ); }
    if ( TestIsSoftwareVulkan() ) { UTEST_SKIP( "Skipped on software Vulkan" ); }
    RunFullCycleBench( "Bench_Frame_FullCycle_Sorted", true );
}

UTEST_F( Bench, Bench_Submit_StateClean )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS3" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS3" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70003;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        state.dirty = 0;  // Force clean — vhSetState early-returns
        t.Begin(); vhSetState( sid, state ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateClean" );

    vhDestroyTexture( rt ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateAllDirty )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS4" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS4" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70004;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        t.Begin(); vhSetState( sid, state, VRHI_DIRTY_ALL ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateAllDirty", 1 );

    vhDestroyTexture( rt ); vhDestroyBuffer( vb ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateDirty_Viewport )
{
    vhTexture rt = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS5" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS5" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,64,64) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70005;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        float w = 32.0f + ( i & 31 );
        state.SetViewRect( glm::vec4( 0, 0, w, w ) );
        t.Begin(); vhSetState( sid, state ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateDirty_Viewport", 1 );

    vhDestroyTexture( rt ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateDirty_TextureSamplers_4tex )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Textures require GPU in null mode" ); }
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture texPool[8];
    for ( int i = 0; i < 8; i++ ) texPool[i] = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM );
    vhFinish();
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS6" );
    vhShader ps = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS6" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70010;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        int base = i & 3;
        state.SetTexture( 0, { .texture=texPool[base+0] } ).SetTexture( 1, { .texture=texPool[base+1] } )
             .SetTexture( 2, { .texture=texPool[base+2] } ).SetTexture( 3, { .texture=texPool[base+3] } );
        t.Begin(); vhSetState( sid, state ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateDirty_TextureSamplers_4tex", 1 );

    vhDestroyTexture( rt );
    for ( int i = 0; i < 8; i++ ) vhDestroyTexture( texPool[i] );
    vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateDirty_Buffers_4 )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer bufs[4];
    for ( int i = 0; i < 4; i++ ) bufs[i] = BenchMakeUB( 64 );
    vhFinish();
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS7" );
    vhShader ps = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS7" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70011;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        for ( int j = 0; j < 4; j++ ) state.SetBuffer( j, { .buffer=bufs[(i+j)&3] } );
        t.Begin(); vhSetState( sid, state ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateDirty_Buffers_4", 1 );

    vhDestroyTexture( rt );
    for ( int i = 0; i < 4; i++ ) vhDestroyBuffer( bufs[i] );
    vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateDirty_PushConstants )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS8" );
    vhShader ps = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS8" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70012;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        state.SetPushConstants( glm::vec4( float(i), 0, 0, 0 ) );
        t.Begin(); vhSetState( sid, state ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateDirty_PushConstants", 1 );

    vhDestroyTexture( rt ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateDirty_Program )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Program switch needs GPU for PSO" ); }
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS9" );
    vhShader ps0 = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS9a" );
    vhShader ps1 = BenchCompileShader( kBenchPS_Shadow, VRHI_SHADER_STAGE_PIXEL, "BS_PSS9b" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );
    vhFinish();

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetVertexBuffer( vb, 0 )
         .SetProgram( vhCreateGfxProgram( vs, ps0 ) );
    vhStateId sid = 70013;
    vhSetState( sid, state );
    for ( int i = 0; i < g_benchConfig.warmup; i++ ) { state.SetProgram( vhCreateGfxProgram( vs, i&1 ? ps1 : ps0 ) ); vhSetState( sid, state ); vhDraw( sid, 3 ); }
    vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        state.SetProgram( vhCreateGfxProgram( vs, i&1 ? ps1 : ps0 ) );
        t.Begin(); vhSetState( sid, state ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateDirty_Program", 1 );

    vhDestroyTexture( rt ); vhDestroyBuffer( vb );
    vhDestroyShader( vs ); vhDestroyShader( ps0 ); vhDestroyShader( ps1 );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

UTEST_F( Bench, Bench_Submit_StateDirty_WorldMatrix )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BS_VS10" );
    vhShader ps = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BS_PSS10" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4(0,0,4,4) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK ).SetProgram( vhCreateGfxProgram( vs, ps ) );
    vhStateId sid = 70014;
    vhSetState( sid, state ); vhFinish();

    BenchTimer t;
    for ( int i = 0; i < g_benchConfig.iters; i++ )
    {
        float angle = float( i ) * 0.01f;
        glm::mat4 m = glm::mat4( 1.0f );
        m[0][0] = cosf( angle ); m[0][1] = sinf( angle );
        m[1][0] = -sinf( angle ); m[1][1] = cosf( angle );
        state.SetWorldTransform( m, 4 );
        t.Begin(); vhSetState( sid, state ); vhDraw( sid, 3 ); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Submit_StateDirty_WorldMatrix", 1 );

    vhDestroyTexture( rt ); vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}

// ==========================================================================
// §B — Realistic per-draw workloads
// ==========================================================================

UTEST_F( Bench, Bench_Realistic_PerDrawUniform )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Uniform update needs GPU resource" ); }
    vhTexture rt = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 4 );
    vhBuffer ib  = vhAllocBuffer();
    { uint32_t idx[6]={0,1,2,1,3,2}; vhMem* m=vhAllocMem(sizeof(idx)); memcpy(m->data(),idx,sizeof(idx)); vhCreateIndexBuffer(ib,"BIB",m,0,VRHI_BUFFER_INDEX32); }
    vhTexture texPool[4];
    for ( int i=0;i<4;i++) texPool[i] = BenchMakeTex(64,64,nvrhi::Format::RGBA8_UNORM);
    vhFinish();

    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BR_VS" );
    vhShader ps = BenchCompileShader( kBenchPS_Textured, VRHI_SHADER_STAGE_PIXEL, "BR_PST" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment(0,rt).SetViewRect(glm::vec4(0,0,64,64))
         .SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0)).SetStateFlags(VRHI_STATE_WRITE_MASK)
         .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs,ps))
         .SetTexture(0,{.name="t0",.texture=texPool[0]}).SetTexture(1,{.name="t1",.texture=texPool[1]})
         .SetTexture(2,{.name="t2",.texture=texPool[2]}).SetTexture(3,{.name="t3",.texture=texPool[3]})
         .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP});
    vhStateId sid = 71000;
    vhSetState(sid,state); vhClear(sid,VRHI_CLEAR_COLOR);
    for ( int i=0;i<g_benchConfig.warmup;i++ )
    {
        glm::vec4 u( float(i)*0.001f,0,0,1 );
        state.SetUniform(0,"Material",&u,1); vhSetState(sid,state); vhDrawIndexed(sid,6);
    }
    vhFinish();

    BenchTimer t;
    for ( int i=0;i<g_benchConfig.iters;i++ )
    {
        glm::vec4 u( float(i)*0.001f,0,0,1 );
        state.SetUniform(0,"Material",&u,1);
        t.Begin(); vhSetState(sid,state); vhDrawIndexed(sid,6); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Realistic_PerDrawUniform", 1 );

    vhDestroyTexture(rt); vhDestroyBuffer(vb); vhDestroyBuffer(ib);
    for(int i=0;i<4;i++) vhDestroyTexture(texPool[i]);
    vhDestroyShader(vs); vhDestroyShader(ps);
    vhSetState(sid,g_state0,VRHI_DIRTY_ALL); vhFinish();
}

UTEST_F( Bench, Bench_Realistic_PerDrawPushConstants )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BPC_VS" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BPC_PS" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment(0,rt).SetViewRect(glm::vec4(0,0,4,4))
         .SetStateFlags(VRHI_STATE_WRITE_MASK).SetVertexBuffer(vb,0)
         .SetProgram(vhCreateGfxProgram(vs,ps));
    vhStateId sid = 71001;
    vhSetState(sid,state); vhFinish();

    BenchTimer t;
    for ( int i=0;i<g_benchConfig.iters;i++ )
    {
        state.SetPushConstants(glm::vec4(float(i)*0.001f,0,0,1));
        t.Begin(); vhSetState(sid,state); vhDraw(sid,3); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Realistic_PerDrawPushConstants", 1 );

    vhDestroyTexture(rt); vhDestroyBuffer(vb); vhDestroyShader(vs); vhDestroyShader(ps);
    vhSetState(sid,g_state0,VRHI_DIRTY_ALL); vhFinish();
}

UTEST_F( Bench, Bench_Realistic_PerDrawWorldMatrix )
{
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhShader vs  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BWM_VS" );
    vhShader ps  = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BWM_PS" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment(0,rt).SetViewRect(glm::vec4(0,0,4,4))
         .SetStateFlags(VRHI_STATE_WRITE_MASK).SetVertexBuffer(vb,0)
         .SetProgram(vhCreateGfxProgram(vs,ps)).SetWorldTransform(glm::mat4(1));
    vhStateId sid = 71002;
    vhSetState(sid,state); vhFinish();

    BenchTimer t;
    for ( int i=0;i<g_benchConfig.iters;i++ )
    {
        float a = float(i)*0.01f;
        glm::mat4 m(1); m[0][0]=cosf(a); m[0][1]=sinf(a); m[1][0]=-sinf(a); m[1][1]=cosf(a);
        state.SetWorldTransform(m);
        t.Begin(); vhSetState(sid,state); vhDraw(sid,3); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Realistic_PerDrawWorldMatrix", 1 );

    vhDestroyTexture(rt); vhDestroyBuffer(vb); vhDestroyShader(vs); vhDestroyShader(ps);
    vhSetState(sid,g_state0,VRHI_DIRTY_ALL); vhFinish();
}

UTEST_F( Bench, Bench_Realistic_TextureRebind4 )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Texture rebind needs GPU" ); }
    vhTexture rt = BenchMakeTex( 4, 4, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhTexture pool[8];
    for (int i=0;i<8;i++) pool[i] = BenchMakeTex(4,4,nvrhi::Format::RGBA8_UNORM);
    vhFinish();
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BTR_VS" );
    vhShader ps = BenchCompileShader( kBenchPS_Solid, VRHI_SHADER_STAGE_PIXEL, "BTR_PS" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment(0,rt).SetViewRect(glm::vec4(0,0,4,4))
         .SetStateFlags(VRHI_STATE_WRITE_MASK).SetVertexBuffer(vb,0)
         .SetProgram(vhCreateGfxProgram(vs,ps));
    vhStateId sid = 71003;
    vhSetState(sid,state); vhFinish();

    BenchTimer t;
    for ( int i=0;i<g_benchConfig.iters;i++ )
    {
        int b = i & 3;
        state.SetTexture(0,{.texture=pool[b+0]}).SetTexture(1,{.texture=pool[b+1]})
             .SetTexture(2,{.texture=pool[b+2]}).SetTexture(3,{.texture=pool[b+3]});
        t.Begin(); vhSetState(sid,state); vhDraw(sid,3); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Realistic_TextureRebind4", 1 );

    vhDestroyTexture(rt); vhDestroyBuffer(vb);
    for(int i=0;i<8;i++) vhDestroyTexture(pool[i]);
    vhDestroyShader(vs); vhDestroyShader(ps);
    vhSetState(sid,g_state0,VRHI_DIRTY_ALL); vhFinish();
}

// ==========================================================================
// §C — Mixed-pass scenarios (GPU-only)
// ==========================================================================

UTEST_F( Bench, Bench_Frame_ShadowPass )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required for frame passes" ); }

    vhTexture ds = BenchMakeTex( 256, 256, nvrhi::Format::D32, VRHI_TEXTURE_RT );
    vhBuffer vb  = BenchMakeVB( 3 );
    vhShader vs  = BenchCompileShader( kBenchVS_PosOnly, VRHI_SHADER_STAGE_VERTEX, "BSH_VS" );
    vhShader ps  = BenchCompileShader( kBenchPS_Shadow, VRHI_SHADER_STAGE_PIXEL, "BSH_PS" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );
    vhFinish();

    vhState state;
    state.SetDepthAttachment(ds).SetViewRect(glm::vec4(0,0,256,256))
         .SetViewClear(VRHI_CLEAR_DEPTH,glm::vec4(0),1.0f)
         .SetStateFlags(VRHI_STATE_WRITE_Z|VRHI_STATE_DEPTH_TEST_LESS|VRHI_STATE_DEPTH_TEST_ENABLE)
         .SetVertexBuffer(vb,0).SetProgram(vhCreateGfxProgram(vs,ps))
         .SetWorldTransform(glm::mat4(1));
    vhStateId sid = 72000;
    vhSetState(sid,state); vhClear(sid,VRHI_CLEAR_DEPTH);
    for (int i=0;i<g_benchConfig.warmup;i++) { state.SetWorldTransform(glm::mat4(1)); vhSetState(sid,state); vhDraw(sid,3); }
    vhFinish();

    BenchTimer t;
    for ( int i=0; i<g_benchConfig.iters; i++ )
    {
        float a = float(i)*0.01f; glm::mat4 m(1); m[3][0]=cosf(a)*0.1f;
        state.SetWorldTransform(m);
        t.Begin(); vhSetState(sid,state); vhDraw(sid,3); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Frame_ShadowPass", 1 );

    vhDestroyTexture(ds); vhDestroyBuffer(vb); vhDestroyShader(vs); vhDestroyShader(ps);
    vhSetState(sid,g_state0,VRHI_DIRTY_ALL); vhFinish();
}

UTEST_F( Bench, Bench_Frame_GBufferPass )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required for frame passes" ); }

    vhTexture rtAlb = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtNrm = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtOrm = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtEmi = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture ds    = BenchMakeTex( 128, 128, nvrhi::Format::D32, VRHI_TEXTURE_RT );

    vhTexture texPool[4];
    for (int i=0;i<4;i++) texPool[i] = BenchMakeTex(64,64,nvrhi::Format::RGBA8_UNORM);
    vhBuffer matBuf = BenchMakeUB( 64 );
    vhBuffer vb     = BenchMakeVB( 4 );
    vhBuffer ib     = vhAllocBuffer();
    { uint32_t idx[6]={0,1,2,1,3,2}; vhMem* m=vhAllocMem(sizeof(idx)); memcpy(m->data(),idx,sizeof(idx)); vhCreateIndexBuffer(ib,"GBI",m,0,VRHI_BUFFER_INDEX32); }
    vhFinish();

    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "BGB_VS" );
    vhShader ps = BenchCompileShader( kBenchPS_GBuffer, VRHI_SHADER_STAGE_PIXEL, "BGB_PS" );
    ASSERT_NE( vs, VRHI_INVALID_HANDLE ); ASSERT_NE( ps, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment(0,rtAlb).SetColourAttachment(1,rtNrm)
         .SetColourAttachment(2,rtOrm).SetColourAttachment(3,rtEmi)
         .SetDepthAttachment(ds)
         .SetViewRect(glm::vec4(0,0,128,128))
         .SetViewClear(VRHI_CLEAR_COLOR|VRHI_CLEAR_DEPTH,glm::vec4(0),1.0f)
         .SetStateFlags(VRHI_STATE_WRITE_MASK|VRHI_STATE_DEPTH_TEST_LESS|VRHI_STATE_DEPTH_TEST_ENABLE)
         .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs,ps))
         .SetTexture(0,{.name="t0",.texture=texPool[0]}).SetTexture(1,{.name="t1",.texture=texPool[1]})
         .SetTexture(2,{.name="t2",.texture=texPool[2]}).SetTexture(3,{.name="t3",.texture=texPool[3]})
         .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP})
         .SetBuffer(0,{.name="Material",.buffer=matBuf});
    vhStateId sid = 72001;
    vhSetState(sid,state); vhClear(sid,VRHI_CLEAR_COLOR|VRHI_CLEAR_DEPTH);
    for(int i=0;i<g_benchConfig.warmup;i++) { glm::vec4 u(float(i)*0.001f,0,0,1); state.SetUniform(0,"Material",&u,1); vhSetState(sid,state); vhDrawIndexed(sid,6); }
    vhFinish();

    BenchTimer t;
    for ( int i=0; i<g_benchConfig.iters; i++ )
    {
        glm::vec4 u( float(i)*0.001f, 0, 0, 1 );
        state.SetUniform(0,"Material",&u,1);
        t.Begin(); vhSetState(sid,state); vhDrawIndexed(sid,6); t.Sample();
    }
    t.MeasureFlushWait();
    t.Print( "Bench_Frame_GBufferPass", 1 );

    vhDestroyTexture(rtAlb); vhDestroyTexture(rtNrm); vhDestroyTexture(rtOrm);
    vhDestroyTexture(rtEmi); vhDestroyTexture(ds);
    for(int i=0;i<4;i++) vhDestroyTexture(texPool[i]);
    vhDestroyBuffer(matBuf); vhDestroyBuffer(vb); vhDestroyBuffer(ib);
    vhDestroyShader(vs); vhDestroyShader(ps);
    vhSetState(sid,g_state0,VRHI_DIRTY_ALL); vhFinish();
}

UTEST_F( Bench, Bench_Frame_FullScene )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "GPU required for full-scene bench" ); }
    if ( TestIsSoftwareVulkan() ) { UTEST_SKIP( "Full-scene bench skipped on software Vulkan" ); }

    vhTexture rtAlb  = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtNrm  = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtOrm  = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtEmi  = BenchMakeTex( 128, 128, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhTexture rtFwd  = BenchMakeTex( 128, 128, nvrhi::Format::RGBA16_FLOAT, VRHI_TEXTURE_RT );
    vhTexture ds     = BenchMakeTex( 128, 128, nvrhi::Format::D32, VRHI_TEXTURE_RT );
    vhTexture csUAV  = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhTexture texPool[4];
    for (int i=0;i<4;i++) texPool[i] = BenchMakeTex(64,64,nvrhi::Format::RGBA8_UNORM);

    vhBuffer vb = BenchMakeVB(4), ib = vhAllocBuffer();
    { uint32_t idx[6]={0,1,2,1,3,2}; vhMem* m=vhAllocMem(sizeof(idx)); memcpy(m->data(),idx,sizeof(idx)); vhCreateIndexBuffer(ib,"FSI",m,0,VRHI_BUFFER_INDEX32); }
    vhBuffer matBuf = BenchMakeUB(64);
    vhFinish();

    vhShader vs_mesh  = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "FS_VMesh" );
    vhShader vs_depth = BenchCompileShader( kBenchVS_PosOnly, VRHI_SHADER_STAGE_VERTEX, "FS_VDepth" );
    vhShader ps_gbuf  = BenchCompileShader( kBenchPS_GBuffer, VRHI_SHADER_STAGE_PIXEL, "FS_PGB" );
    vhShader ps_shd   = BenchCompileShader( kBenchPS_Shadow, VRHI_SHADER_STAGE_PIXEL, "FS_PSh" );
    vhShader ps_fwd   = BenchCompileShader( kBenchPS_Textured, VRHI_SHADER_STAGE_PIXEL, "FS_PFwd" );
    vhShader cs_light = BenchCompileShader( kBenchCS_Noop, VRHI_SHADER_STAGE_COMPUTE, "FS_CSL" );
    ASSERT_NE( vs_mesh, VRHI_INVALID_HANDLE ); ASSERT_NE( ps_gbuf, VRHI_INVALID_HANDLE );
    ASSERT_NE( ps_fwd,  VRHI_INVALID_HANDLE ); ASSERT_NE( cs_light, VRHI_INVALID_HANDLE );

    const int numFrames = std::min( 32, g_benchConfig.iters );

    BenchTimer t;
    t.Begin();

    for ( int frame = 0; frame < numFrames; frame++ )
    {
        // Shadow pass (100 depth draws)
        {
            vhState s;
            s.SetDepthAttachment(ds).SetViewRect(glm::vec4(0,0,128,128))
             .SetViewClear(VRHI_CLEAR_DEPTH,glm::vec4(0),1.0f)
             .SetStateFlags(VRHI_STATE_WRITE_Z|VRHI_STATE_DEPTH_TEST_LESS|VRHI_STATE_DEPTH_TEST_ENABLE)
             .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_depth,ps_shd));
            vhStateId sid = 73000;
            vhSetState(sid,s); vhClear(sid,VRHI_CLEAR_DEPTH);
            for(int i=0;i<100;i++) { glm::mat4 m(1); m[3][0]=float(i)*0.001f; s.SetWorldTransform(m); vhSetState(sid,s); vhDrawIndexed(sid,6); }
        }

        // GBuffer pass (200 draws)
        {
            vhState s;
            s.SetColourAttachment(0,rtAlb).SetColourAttachment(1,rtNrm)
             .SetColourAttachment(2,rtOrm).SetColourAttachment(3,rtEmi)
             .SetDepthAttachment(ds)
             .SetViewRect(glm::vec4(0,0,128,128))
             .SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0))
             .SetStateFlags(VRHI_STATE_WRITE_MASK|VRHI_STATE_DEPTH_TEST_LEQUAL|VRHI_STATE_DEPTH_TEST_ENABLE)
             .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_gbuf))
             .SetTexture(0,{.name="t0",.texture=texPool[0]}).SetTexture(1,{.name="t1",.texture=texPool[1]})
             .SetTexture(2,{.name="t2",.texture=texPool[2]}).SetTexture(3,{.name="t3",.texture=texPool[3]})
             .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP})
             .SetBuffer(0,{.name="Material",.buffer=matBuf});
            vhStateId sid = 73001;
            vhSetState(sid,s); vhClear(sid,VRHI_CLEAR_COLOR);
            for(int i=0;i<200;i++) { glm::vec4 u(float(i)*0.001f,0,0,1); s.SetUniform(0,"Material",&u,1); vhSetState(sid,s); vhDrawIndexed(sid,6); }
        }

        // Light-cull compute (16 dispatches)
        {
            vhState s = g_state0;
            s.SetProgram(vhCreateComputeProgram(cs_light));
            s.SetTexture(0,{.name="g_Out",.texture=csUAV,.computeUAV=true});
            vhStateId sid = 73002;
            vhSetState(sid,s);
            for(int i=0;i<16;i++) vhDispatch(sid,{1,1,1});
        }

        // Forward pass (50 draws, blending)
        {
            vhState s;
            s.SetColourAttachment(0,rtFwd).SetDepthAttachment(ds)
             .SetViewRect(glm::vec4(0,0,128,128))
             .SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0))
             .SetStateFlags(VRHI_STATE_WRITE_RGB|VRHI_STATE_WRITE_A|VRHI_STATE_DEPTH_TEST_LEQUAL|
                            VRHI_STATE_DEPTH_TEST_ENABLE|VRHI_STATE_BLEND_ALPHA)
             .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_fwd))
             .SetTexture(0,{.name="t0",.texture=texPool[0]}).SetTexture(1,{.name="t1",.texture=texPool[1]})
             .SetTexture(2,{.name="t2",.texture=texPool[2]}).SetTexture(3,{.name="t3",.texture=texPool[3]})
             .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP})
             .SetBuffer(0,{.name="Material",.buffer=matBuf});
            vhStateId sid = 73003;
            vhSetState(sid,s); vhClear(sid,VRHI_CLEAR_COLOR);
            for(int i=0;i<50;i++) { glm::vec4 u(float(i)*0.01f,0,0,1); s.SetUniform(0,"Material",&u,1); vhSetState(sid,s); vhDrawIndexed(sid,6); }
        }

        t.Sample();
    }

    t.MeasureFlushWait();
    t.Print( "Bench_Frame_FullScene", 366, 16 );  // 100+200+50+16 per frame

    // CPU-only variant: same workload but vhFlush(true) per frame so we measure
    // frontend+backend round-trip excluding any GPU wait.
    {
        BenchTimer tCpu;
        for ( int frame = 0; frame < numFrames; frame++ )
        {
            tCpu.Begin();
            {
                vhState s;
                s.SetDepthAttachment(ds).SetViewRect(glm::vec4(0,0,128,128))
                 .SetViewClear(VRHI_CLEAR_DEPTH,glm::vec4(0),1.0f)
                 .SetStateFlags(VRHI_STATE_WRITE_Z|VRHI_STATE_DEPTH_TEST_LESS|VRHI_STATE_DEPTH_TEST_ENABLE)
                 .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_depth,ps_shd));
                vhStateId sid = 73000;
                vhSetState(sid,s); vhClear(sid,VRHI_CLEAR_DEPTH);
                for(int i=0;i<100;i++) { glm::mat4 m(1); m[3][0]=float(i)*0.001f; s.SetWorldTransform(m); vhSetState(sid,s); vhDrawIndexed(sid,6); }
            }
            {
                vhState s;
                s.SetColourAttachment(0,rtAlb).SetColourAttachment(1,rtNrm)
                 .SetColourAttachment(2,rtOrm).SetColourAttachment(3,rtEmi).SetDepthAttachment(ds)
                 .SetViewRect(glm::vec4(0,0,128,128)).SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0))
                 .SetStateFlags(VRHI_STATE_WRITE_MASK|VRHI_STATE_DEPTH_TEST_LEQUAL|VRHI_STATE_DEPTH_TEST_ENABLE)
                 .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_gbuf))
                 .SetTexture(0,{.name="t0",.texture=texPool[0]}).SetTexture(1,{.name="t1",.texture=texPool[1]})
                 .SetTexture(2,{.name="t2",.texture=texPool[2]}).SetTexture(3,{.name="t3",.texture=texPool[3]})
                 .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP})
                 .SetBuffer(0,{.name="Material",.buffer=matBuf});
                vhStateId sid = 73001;
                vhSetState(sid,s); vhClear(sid,VRHI_CLEAR_COLOR);
                for(int i=0;i<200;i++) { glm::vec4 u(float(i)*0.001f,0,0,1); s.SetUniform(0,"Material",&u,1); vhSetState(sid,s); vhDrawIndexed(sid,6); }
            }
            {
                vhState s = g_state0;
                s.SetProgram(vhCreateComputeProgram(cs_light));
                s.SetTexture(0,{.name="g_Out",.texture=csUAV,.computeUAV=true});
                vhStateId sid = 73002;
                vhSetState(sid,s);
                for(int i=0;i<16;i++) vhDispatch(sid,{1,1,1});
            }
            {
                vhState s;
                s.SetColourAttachment(0,rtFwd).SetDepthAttachment(ds).SetViewRect(glm::vec4(0,0,128,128))
                 .SetViewClear(VRHI_CLEAR_COLOR,glm::vec4(0))
                 .SetStateFlags(VRHI_STATE_WRITE_RGB|VRHI_STATE_WRITE_A|VRHI_STATE_DEPTH_TEST_LEQUAL|VRHI_STATE_DEPTH_TEST_ENABLE|VRHI_STATE_BLEND_ALPHA)
                 .SetVertexBuffer(vb,0).SetIndexBuffer(ib).SetProgram(vhCreateGfxProgram(vs_mesh,ps_fwd))
                 .SetTexture(0,{.name="t0",.texture=texPool[0]}).SetTexture(1,{.name="t1",.texture=texPool[1]})
                 .SetTexture(2,{.name="t2",.texture=texPool[2]}).SetTexture(3,{.name="t3",.texture=texPool[3]})
                 .SetSampler(0,{.name="s0",.flags=VRHI_SAMPLER_POINT|VRHI_SAMPLER_UVW_CLAMP})
                 .SetBuffer(0,{.name="Material",.buffer=matBuf});
                vhStateId sid = 73003;
                vhSetState(sid,s); vhClear(sid,VRHI_CLEAR_COLOR);
                for(int i=0;i<50;i++) { glm::vec4 u(float(i)*0.01f,0,0,1); s.SetUniform(0,"Material",&u,1); vhSetState(sid,s); vhDrawIndexed(sid,6); }
            }
            vhFlush( true );  // waits for backend to drain, NOT for GPU
            tCpu.Sample();
        }
        tCpu.MeasureFlushWait();
        tCpu.Print( "Bench_Frame_FullScene_CPUOnly", 366, 16 );
    }

    vhDestroyTexture(rtAlb); vhDestroyTexture(rtNrm); vhDestroyTexture(rtOrm);
    vhDestroyTexture(rtEmi); vhDestroyTexture(rtFwd); vhDestroyTexture(ds); vhDestroyTexture(csUAV);
    for(int i=0;i<4;i++) vhDestroyTexture(texPool[i]);
    vhDestroyBuffer(vb); vhDestroyBuffer(ib); vhDestroyBuffer(matBuf);
    vhDestroyShader(vs_mesh); vhDestroyShader(vs_depth); vhDestroyShader(ps_gbuf);
    vhDestroyShader(ps_shd);  vhDestroyShader(ps_fwd);   vhDestroyShader(cs_light);
    vhFinish();
}

// ==========================================================================
// §H — Backend latency
// ==========================================================================

UTEST_F( Bench, Bench_FlushWait_Empty )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Flush latency requires real backend" ); }
    const int N = std::min( 200, g_benchConfig.iters );
    BenchTimer t;
    for ( int i=0;i<N;i++ )
    {
        t.Begin(); vhFlush(true); t.Sample();
    }
    t.Print( "Bench_FlushWait_Empty" );
}

UTEST_F( Bench, Bench_FinishWait_Empty )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Finish latency requires real backend" ); }
    const int N = std::min( 50, g_benchConfig.iters );
    BenchTimer t;
    for ( int i=0;i<N;i++ )
    {
        t.Begin(); vhFinish(); t.Sample();
    }
    t.Print( "Bench_FinishWait_Empty" );
}

// Submit realistic-ish work, time ONLY the flush. Measures backend-drain latency
// when there are real commands queued. Uses vhFlush(true) so we wait for backend
// to process everything but NOT for the GPU to finish.
UTEST_F( Bench, Bench_FlushWait_RealWork )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Flush latency requires real backend" ); }

    vhTexture rt = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer  vb = BenchMakeVB( 3 );
    vhBuffer  matBuf = BenchMakeUB( 64 );
    vhTexture texPool[4];
    for ( int i = 0; i < 4; i++ ) texPool[i] = BenchMakeTex( 16, 16, nvrhi::Format::RGBA8_UNORM );
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "FW_VS" );
    vhShader ps = BenchCompileShader( kBenchPS_Textured, VRHI_SHADER_STAGE_PIXEL, "FW_PS" );
    vhFinish();

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0 ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK )
         .SetVertexBuffer( vb, 0 ).SetProgram( vhCreateGfxProgram( vs, ps ) )
         .SetTexture( 0, { .name = "t0", .texture = texPool[0] } )
         .SetTexture( 1, { .name = "t1", .texture = texPool[1] } )
         .SetTexture( 2, { .name = "t2", .texture = texPool[2] } )
         .SetTexture( 3, { .name = "t3", .texture = texPool[3] } )
         .SetSampler( 0, { .name = "s0", .flags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } )
         .SetBuffer( 0, { .name = "Material", .buffer = matBuf } );

    const int N = g_benchConfig.iters;
    const int kDrawsPerIter = 64;

    for ( int w = 0; w < g_benchConfig.warmup; w++ )
    {
        vhStateId sid = 75500;
        vhSetState( sid, state ); vhClear( sid, VRHI_CLEAR_COLOR );
        for ( int i = 0; i < kDrawsPerIter; i++ )
        {
            glm::vec4 u( float( i ) * 0.001f, 0, 0, 1 );
            state.SetUniform( 0, "Material", &u, 1 );
            vhSetState( sid, state );
            vhDraw( sid, 3 );
        }
        vhFlush( true );
    }

    BenchTimer t;
    for ( int iter = 0; iter < N; iter++ )
    {
        vhStateId sid = 75500;
        vhSetState( sid, state ); vhClear( sid, VRHI_CLEAR_COLOR );
        for ( int i = 0; i < kDrawsPerIter; i++ )
        {
            glm::vec4 u( float( iter * kDrawsPerIter + i ) * 0.001f, 0, 0, 1 );
            state.SetUniform( 0, "Material", &u, 1 );
            vhSetState( sid, state );
            vhDraw( sid, 3 );
        }
        t.Begin();
        vhFlush( true );
        t.Sample();
    }
    t.Print( "Bench_FlushWait_RealWork", kDrawsPerIter );

    vhDestroyTexture( rt ); vhDestroyBuffer( vb ); vhDestroyBuffer( matBuf );
    for ( int i = 0; i < 4; i++ ) vhDestroyTexture( texPool[i] );
    vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( 75500, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// Repro for the multi-ms flush-spike issue. Floods other threads with CPU work
// to force scheduler pressure, then measures vhFlush(true) latency. The spike
// surfaces because vhFlush spin-waits via sleep_for(20ns) which Darwin rounds
// up to a ~1ms minimum quantum — so when the calling thread loses its slot to
// a background hog, the next wakeup can be 1-5ms late.
UTEST_F( Bench, Bench_FlushWait_UnderLoad )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Flush latency requires real backend" ); }

    vhTexture rt = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer  vb = BenchMakeVB( 3 );
    vhBuffer  matBuf = BenchMakeUB( 64 );
    vhTexture texPool[4];
    for ( int i = 0; i < 4; i++ ) texPool[i] = BenchMakeTex( 16, 16, nvrhi::Format::RGBA8_UNORM );
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "FU_VS" );
    vhShader ps = BenchCompileShader( kBenchPS_Textured, VRHI_SHADER_STAGE_PIXEL, "FU_PS" );
    vhFinish();

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0 ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK )
         .SetVertexBuffer( vb, 0 ).SetProgram( vhCreateGfxProgram( vs, ps ) )
         .SetTexture( 0, { .name = "t0", .texture = texPool[0] } )
         .SetTexture( 1, { .name = "t1", .texture = texPool[1] } )
         .SetTexture( 2, { .name = "t2", .texture = texPool[2] } )
         .SetTexture( 3, { .name = "t3", .texture = texPool[3] } )
         .SetSampler( 0, { .name = "s0", .flags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } )
         .SetBuffer( 0, { .name = "Material", .buffer = matBuf } );

    const int N = g_benchConfig.iters;
    const int kDrawsPerIter = 64;

    // Background CPU hogs at ~half the logical cores. Mimics a realistic engine
    // where worker threads do parallel work while the main thread submits.
    const int hogCount = std::max( 1, ( int ) std::thread::hardware_concurrency() / 2 );
    std::atomic< bool > hogStop( false );
    std::vector< std::thread > hogs;
    for ( int h = 0; h < hogCount; h++ )
    {
        hogs.emplace_back( [&hogStop]
        {
            volatile uint64_t x = 0;
            while ( !hogStop.load( std::memory_order_relaxed ) )
            {
                for ( int i = 0; i < 1000; i++ ) x = x * 6364136223846793005ULL + 1442695040888963407ULL;
            }
        } );
    }

    BenchTimer t;
    for ( int iter = 0; iter < N; iter++ )
    {
        vhStateId sid = 75502;
        vhSetState( sid, state ); vhClear( sid, VRHI_CLEAR_COLOR );
        for ( int i = 0; i < kDrawsPerIter; i++ )
        {
            glm::vec4 u( float( iter * kDrawsPerIter + i ) * 0.001f, 0, 0, 1 );
            state.SetUniform( 0, "Material", &u, 1 );
            vhSetState( sid, state );
            vhDraw( sid, 3 );
        }
        t.Begin();
        vhFlush( true );
        t.Sample();
    }
    t.Print( "Bench_FlushWait_UnderLoad", kDrawsPerIter );

    hogStop.store( true );
    for ( auto& th : hogs ) th.join();

    vhDestroyTexture( rt ); vhDestroyBuffer( vb ); vhDestroyBuffer( matBuf );
    for ( int i = 0; i < 4; i++ ) vhDestroyTexture( texPool[i] );
    vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( 75502, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// Same as above but with vhFinish() — measures full GPU drain latency too.
UTEST_F( Bench, Bench_FinishWait_RealWork )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Finish latency requires real backend" ); }

    vhTexture rt = BenchMakeTex( 64, 64, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhBuffer  vb = BenchMakeVB( 3 );
    vhBuffer  matBuf = BenchMakeUB( 64 );
    vhTexture texPool[4];
    for ( int i = 0; i < 4; i++ ) texPool[i] = BenchMakeTex( 16, 16, nvrhi::Format::RGBA8_UNORM );
    vhShader vs = BenchCompileShader( kBenchVS, VRHI_SHADER_STAGE_VERTEX, "FN_VS" );
    vhShader ps = BenchCompileShader( kBenchPS_Textured, VRHI_SHADER_STAGE_PIXEL, "FN_PS" );
    vhFinish();

    vhState state;
    state.SetColourAttachment( 0, rt ).SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
         .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0 ) )
         .SetStateFlags( VRHI_STATE_WRITE_MASK )
         .SetVertexBuffer( vb, 0 ).SetProgram( vhCreateGfxProgram( vs, ps ) )
         .SetTexture( 0, { .name = "t0", .texture = texPool[0] } )
         .SetTexture( 1, { .name = "t1", .texture = texPool[1] } )
         .SetTexture( 2, { .name = "t2", .texture = texPool[2] } )
         .SetTexture( 3, { .name = "t3", .texture = texPool[3] } )
         .SetSampler( 0, { .name = "s0", .flags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP } )
         .SetBuffer( 0, { .name = "Material", .buffer = matBuf } );

    const int N = std::min( 50, g_benchConfig.iters );
    const int kDrawsPerIter = 64;

    BenchTimer t;
    for ( int iter = 0; iter < N; iter++ )
    {
        vhStateId sid = 75501;
        vhSetState( sid, state ); vhClear( sid, VRHI_CLEAR_COLOR );
        for ( int i = 0; i < kDrawsPerIter; i++ )
        {
            glm::vec4 u( float( iter * kDrawsPerIter + i ) * 0.001f, 0, 0, 1 );
            state.SetUniform( 0, "Material", &u, 1 );
            vhSetState( sid, state );
            vhDraw( sid, 3 );
        }
        t.Begin();
        vhFinish();
        t.Sample();
    }
    t.Print( "Bench_FinishWait_RealWork", kDrawsPerIter );

    vhDestroyTexture( rt ); vhDestroyBuffer( vb ); vhDestroyBuffer( matBuf );
    for ( int i = 0; i < 4; i++ ) vhDestroyTexture( texPool[i] );
    vhDestroyShader( vs ); vhDestroyShader( ps );
    vhSetState( 75501, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

// ==========================================================================
// §I — Stats sanity
// ==========================================================================

UTEST_F( Bench, Bench_StatsConsistency )
{
    vhFrame();
    const int D = 37, C = 13;
    for ( int i=0;i<D;i++ ) vhDraw( 0, 3 );
    for ( int i=0;i<C;i++ ) vhDispatch( 0, {1,1,1} );
    vhFrame();
    vhRenderStats s = vhGetStats();
    EXPECT_EQ( s.drawCalls, (uint64_t)D );
    EXPECT_EQ( s.dispatchCalls, (uint64_t)C );
}

// ==========================================================================
// Reference: moved legacy benchmarks (keep for historical comparison)
// ==========================================================================

UTEST_F( Bench, Bench_Reference_ComputeDispatch )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( TestIsSoftwareVulkan() ) { UTEST_SKIP( "Skipped on software Vulkan" ); }

    vhTexture outTex = BenchMakeTex( 8, 8, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);
        [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { g_Out[id.xy] = 1.0; }
    )";
    vhShader cs = BenchCompileShader( csSource, VRHI_SHADER_STAGE_COMPUTE, "BR_CS" );
    ASSERT_NE( cs, VRHI_INVALID_HANDLE );
    vhFinish();

    vhState state = g_state0;
    state.SetProgram( vhCreateComputeProgram( cs ) );
    state.SetTexture( 0, { .name="g_Out", .texture=outTex, .computeUAV=true } );
    vhStateId sid = 74000;
    vhSetState( sid, state );
    for ( int i=0;i<g_benchConfig.warmup;i++ ) vhDispatch( sid, {1,1,1} );
    vhFinish();

    BenchTimer t;
    for ( int i=0;i<1000;i++ ) { t.Begin(); vhDispatch( sid, {1,1,1} ); t.Sample(); }
    vhFlush(true);
    t.Print( "Bench_Reference_ComputeDispatch", 0, 1 );

    vhDestroyTexture( outTex ); vhDestroyShader( cs );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL ); vhFinish();
}
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
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "test.h"
#include <vrhi_internal.h>
#include <vrhi.h>
#include <spirv-tools/optimizer.hpp>
#include <spirv-tools/libspirv.hpp>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;

struct Shader {};
UTEST_F_SETUP( Shader )
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
UTEST_F_TEARDOWN( Shader )
{
    vhEndMarker();
}

UTEST( ShaderInternal, StateToDesc )
{
    // Test Primitive Topology
    EXPECT_EQ( vhTranslatePrimitiveType( VRHI_STATE_PT_LINES ), nvrhi::PrimitiveType::LineList );
    EXPECT_EQ( vhTranslatePrimitiveType( VRHI_STATE_PT_TRIANGLES ), nvrhi::PrimitiveType::TriangleList );
    EXPECT_EQ( vhTranslatePrimitiveType( VRHI_STATE_PT_TRISTRIP ), nvrhi::PrimitiveType::TriangleStrip );

    // Test Default (Depth Test Less, Write All, Cull Back with CCW=front)
    {
        nvrhi::RasterState rs = vhTranslateRasterState( VRHI_STATE_DEFAULT );
        EXPECT_EQ( rs.cullMode, nvrhi::RasterCullMode::Back );
        EXPECT_TRUE( rs.frontCounterClockwise ); // Default: CCW = front

        nvrhi::DepthStencilState ds = vhTranslateDepthStencilState( VRHI_STATE_DEFAULT, VRHI_STENCIL_NONE );
        EXPECT_TRUE( ds.depthTestEnable );
        EXPECT_EQ( ds.depthFunc, nvrhi::ComparisonFunc::Less );
        EXPECT_TRUE( ds.depthWriteEnable );
    }

    // Test Blend Add
    {
        nvrhi::BlendState bs = vhTranslateBlendState( VRHI_STATE_BLEND_ADD );
        EXPECT_EQ( bs.targets[0].srcBlend, nvrhi::BlendFactor::One );
        EXPECT_EQ( bs.targets[0].destBlend, nvrhi::BlendFactor::One );
    }

    // Test Cull Back (Default CCW=front)
    {
        nvrhi::RasterState rs = vhTranslateRasterState( VRHI_STATE_CULL_BACK | VRHI_STATE_WRITE_RGB );
        EXPECT_EQ( rs.cullMode, nvrhi::RasterCullMode::Back );
        EXPECT_TRUE( rs.frontCounterClockwise ); // CCW = front
    }

    // Test Cull Front (Default CCW=front)
    {
        nvrhi::RasterState rs = vhTranslateRasterState( VRHI_STATE_CULL_FRONT | VRHI_STATE_WRITE_RGB );
        EXPECT_EQ( rs.cullMode, nvrhi::RasterCullMode::Front );
        EXPECT_TRUE( rs.frontCounterClockwise ); // CCW = front
    }

    // Test Cull None
    {
        nvrhi::RasterState rs = vhTranslateRasterState( VRHI_STATE_CULL_NONE | VRHI_STATE_WRITE_RGB );
        EXPECT_EQ( rs.cullMode, nvrhi::RasterCullMode::None );
        EXPECT_TRUE( rs.frontCounterClockwise ); // CCW = front (default)
    }

    // Test Cull Back + CW Override (CW=front)
    {
        nvrhi::RasterState rs = vhTranslateRasterState( VRHI_STATE_CULL_BACK | VRHI_STATE_FRONT_CW | VRHI_STATE_WRITE_RGB );
        EXPECT_EQ( rs.cullMode, nvrhi::RasterCullMode::Back );
        EXPECT_FALSE( rs.frontCounterClockwise ); // CW = front
    }

    // Test Cull Front + CW Override (CW=front)
    {
        nvrhi::RasterState rs = vhTranslateRasterState( VRHI_STATE_CULL_FRONT | VRHI_STATE_FRONT_CW | VRHI_STATE_WRITE_RGB );
        EXPECT_EQ( rs.cullMode, nvrhi::RasterCullMode::Front );
        EXPECT_FALSE( rs.frontCounterClockwise ); // CW = front
    }

    // Test Depth Always
    {
        nvrhi::DepthStencilState ds = vhTranslateDepthStencilState( VRHI_STATE_DEPTH_TEST_ALWAYS, VRHI_STENCIL_NONE );
        EXPECT_TRUE( ds.depthTestEnable );
        EXPECT_EQ( ds.depthFunc, nvrhi::ComparisonFunc::Always );
    }

    // Test Stencil Enable & Unpacking (Unified)
    {
        uint64_t stencil =
            VRHI_STENCIL_FUNC_REF( 0x80 ) |
            VRHI_STENCIL_FUNC_RMASK( 0xFF ) |
            VRHI_STENCIL_TEST_EQUAL |
            VRHI_STENCIL_OP_FAIL_S_KEEP |
            VRHI_STENCIL_OP_FAIL_Z_REPLACE |
            VRHI_STENCIL_OP_PASS_Z_INCR;

        nvrhi::DepthStencilState ds = vhTranslateDepthStencilState( VRHI_STATE_DEFAULT, stencil );

        EXPECT_TRUE( ds.stencilEnable );
        EXPECT_EQ( ds.stencilRefValue, 0x80 );
        EXPECT_EQ( ds.stencilReadMask, 0xFF );

        // Front Face
        EXPECT_EQ( ds.frontFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Equal );
        EXPECT_EQ( ds.frontFaceStencil.failOp, nvrhi::StencilOp::Keep );
        EXPECT_EQ( ds.frontFaceStencil.depthFailOp, nvrhi::StencilOp::Replace );
        EXPECT_EQ( ds.frontFaceStencil.passOp, nvrhi::StencilOp::IncrementAndWrap );

        // Back Face (should match front)
        EXPECT_EQ( ds.backFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Equal );
        EXPECT_EQ( ds.backFaceStencil.passOp, nvrhi::StencilOp::IncrementAndWrap );
    }

    // Test Stencil Separate (manually packed into single uint64_t)
    {
        uint64_t front = VRHI_STENCIL_TEST_ALWAYS | VRHI_STENCIL_OP_PASS_Z_KEEP;
        uint64_t back = VRHI_STENCIL_TEST_NEVER | VRHI_STENCIL_OP_PASS_Z_REPLACE;
        
        // Pack back face bits into the unified format
        uint64_t packedStencil = front |
            ((back & VRHI_STENCIL_TEST_MASK) << (VRHI_STENCIL_BACK_TEST_SHIFT - VRHI_STENCIL_TEST_SHIFT)) |
            ((back & VRHI_STENCIL_OP_PASS_Z_MASK) << (VRHI_STENCIL_BACK_OP_PASS_Z_SHIFT - VRHI_STENCIL_OP_PASS_Z_SHIFT));

        nvrhi::DepthStencilState ds = vhTranslateDepthStencilState( VRHI_STATE_DEFAULT, packedStencil );

        EXPECT_TRUE( ds.stencilEnable );
        EXPECT_EQ( ds.frontFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Always );
        EXPECT_EQ( ds.frontFaceStencil.passOp, nvrhi::StencilOp::Keep );

        EXPECT_EQ( ds.backFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Never );
        EXPECT_EQ( ds.backFaceStencil.passOp, nvrhi::StencilOp::Replace );
    }
}

// Test that verifies all stencil mask/shift combinations extract correct values
UTEST_F( Shader, StencilMaskShiftConsistency )
{
    // Test VRHI_STENCIL_TEST_* constants
    {
        EXPECT_EQ( (VRHI_STENCIL_TEST_LESS     & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 1 );
        EXPECT_EQ( (VRHI_STENCIL_TEST_EQUAL    & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 3 );
        EXPECT_EQ( (VRHI_STENCIL_TEST_ALWAYS   & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 8 );
    }

    // Test VRHI_STENCIL_OP_FAIL_S_* constants
    {
        EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_KEEP    & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 1 );
        EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_REPLACE & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 2 );
        EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_INCR    & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 3 );
    }

    // Test VRHI_STENCIL_OP_FAIL_Z_* constants
    {
        EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_Z_KEEP    & VRHI_STENCIL_OP_FAIL_Z_MASK) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT, 1 );
        EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_Z_REPLACE & VRHI_STENCIL_OP_FAIL_Z_MASK) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT, 2 );
        EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_Z_INCR    & VRHI_STENCIL_OP_FAIL_Z_MASK) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT, 3 );
    }

    // Test VRHI_STENCIL_OP_PASS_Z_* constants
    {
        EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_KEEP    & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 1 );
        EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_REPLACE & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 2 );
        EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_INCR    & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 3 );
    }

    // Test FUNC_REF, RMASK, WMASK constants
    {
        EXPECT_EQ( (VRHI_STENCIL_FUNC_REF(0x80) & VRHI_STENCIL_FUNC_REF_MASK) >> VRHI_STENCIL_FUNC_REF_SHIFT, 0x80 );
        EXPECT_EQ( (VRHI_STENCIL_FUNC_RMASK(0xFF) & VRHI_STENCIL_FUNC_RMASK_MASK) >> VRHI_STENCIL_FUNC_RMASK_SHIFT, 0xFF );
        EXPECT_EQ( (VRHI_STENCIL_FUNC_WMASK(0x7F) & VRHI_STENCIL_FUNC_WMASK_MASK) >> VRHI_STENCIL_FUNC_WMASK_SHIFT, 0x7F );
    }
}

// Test that verifies unified stencil packing of separate front/back states
UTEST_F( Shader, StencilUnifiedPacking )
{
    // Test case 1: Simple front-only stencil (no back face bits)
    {
        uint64_t frontOnly = VRHI_STENCIL_TEST_ALWAYS | VRHI_STENCIL_OP_PASS_Z_KEEP;
        nvrhi::DepthStencilState ds = vhTranslateDepthStencilState( VRHI_STATE_DEFAULT, frontOnly );
        
        EXPECT_TRUE( ds.stencilEnable );
        EXPECT_EQ( ds.frontFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Always );
        EXPECT_EQ( ds.frontFaceStencil.passOp, nvrhi::StencilOp::Keep );
        
        // Back face should match front when no back bits are set
        EXPECT_EQ( ds.backFaceStencil.stencilFunc, ds.frontFaceStencil.stencilFunc );
        EXPECT_EQ( ds.backFaceStencil.passOp, ds.frontFaceStencil.passOp );
    }

    // Test case 2: Different front and back (manually packed)
    {
        uint64_t front = VRHI_STENCIL_TEST_ALWAYS | VRHI_STENCIL_OP_PASS_Z_KEEP;
        uint64_t back = VRHI_STENCIL_TEST_NEVER | VRHI_STENCIL_OP_PASS_Z_REPLACE;
        
        // Pack back face bits into the unified format (simulating user packing)
        uint64_t packedStencil = front |
            ((back & VRHI_STENCIL_TEST_MASK) << (VRHI_STENCIL_BACK_TEST_SHIFT - VRHI_STENCIL_TEST_SHIFT)) |
            ((back & VRHI_STENCIL_OP_PASS_Z_MASK) << (VRHI_STENCIL_BACK_OP_PASS_Z_SHIFT - VRHI_STENCIL_OP_PASS_Z_SHIFT));
        
        nvrhi::DepthStencilState ds = vhTranslateDepthStencilState( VRHI_STATE_DEFAULT, packedStencil );
        
        EXPECT_TRUE( ds.stencilEnable );
        EXPECT_EQ( ds.frontFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Always );
        EXPECT_EQ( ds.frontFaceStencil.passOp, nvrhi::StencilOp::Keep );
        EXPECT_EQ( ds.backFaceStencil.stencilFunc, nvrhi::ComparisonFunc::Never );
        EXPECT_EQ( ds.backFaceStencil.passOp, nvrhi::StencilOp::Replace );
    }
}

// Test that validates stencil operation constants have correct enum mapping values
UTEST_F( Shader, StencilConstantValues )
{
    // Test all stencil operations - extracted values should match enum indices
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_KEEP    & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 1 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_REPLACE & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 2 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_INCR    & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 3 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_INCRSAT & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 4 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_DECR    & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 5 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_DECRSAT & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 6 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_S_INVERT  & VRHI_STENCIL_OP_FAIL_S_MASK) >> VRHI_STENCIL_OP_FAIL_S_SHIFT, 7 );

    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_Z_KEEP    & VRHI_STENCIL_OP_FAIL_Z_MASK) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT, 1 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_Z_REPLACE & VRHI_STENCIL_OP_FAIL_Z_MASK) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT, 2 );
    EXPECT_EQ( (VRHI_STENCIL_OP_FAIL_Z_INCR    & VRHI_STENCIL_OP_FAIL_Z_MASK) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT, 3 );

    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_KEEP    & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 1 );
    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_REPLACE & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 2 );
    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_INCR    & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 3 );
    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_INCRSAT & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 4 );
    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_DECR    & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 5 );
    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_DECRSAT & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 6 );
    EXPECT_EQ( (VRHI_STENCIL_OP_PASS_Z_INVERT  & VRHI_STENCIL_OP_PASS_Z_MASK) >> VRHI_STENCIL_OP_PASS_Z_SHIFT, 7 );

    // Verify comparison function constants
    EXPECT_EQ( (VRHI_STENCIL_TEST_LESS     & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 1 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_LEQUAL   & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 2 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_EQUAL    & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 3 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_GEQUAL   & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 4 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_GREATER  & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 5 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_NOTEQUAL & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 6 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_NEVER    & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 7 );
    EXPECT_EQ( (VRHI_STENCIL_TEST_ALWAYS   & VRHI_STENCIL_TEST_MASK) >> VRHI_STENCIL_TEST_SHIFT, 8 );

    // Verify reference, read mask, write mask constants
    EXPECT_EQ( (VRHI_STENCIL_FUNC_REF(0x42)  & VRHI_STENCIL_FUNC_REF_MASK)  >> VRHI_STENCIL_FUNC_REF_SHIFT,  0x42 );
    EXPECT_EQ( (VRHI_STENCIL_FUNC_RMASK(0x55) & VRHI_STENCIL_FUNC_RMASK_MASK) >> VRHI_STENCIL_FUNC_RMASK_SHIFT, 0x55 );
    EXPECT_EQ( (VRHI_STENCIL_FUNC_WMASK(0xAA) & VRHI_STENCIL_FUNC_WMASK_MASK) >> VRHI_STENCIL_FUNC_WMASK_SHIFT, 0xAA );
}

UTEST_F( Shader, ValidateBinding )
{
    vhShaderReflectionResource res;
    res.name = "TestRes";
    res.slot = 5;
    res.set = 0;
    res.type = nvrhi::ResourceType::Texture_SRV;
    res.arraySize = 1;

    nvrhi::BindingLayoutItem item;
    item.slot = 5;
    item.type = nvrhi::ResourceType::Texture_SRV;
    item.size = 1;

    // 1. Valid match
    EXPECT_TRUE( vhShaderValidateBinding( res, item, true ) );

    int32_t startErrors = g_vhErrorCounter.load();

    // 2. Slot mismatch
    item.slot = 6;
    EXPECT_FALSE( vhShaderValidateBinding( res, item, true ) );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    item.slot = 5;

    // 3. Type mismatch
    item.type = nvrhi::ResourceType::Texture_UAV;
    EXPECT_FALSE( vhShaderValidateBinding( res, item, true ) );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 2 );
    item.type = nvrhi::ResourceType::Texture_SRV;

    // 4. Array Size mismatch
    item.size = 4;
    EXPECT_FALSE( vhShaderValidateBinding( res, item, true ) );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 3 );
    item.size = 1;

    // 5. No log error
    item.slot = 6;
    EXPECT_FALSE( vhShaderValidateBinding( res, item, false ) );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 3 ); // Should not increment
}

UTEST_F( Shader, Lifecycle )
{

    vhFlush();
    int32_t baseline = g_vhErrorCounter.load();

    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            return output;
        }
    )";

    std::vector< uint32_t > spirv;
    bool compiled = vhCompileShader(
        "LifecycleShader",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        spirv,
        "main"
    );
    ASSERT_TRUE( compiled );

    vhShader s = vhAllocShader();
    vhCreateShader( s, "LifecycleShader", VRHI_SHADER_STAGE_VERTEX, spirv, "main" );

    vhDestroyShader( s );
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), baseline );
}

UTEST_F( Shader, Compile )
{


    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            return output;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "TestShader",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        spirv,
        "main",
        {},
        {},
        &error
    );

    if ( !success )
    {
        std::cout << "Shader compilation failed: " << error << std::endl;
    }

    EXPECT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );

    // Test Caching (second call should be fast and succeed)
    std::vector< uint32_t > cachedSpirv;
    success = vhCompileShader(
        "TestShader",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        cachedSpirv,
        "main",
        {},
        {},
        &error
    );

    EXPECT_TRUE( success );
    EXPECT_EQ( spirv.size(), cachedSpirv.size() );
    if ( spirv.size() == cachedSpirv.size() )
    {
        for ( size_t i = 0; i < spirv.size(); ++i )
        {
            EXPECT_EQ( spirv[i], cachedSpirv[i] );
        }
    }
}

UTEST_F( Shader, CompileFail )
{


    // Shader with syntax error (missing semicolon)
    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            return output // Missing semicolon
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "TestShaderFail",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
        spirv,
        "main",
        {},
        {},
        &error
    );

    EXPECT_FALSE( success );
    EXPECT_GT( error.size(), 0 );
    // Check for some text indicating an error
    EXPECT_TRUE( error.find( "error" ) != std::string::npos || error.find( "Error" ) != std::string::npos );
}

UTEST_F( Shader, Reflection )
{


    // Note ByteAddressBuffer and RWByteAddressBuffer MUST have "Raw" in their name in order to spirv_reflect correctly.

    const char* c_shaderSource = R"(
        struct Data { float4 val; };
        ConstantBuffer<Data> g_Constants;
        RWStructuredBuffer<Data> g_Output;
        Texture2D<float4> g_Tex2D;
        [[vk::image_format("rgba32f")]] RWTexture3D<float4> g_RWTex3D;
        
        Buffer<float4> g_TypedSRV;
        RWBuffer<float4> g_TypedUAV;
        ByteAddressBuffer g_RawSRV;
        RWByteAddressBuffer g_RawUAV;
        StructuredBuffer<Data> g_StructSRV;
        StructuredBuffer<float> g_StructFloatSRV;

        [numthreads(8, 4, 1)]
        void main(uint3 threadID : SV_DispatchThreadID)
        {
            float4 val = g_Constants.val
                + g_Tex2D.Load( int3( 0, 0, 0 ) )
                + g_RWTex3D.Load( int3( 0, 0, 0 ) )
                + g_TypedSRV.Load( 0 )
                + g_TypedUAV.Load( 0 )
                + asfloat( g_RawSRV.Load( 0 ) )
                + asfloat( g_RawUAV.Load( 0 ) )
                + g_StructSRV[0].val
                + g_StructFloatSRV[0];

            g_Output[threadID.x].val = val;
        }
    )";

    std::vector<uint32_t> spirv;
    std::string error;
    bool compiled = vhCompileShader( "TestQueryShader", c_shaderSource, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if (!compiled) std::cout << "Compile Error: " << error << std::endl;
    ASSERT_TRUE( compiled );

    vhShader shader = vhAllocShader();
    vhCreateShader( shader, "TestQueryShader", VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main" );
    vhFlush();

    // Query Info
    glm::uvec3 groupSize = { 0, 0, 0 };
    std::vector< vhShaderReflectionResource > resources;
    vhGetShaderInfo( shader, &groupSize, &resources );

    EXPECT_EQ( groupSize.x, 8 );
    EXPECT_EQ( groupSize.y, 4 );
    EXPECT_EQ( groupSize.z, 1 );

    // Expecting at least 10 resources
    EXPECT_GE( resources.size(), 10 );

    bool foundTex2D = false;
    bool foundRWTex3D = false;
    bool foundTypedSRV = false;
    bool foundTypedUAV = false;
    bool foundRawSRV = false;
    bool foundRawUAV = false;
    bool foundStructSRV = false;
    bool foundStructFloatSRV = false;
    bool foundStructUAV = false;
    bool foundCB = false;

    for ( const auto& res : resources )
    {
        if ( res.name == "g_Tex2D" )
        {
            foundTex2D = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::Texture_SRV );
            EXPECT_EQ( res.dim, nvrhi::TextureDimension::Texture2D );
        }
        else if ( res.name == "g_RWTex3D" )
        {
            foundRWTex3D = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::Texture_UAV );
            EXPECT_EQ( res.dim, nvrhi::TextureDimension::Texture3D );
            EXPECT_EQ( res.format, nvrhi::Format::RGBA32_FLOAT );
        }
        else if ( res.name == "g_TypedSRV" )
        {
            foundTypedSRV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::TypedBuffer_SRV );
        }
        else if ( res.name == "g_TypedUAV" )
        {
            foundTypedUAV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::TypedBuffer_UAV );
        }
        else if ( res.name == "g_RawSRV" )
        {
            foundRawSRV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::RawBuffer_SRV );
        }
        else if ( res.name == "g_RawUAV" )
        {
            foundRawUAV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::RawBuffer_UAV );
        }
        else if ( res.name == "g_StructSRV" )
        {
            foundStructSRV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::StructuredBuffer_SRV );
        }
        else if ( res.name == "g_StructFloatSRV" )
        {
            foundStructFloatSRV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::StructuredBuffer_SRV );
        }
        else if ( res.name == "g_Output" )
        {
            foundStructUAV = true;
            EXPECT_EQ( res.type, nvrhi::ResourceType::StructuredBuffer_UAV );
        }
        else if ( res.name == "g_Constants" && res.type == nvrhi::ResourceType::ConstantBuffer )
        {
            foundCB = true;
        }
    }

    EXPECT_TRUE( foundTex2D );
    EXPECT_TRUE( foundRWTex3D );
    EXPECT_TRUE( foundTypedSRV );
    EXPECT_TRUE( foundTypedUAV );
    EXPECT_TRUE( foundRawSRV );
    EXPECT_TRUE( foundRawUAV );
    EXPECT_TRUE( foundStructSRV );
    EXPECT_TRUE( foundStructFloatSRV );
    EXPECT_TRUE( foundStructUAV );
    EXPECT_TRUE( foundCB );

    // Query Handle
    nvrhi::ShaderHandle handle = vhGetShaderNvrhiHandle( shader );
    EXPECT_NE( handle, nullptr );

    vhDestroyShader( shader );
    vhFlush();

    // Query after destruction
    nvrhi::ShaderHandle handleAfter = vhGetShaderNvrhiHandle( shader );
    EXPECT_EQ( handleAfter, nullptr );
}

UTEST( Hashing, ShaderDebugName )
{
    // Basic null check
    EXPECT_EQ( vhHashShaderDebugName( nullptr ), 0 );

    // Declare internal function if not already visible
    extern uint64_t vhHashShaderSPIRV( const std::vector< uint32_t >& spirv );

    // 1. Test vhHashShaderSPIRV stability and differentiation
    std::vector< uint32_t > codeA = { 10, 20, 30, 40 };
    std::vector< uint32_t > codeB = { 10, 20, 30, 40 }; // Same as A
    std::vector< uint32_t > codeC = { 40, 30, 20, 10 }; // Different

    uint64_t hashA = vhHashShaderSPIRV( codeA );
    uint64_t hashB = vhHashShaderSPIRV( codeB );
    uint64_t hashC = vhHashShaderSPIRV( codeC );

    EXPECT_NE( hashA, 0 );
    EXPECT_EQ( hashA, hashB );
    EXPECT_NE( hashA, hashC );

    // 2. Test vhHashShaderDebugName using simulated backend naming (Name # Hash)
    struct MockShader : public nvrhi::RefCounter<nvrhi::IShader>
    {
        nvrhi::ShaderDesc d;
        MockShader( const std::string& name ) { d.debugName = name; }
        const nvrhi::ShaderDesc& getDesc() const override { return d; }
        void getBytecode( const void** ppBytecode, size_t* pSize ) const override
        {
            *ppBytecode = nullptr;
            *pSize = 0;
        }
    };

    // Simulate what Handle_vhCreateShader does:
    std::string debugNameA = "MyShader # " + std::to_string( hashA );
    std::string debugNameC = "MyShader # " + std::to_string( hashC );

    MockShader* raw1 = new MockShader( debugNameA );
    nvrhi::ShaderHandle s1( raw1 );
    raw1->Release();

    MockShader* raw2 = new MockShader( debugNameA ); // Identical content -> Identical Name
    nvrhi::ShaderHandle s2( raw2 );
    raw2->Release();

    MockShader* raw3 = new MockShader( debugNameC ); // Different content -> Different Name
    nvrhi::ShaderHandle s3( raw3 );
    raw3->Release();

    uint64_t h1 = vhHashShaderDebugName( s1 );
    uint64_t h2 = vhHashShaderDebugName( s2 );
    uint64_t h3 = vhHashShaderDebugName( s3 );

    EXPECT_NE( h1, 0 );
    EXPECT_EQ( h1, h2 );
    EXPECT_NE( h1, h3 );
}


// --------------------------------------------------------------------------
// Shader Globals Tests
// --------------------------------------------------------------------------

static const char* g_globalsTestShader = R"(
uniform float3 u_float3;
uniform float4 u_float4;

[shader("vertex")]
float4 main() : SV_Position
{
    return float4( u_float3, 1.0 ) + u_float4;
}
)";

UTEST_F( Shader, BareGlobalsReflection )
{
    if ( !g_testInit ) return;

    vhShader shader = vhAllocShader();
    
    std::vector< uint32_t > spirv;
    std::string error;
    bool compiled = vhCompileShader( "GlobalsTest", g_globalsTestShader, VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if (!compiled) printf("Compile Error: %s\n", error.c_str());
    ASSERT_TRUE( compiled );

    // Create shader and assume it compiles successfully
    vhCreateShader( shader, "GlobalsTest", VRHI_SHADER_STAGE_VERTEX, spirv, "main" );
    
    // Flush to ensure backend processes the creation command
    vhFlush();
    
    std::vector< vhShaderReflectionResource > resources;
    vhGetShaderInfo( shader, nullptr, &resources );
    
    bool foundGlobals = false;
    bool foundFloat3 = false;
    bool foundFloat4 = false;
    
    printf( "Reflected Resources: %zu\n", resources.size() );
    for ( const auto& r : resources )
    {
        printf( "Resource: '%s' Type: %d Slot: %u Set: %u Size: %u\n", r.name.c_str(), (int)r.type, r.slot, r.set, r.sizeInBytes );
        for( const auto& m : r.members )
        {
             printf( "  Member: '%s' Offset: %u Size: %u\n", m.name.c_str(), m.offset, m.size );
        }
    }
    
    for ( const auto& r : resources )
    {
        if ( r.type == nvrhi::ResourceType::ConstantBuffer && ( r.name == "$Globals" || r.name == "_Globals" || r.name == "globalParams" ) )
        {
            foundGlobals = true;
            for ( const auto& m : r.members )
            {
                if ( m.name == "u_float3" ) {
                     EXPECT_GE( m.size, 12u ); // float3 is 12 bytes
                     foundFloat3 = true;
                }
                if ( m.name == "u_float4" ) {
                     EXPECT_EQ( m.size, 16u );
                     foundFloat4 = true;
                }
            }
        }
    }
    
    EXPECT_TRUE( foundGlobals );
    EXPECT_TRUE( foundFloat3 );
    EXPECT_TRUE( foundFloat4 );
    
    vhDestroyShader( shader );
}

UTEST_F( Shader, GlobalsPacking )
{
    std::vector< vhState::UniformBufferValue > uniforms;
    std::vector< vhReflectionMember > members;
    
    // Setup Uniforms
    vhState::UniformBufferValue u1;
    u1.name = "MyVec3";
    u1.data.push_back( glm::vec4( 1.0f, 2.0f, 3.0f, 4.0f ) );
    uniforms.push_back( u1 );
    
    vhState::UniformBufferValue u2;
    u2.name = "MyFloat";
    u2.data.push_back( glm::vec4( 42.0f, 0.0f, 0.0f, 0.0f ) );
    uniforms.push_back( u2 );
    
    // Setup Reflection Members
    members.push_back( { "MyVec3", 0, 12 } );
    members.push_back( { "MyFloat", 16, 4 } );
    
    uint8_t buffer[32];
    vhPackUserGlobals( uniforms, members, buffer, sizeof(buffer) );
    
    // Verify MyVec3 (1.0, 2.0, 3.0)
    float* fBuf = (float*)buffer;
    EXPECT_EQ( fBuf[0], 1.0f );
    EXPECT_EQ( fBuf[1], 2.0f );
    EXPECT_EQ( fBuf[2], 3.0f );
    EXPECT_EQ( fBuf[3], 0.0f ); // Zeroed out
                                
    // Verify MyFloat (42.0) at offset 16 (index 4 in float array)
    EXPECT_EQ( fBuf[4], 42.0f );
}

UTEST_F( Shader, CompileNonMainEntryPoints )
{
    // Multi-stage shader with distinct entry points
    const char* shaderSource = R"(
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; float4 col : COLOUR; };
        
        uniform float4 u_colour;
        
        [shader("vertex")]
        VSOutput VSMain(VSInput input)
        {
            VSOutput output;
            output.pos = float4(input.pos, 1.0);
            output.col = u_colour;
            return output;
        }
        
        [shader("pixel")]
        float4 PSMain(VSOutput input) : SV_Target
        {
            return input.col;
        }
        
        RWStructuredBuffer<float4> g_Output;
        
        [shader("compute")]
        [numthreads(8, 8, 1)]
        void CSMain(uint3 threadID : SV_DispatchThreadID)
        {
            g_Output[threadID.x] = float4(1.0, 0.0, 0.0, 1.0);
        }
    )";

    // Compile vertex shader with VSMain entry point
    {
        std::vector< uint32_t > spirv;
        std::string error;
        bool success = vhCompileShader(
            "TestVSMain",
            shaderSource,
            VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5,
            spirv,
            "VSMain",
            {},
            {},
            &error
        );
        
        if ( !success )
        {
            std::cout << "VSMain compilation failed: " << error << std::endl;
        }
        
        EXPECT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }
    
    // Compile pixel shader with PSMain entry point
    {
        std::vector< uint32_t > spirv;
        std::string error;
        bool success = vhCompileShader(
            "TestPSMain",
            shaderSource,
            VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_5,
            spirv,
            "PSMain",
            {},
            {},
            &error
        );
        
        if ( !success )
        {
            std::cout << "PSMain compilation failed: " << error << std::endl;
        }
        
        EXPECT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }
    
    // Compile compute shader with CSMain entry point
    {
        std::vector< uint32_t > spirv;
        std::string error;
        bool success = vhCompileShader(
            "TestCSMain",
            shaderSource,
            VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_5,
            spirv,
            "CSMain",
            {},
            {},
            &error
        );
        
        if ( !success )
        {
            std::cout << "CSMain compilation failed: " << error << std::endl;
        }
        
        EXPECT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }
}

// --------------------------------------------------------------------------
// SPIR-V Descriptor Set Patching Tests
// --------------------------------------------------------------------------

// Construct an OpDecorate instruction word array: OpDecorate TargetId Decoration [Literals]
static std::vector< uint32_t > MakeOpDecorate( uint32_t targetId, uint32_t decoration, const std::vector< uint32_t >& literals )
{
    std::vector< uint32_t > result;
    uint32_t wordCount = 1 + 1 + 1 + static_cast< uint32_t >( literals.size() );
    constexpr uint32_t spvOpDecorate = 71;
    result.push_back( ( wordCount << 16 ) | spvOpDecorate );
    result.push_back( targetId );
    result.push_back( decoration );
    result.insert( result.end(), literals.begin(), literals.end() );
    return result;
}

UTEST( SpirvPatchDescriptorSet, PatchesOnlySetZero )
{
    // SPIR-V header: Magic, Version, Generator, Bound, Schema
    std::vector< uint32_t > spirv = {
        0x07230203, // Magic number
        0x00010600, // Version 1.6.0
        0x00000000, // Generator
        100,        // Bound
        0           // Schema
    };

    // Add OpDecorate instructions:
    // Decoration 34 = DescriptorSet per SPIR-V spec
    constexpr uint32_t spvDecorationDescriptorSet = 34;
    constexpr uint32_t spvDecorationBuiltIn = 11;

    // Instruction 1: DescriptorSet 0 (should be patched to 10)
    auto inst1 = MakeOpDecorate( 10, spvDecorationDescriptorSet, { 0 } );
    spirv.insert( spirv.end(), inst1.begin(), inst1.end() );

    // Instruction 2: DescriptorSet 1 (should remain unchanged)
    auto inst2 = MakeOpDecorate( 20, spvDecorationDescriptorSet, { 1 } );
    spirv.insert( spirv.end(), inst2.begin(), inst2.end() );

    // Instruction 3: BuiltIn decoration (not a descriptor set, should be unchanged)
    auto inst3 = MakeOpDecorate( 30, spvDecorationBuiltIn, { 5 } );
    spirv.insert( spirv.end(), inst3.begin(), inst3.end() );

    // Record original values for comparison
    size_t inst1SetIndex = 5 + 3;  // Header (5) + instruction offset + set literal offset
    size_t inst2SetIndex = 5 + 4 + 3;  // Previous + inst1 size + instruction offset
    size_t inst3LiteralIndex = 5 + 4 + 4 + 3;

    uint32_t originalInst1Set = spirv[inst1SetIndex];
    uint32_t originalInst2Set = spirv[inst2SetIndex];
    uint32_t originalInst3Literal = spirv[inst3LiteralIndex];

    // Apply patch
    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 10 );

    // Assertions
    EXPECT_EQ( patchedCount, 1 );
    EXPECT_EQ( spirv[inst1SetIndex], 10 );      // Patched from 0 to 10
    EXPECT_EQ( spirv[inst2SetIndex], originalInst2Set );  // Unchanged (was 1)
    EXPECT_EQ( spirv[inst3LiteralIndex], originalInst3Literal );  // Unchanged (BuiltIn)
}

UTEST( SpirvPatchDescriptorSet, NoOpWhenTargetIsZero )
{
    // SPIR-V header
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000, 100, 0
    };

    constexpr uint32_t spvDecorationDescriptorSet = 34;

    // Add DescriptorSet 0 decoration
    auto inst = MakeOpDecorate( 10, spvDecorationDescriptorSet, { 0 } );
    spirv.insert( spirv.end(), inst.begin(), inst.end() );

    // Record original value
    size_t setIndex = 5 + 3;
    uint32_t originalValue = spirv[setIndex];

    // Patch with target 0 (same as original, should be idempotent)
    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 0 );

    // When target is 0, we still report the patch but value remains 0
    EXPECT_EQ( patchedCount, 1 );
    EXPECT_EQ( spirv[setIndex], originalValue );  // Still 0
}

UTEST( SpirvPatchDescriptorSet, MultipleSetZeroDecorations )
{
    // SPIR-V header
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000, 100, 0
    };

    constexpr uint32_t spvDecorationDescriptorSet = 34;

    // Add multiple DescriptorSet 0 decorations
    for ( uint32_t i = 0; i < 5; ++i )
    {
        auto inst = MakeOpDecorate( 10 + i, spvDecorationDescriptorSet, { 0 } );
        spirv.insert( spirv.end(), inst.begin(), inst.end() );
    }

    // Add one DescriptorSet 1 decoration (should not be patched)
    auto instNonZero = MakeOpDecorate( 20, spvDecorationDescriptorSet, { 1 } );
    spirv.insert( spirv.end(), instNonZero.begin(), instNonZero.end() );

    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 15 );

    EXPECT_EQ( patchedCount, 5 );  // Only the 5 set-0 decorations
}

UTEST( SpirvPatchDescriptorSet, TooShortInput )
{
    // Vector shorter than header size (5 words)
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000  // Only 3 words
    };

    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 10 );

    EXPECT_EQ( patchedCount, 0 );
}

UTEST( SpirvPatchDescriptorSet, MalformedWordCountOverflow )
{
    // SPIR-V header
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000, 100, 0
    };

    // Add an instruction with wordCount that would exceed buffer size
    constexpr uint32_t spvOpDecorate = 71;
    uint32_t wordCount = 100;  // Claims to be 100 words but we only add a few
    spirv.push_back( ( wordCount << 16 ) | spvOpDecorate );
    spirv.push_back( 10 );   // TargetId
    spirv.push_back( 34 );   // DescriptorSet decoration
    spirv.push_back( 0 );    // Set literal

    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 10 );

    // Should detect overflow and stop safely
    EXPECT_EQ( patchedCount, 0 );
}

UTEST( SpirvPatchDescriptorSet, MalformedZeroWordCount )
{
    // SPIR-V header
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000, 100, 0
    };

    // Add an instruction with wordCount = 0 (invalid)
    constexpr uint32_t spvOpDecorate = 71;
    spirv.push_back( ( 0 << 16 ) | spvOpDecorate );  // wordCount = 0

    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 10 );

    // Should detect invalid word count and stop
    EXPECT_EQ( patchedCount, 0 );
}

UTEST( SpirvPatchDescriptorSet, ShortInstructionNotDescriptorSet )
{
    // SPIR-V header
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000, 100, 0
    };

    // Add an OpDecorate with only 3 words (missing literal)
    // This should be skipped because it doesn't have enough operands
    constexpr uint32_t spvOpDecorate = 71;
    constexpr uint32_t spvDecorationDescriptorSet = 34;
    spirv.push_back( ( 3 << 16 ) | spvOpDecorate );  // wordCount = 3
    spirv.push_back( 10 );   // TargetId
    spirv.push_back( spvDecorationDescriptorSet );  // Decoration
    // Missing: set literal

    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 10 );

    // Should skip this instruction due to insufficient operands
    EXPECT_EQ( patchedCount, 0 );
}

UTEST( SpirvPatchDescriptorSet, MixedDecorations )
{
    // SPIR-V header
    std::vector< uint32_t > spirv = {
        0x07230203, 0x00010600, 0x00000000, 100, 0
    };

    constexpr uint32_t spvOpDecorate = 71;
    constexpr uint32_t spvDecorationDescriptorSet = 34;
    constexpr uint32_t spvDecorationBinding = 33;
    constexpr uint32_t spvDecorationLocation = 30;

    // Interleave various decoration types with DescriptorSet decorations
    auto addInst = [&]( uint32_t opcode, uint32_t wordCount, const std::vector< uint32_t >& operands )
    {
        spirv.push_back( ( wordCount << 16 ) | opcode );
        for ( uint32_t op : operands ) spirv.push_back( op );
    };

    // Binding decoration (not DescriptorSet)
    addInst( spvOpDecorate, 4, { 10, spvDecorationBinding, 5 } );

    // DescriptorSet 0 (should be patched)
    addInst( spvOpDecorate, 4, { 11, spvDecorationDescriptorSet, 0 } );

    // Location decoration
    addInst( spvOpDecorate, 4, { 12, spvDecorationLocation, 0 } );

    // DescriptorSet 2 (should NOT be patched - not set 0)
    addInst( spvOpDecorate, 4, { 13, spvDecorationDescriptorSet, 2 } );

    // Another DescriptorSet 0 (should be patched)
    addInst( spvOpDecorate, 4, { 14, spvDecorationDescriptorSet, 0 } );

    // Record indices of the DescriptorSet literals
    size_t set0Index1 = 5 + 4 + 3;  // After first 3-word inst + 4-word inst
    size_t set2Index = set0Index1 + 4 + 4;
    size_t set0Index2 = set2Index + 4;

    uint32_t patchedCount = vhPatchSpirvDescriptorSet0( spirv, 99 );

    EXPECT_EQ( patchedCount, 2 );  // Two set-0 decorations patched
    EXPECT_EQ( spirv[set0Index1], 99 );  // First set-0 patched
    EXPECT_EQ( spirv[set2Index], 2 );    // Set-2 unchanged
    EXPECT_EQ( spirv[set0Index2], 99 );  // Second set-0 patched
}

// --------------------------------------------------------------------------
// Shader Compilation Flag Tests
// --------------------------------------------------------------------------

UTEST_F( Shader, CompileWithPatchDescriptorSetFlag )
{
    // Shader that uses unannotated resources (will default to DescriptorSet 0)
    const char* shaderSource = R"(
        struct Data { float4 val; };
        ConstantBuffer<Data> g_Constants;  // No explicit register space
        RWStructuredBuffer<Data> g_Output; // No explicit register space
        
        [numthreads(8, 4, 1)]
        void main(uint3 threadID : SV_DispatchThreadID)
        {
            g_Output[threadID.x].val = g_Constants.val;
        }
    )";

    // Compile WITHOUT the patch flag
    std::vector< uint32_t > spirvNoPatch;
    std::string error;
    bool success = vhCompileShader(
        "TestNoPatch",
        shaderSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_5,
        spirvNoPatch,
        "main",
        {},
        {},
        &error
    );
    
    if ( !success )
    {
        std::cout << "No-patch compilation failed: " << error << std::endl;
    }
    ASSERT_TRUE( success );
    EXPECT_GT( spirvNoPatch.size(), 0 );

    // Compile WITH the patch flag
    std::vector< uint32_t > spirvWithPatch;
    success = vhCompileShader(
        "TestWithPatch",
        shaderSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_5 | VRHI_SHADER_PATCH_DSET0,
        spirvWithPatch,
        "main",
        {},
        {},
        &error
    );
    
    if ( !success )
    {
        std::cout << "With-patch compilation failed: " << error << std::endl;
    }
    ASSERT_TRUE( success );
    EXPECT_GT( spirvWithPatch.size(), 0 );

    // Both should produce valid SPIR-V of the same size
    EXPECT_EQ( spirvNoPatch.size(), spirvWithPatch.size() );

    // Reflect both shaders and verify descriptor sets differ
    nvrhi::BindingLayoutDesc descNoPatch;
    std::vector< vhShaderReflectionResource > resourcesNoPatch;
    glm::uvec3 groupSizeNoPatch;
    std::vector< vhPushConstantRange > pushConstantsNoPatch;
    
    bool reflectedNoPatch = vhReflectSpirv( spirvNoPatch, descNoPatch, resourcesNoPatch, groupSizeNoPatch, pushConstantsNoPatch );
    ASSERT_TRUE( reflectedNoPatch );

    nvrhi::BindingLayoutDesc descWithPatch;
    std::vector< vhShaderReflectionResource > resourcesWithPatch;
    glm::uvec3 groupSizeWithPatch;
    std::vector< vhPushConstantRange > pushConstantsWithPatch;
    
    bool reflectedWithPatch = vhReflectSpirv( spirvWithPatch, descWithPatch, resourcesWithPatch, groupSizeWithPatch, pushConstantsWithPatch );
    ASSERT_TRUE( reflectedWithPatch );

    // Find the resources and verify their descriptor sets
    bool foundConstantsNoPatch = false;
    bool foundConstantsWithPatch = false;
    
    for ( const auto& res : resourcesNoPatch )
    {
        if ( res.name == "g_Constants" )
        {
            foundConstantsNoPatch = true;
            // Without patch flag, should be in set 0
            EXPECT_EQ( res.set, 0 );
        }
    }
    
    for ( const auto& res : resourcesWithPatch )
    {
        if ( res.name == "g_Constants" )
        {
            foundConstantsWithPatch = true;
            // With patch flag, should be in set VRHI_DESCRIPTOR_SET_COMPUTE (which is 1)
            EXPECT_EQ( res.set, VRHI_DESCRIPTOR_SET_COMPUTE );
        }
    }
    
    EXPECT_TRUE( foundConstantsNoPatch );
    EXPECT_TRUE( foundConstantsWithPatch );
}

UTEST_F( Shader, CompileWithPatchFlagVertexStage )
{
    // Vertex shader with unannotated constant buffer
    const char* shaderSource = R"(
        struct Globals { float4x4 mvp; };
        ConstantBuffer<Globals> g_Globals;
        
        struct VSInput { float3 pos : POSITION; };
        struct VSOutput { float4 pos : SV_Position; };
        
        VSOutput main(VSInput input)
        {
            VSOutput output;
            output.pos = mul( float4( input.pos, 1.0 ), g_Globals.mvp );
            return output;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "TestVSPatch",
        shaderSource,
        VRHI_SHADER_STAGE_VERTEX | VRHI_SHADER_SM_6_5 | VRHI_SHADER_PATCH_DSET0,
        spirv,
        "main",
        {},
        {},
        &error
    );
    
    if ( !success )
    {
        std::cout << "Vertex shader with patch flag compilation failed: " << error << std::endl;
    }
    
    ASSERT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );

    // Reflect and verify the resource is in the correct set
    nvrhi::BindingLayoutDesc desc;
    std::vector< vhShaderReflectionResource > resources;
    glm::uvec3 groupSize;
    std::vector< vhPushConstantRange > pushConstants;
    
    bool reflected = vhReflectSpirv( spirv, desc, resources, groupSize, pushConstants );
    ASSERT_TRUE( reflected );

    bool foundGlobals = false;
    for ( const auto& res : resources )
    {
        if ( res.name.find( "Globals" ) != std::string::npos || res.name == "g_Globals" || res.name == "$Globals" )
        {
            foundGlobals = true;
            // With patch flag on vertex shader, should be in set VRHI_DESCRIPTOR_SET_VERTEX (which is 1)
            EXPECT_EQ( res.set, VRHI_DESCRIPTOR_SET_VERTEX );
        }
    }
    
    EXPECT_TRUE( foundGlobals );
}

UTEST_F( Shader, CompileWithPatchFlagBareUniforms )
{
    // Pixel shader with bare uniforms (no struct wrapper)
    // These should be collected into globalParams/$Globals by the compiler
    const char* shaderSource = R"(
        uniform float4 u_colour;  // Bare uniform, no struct
        uniform float u_time;     // Another bare uniform
        
        float4 main() : SV_Target
        {
            return u_colour * u_time;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "TestBareUniformsPatch",
        shaderSource,
        VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_5 | VRHI_SHADER_PATCH_DSET0,
        spirv,
        "main",
        {},
        {},
        &error
    );
    
    if ( !success )
    {
        std::cout << "Bare uniforms shader with patch flag compilation failed: " << error << std::endl;
    }
    
    ASSERT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );

    // Reflect and verify the global uniform buffer is in the correct set
    nvrhi::BindingLayoutDesc desc;
    std::vector< vhShaderReflectionResource > resources;
    glm::uvec3 groupSize;
    std::vector< vhPushConstantRange > pushConstants;
    
    bool reflected = vhReflectSpirv( spirv, desc, resources, groupSize, pushConstants );
    ASSERT_TRUE( reflected );

    bool foundGlobalParams = false;
    for ( const auto& res : resources )
    {
        // Bare uniforms get collected into globalParams/$Globals
        if ( res.type == nvrhi::ResourceType::ConstantBuffer && 
             ( res.name == "globalParams" || res.name == "$Globals" || res.name == "_Globals" ) )
        {
            foundGlobalParams = true;
            // With patch flag on pixel shader, should be in set VRHI_DESCRIPTOR_SET_PIXEL (which is 2)
            EXPECT_EQ( res.set, VRHI_DESCRIPTOR_SET_PIXEL );
            
            // Verify the members are present
            bool foundColour = false;
            bool foundTime = false;
            for ( const auto& member : res.members )
            {
                if ( member.name == "u_colour" )
                {
                    foundColour = true;
                    EXPECT_EQ( member.size, 16 );  // float4 is 16 bytes
                }
                else if ( member.name == "u_time" )
                {
                    foundTime = true;
                    EXPECT_EQ( member.size, 4 );  // float is 4 bytes
                }
            }
            EXPECT_TRUE( foundColour );
            EXPECT_TRUE( foundTime );
        }
    }
    
    EXPECT_TRUE( foundGlobalParams );
}

// Test shader compilation flags work correctly
UTEST_F( Shader, CompileFlags )
{
    const char* shaderSource = R"(
        uniform float4 g_colour;

        float4 main() : SV_Target
        {
            return g_colour;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;

    // Test with DEBUG flag
    {
        bool success = vhCompileShader(
            "TestDebugFlag",
            shaderSource,
            VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0 | VRHI_SHADER_DEBUG,
            spirv,
            "main",
            {},
            {},
            &error
        );
        ASSERT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }

    // Test with ROW_MAJOR flag
    {
        spirv.clear();
        bool success = vhCompileShader(
            "TestRowMajorFlag",
            shaderSource,
            VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0 | VRHI_SHADER_ROW_MAJOR,
            spirv,
            "main",
            {},
            {},
            &error
        );
        ASSERT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }

    // Test with STRIP_REFLECTION flag
    {
        spirv.clear();
        bool success = vhCompileShader(
            "TestStripReflectionFlag",
            shaderSource,
            VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0 | VRHI_SHADER_STRIP_REFLECTION,
            spirv,
            "main",
            {},
            {},
            &error
        );
        ASSERT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }

    // Test with ALL_RESOURCES_BOUND flag
    {
        spirv.clear();
        bool success = vhCompileShader(
            "TestAllResourcesBoundFlag",
            shaderSource,
            VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0 | VRHI_SHADER_ALL_RESOURCES_BOUND,
            spirv,
            "main",
            {},
            {},
            &error
        );
        ASSERT_TRUE( success );
        EXPECT_GT( spirv.size(), 0 );
    }
}

// Test skipShaderCacheWrite prevents file creation
UTEST_F( Shader, SkipShaderCacheWrite )
{
    const char* shaderSource = R"(
        uniform float4 g_colour;

        float4 main() : SV_Target
        {
            return g_colour;
        }
    )";

    // Enable skipShaderCacheWrite
    bool originalSkipValue = g_vhInit.skipShaderCacheWrite;
    g_vhInit.skipShaderCacheWrite = true;

    // Clean up any existing cache files for this test
    std::filesystem::path tempDir = g_vhInit.shaderCompileTempDir;
    std::string testShaderName = "TestSkipCacheWrite";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        testShaderName.c_str(),
        shaderSource,
        VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0,
        spirv,
        "main",
        {},
        {},
        &error
    );

    // Restore original value
    g_vhInit.skipShaderCacheWrite = originalSkipValue;

    ASSERT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );

    // Verify no .spirv file was created
    // The filename includes a hash, so we need to search for it
    bool foundSpirvFile = false;
    std::string expectedPrefix = testShaderName + "_";
    if ( std::filesystem::exists( tempDir ) )
    {
        for ( const auto& entry : std::filesystem::directory_iterator( tempDir ) )
        {
            std::string filename = entry.path().filename().string();
            if ( filename.starts_with( expectedPrefix ) && filename.ends_with( ".spirv" ) )
            {
                foundSpirvFile = true;
                break;
            }
        }
    }

    EXPECT_FALSE( foundSpirvFile );

    // Clean up the .slang source file that was created
    for ( const auto& entry : std::filesystem::directory_iterator( tempDir ) )
    {
        std::string filename = entry.path().filename().string();
        if ( filename.starts_with( expectedPrefix ) && filename.ends_with( ".slang" ) )
        {
            std::filesystem::remove( entry.path() );
        }
    }
}

// Test dumpShaderSource creates source files when enabled
UTEST_F( Shader, DumpShaderSource )
{
    const char* shaderSource = R"(
        uniform float4 g_colour;

        float4 main() : SV_Target
        {
            return g_colour;
        }
    )";

    // Enable dumpShaderSource
    bool originalDumpValue = g_vhInit.dumpShaderSource;
    g_vhInit.dumpShaderSource = true;

    std::filesystem::path tempDir = g_vhInit.shaderCompileTempDir;
    std::string testShaderName = "TestDumpShaderSource";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        testShaderName.c_str(),
        shaderSource,
        VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0,
        spirv,
        "main",
        {},
        {},
        &error
    );

    // Restore original value
    g_vhInit.dumpShaderSource = originalDumpValue;

    ASSERT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );

    // Verify .slang file was created
    bool foundSlangFile = false;
    std::string expectedPrefix = testShaderName + "_";
    if ( std::filesystem::exists( tempDir ) )
    {
        for ( const auto& entry : std::filesystem::directory_iterator( tempDir ) )
        {
            std::string filename = entry.path().filename().string();
            if ( filename.starts_with( expectedPrefix ) && filename.ends_with( ".slang" ) )
            {
                foundSlangFile = true;
                // Clean it up
                std::filesystem::remove( entry.path() );
                break;
            }
        }
    }

    EXPECT_TRUE( foundSlangFile );

    // Clean up any .spirv file that was created
    for ( const auto& entry : std::filesystem::directory_iterator( tempDir ) )
    {
        std::string filename = entry.path().filename().string();
        if ( filename.starts_with( expectedPrefix ) && filename.ends_with( ".spirv" ) )
        {
            std::filesystem::remove( entry.path() );
        }
    }
}

// Test SPIRV optimisation with O3 flag
UTEST_F( Shader, CompileWithO3Optimisation )
{
    // Force recompilation to ensure we actually test the compilation path, not cache
    bool oldForceRecompile = g_vhInit.forceShaderRecompile;
    g_vhInit.forceShaderRecompile = true;
    const char* shaderSource = R"(
        uniform float4 g_colour;

        float4 main() : SV_Target
        {
            float4 colour = g_colour;
            // Dead code that should be eliminated by optimiser
            float unused = colour.x + colour.y;
            float4 result = colour;
            return result;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    
    // Compile with O3 optimisation (no VRHI_SHADER_DEBUG flag)
    bool success = vhCompileShader(
        "TestO3Optimise",
        shaderSource,
        VRHI_SHADER_STAGE_PIXEL | VRHI_SHADER_SM_6_0,  // No VRHI_SHADER_DEBUG
        spirv,
        "main",
        {},
        {},
        &error
    );

    ASSERT_TRUE( success );
    EXPECT_GT( spirv.size(), 0 );
    
    // Verify it's valid SPIRV (magic number)
    EXPECT_EQ( spirv[0], 0x07230203 );

    // Check no downstream compiler warnings present
    EXPECT_EQ( error.find( "failed to load downstream compiler" ), std::string::npos );

    // Restore previous force recompile setting
    g_vhInit.forceShaderRecompile = oldForceRecompile;
}

UTEST_F( Shader, UnrollAccumulator_OptimiserPreservesUseBeforeDef )
{
    // Layout:
    //   2x2 [unroll] loop with three parallel FAdd accumulators (scalar, vec3, scalar),
    //   inner body depends on (dx,dy) and has a conditional that produces re-orderable
    //   branches (the trigger shape for the BlockMergePass / DeadBranchElim interaction
    //   that produced an ID-N-not-defined OpFAdd on a previous SPIRV-Tools pin).

    static const char* src = R"HLSL(
        [numthreads(8, 8, 1)]
        void main(uint3 dtid : SV_DispatchThreadID)
        {
            float  aoSum = 0.0f;
            float3 giSum = float3(0.0, 0.0, 0.0);
            float  wSum  = 0.0f;

            [unroll]
            for (int dy = 0; dy < 2; dy++)
            {
                [unroll]
                for (int dx = 0; dx < 2; dx++)
                {
                    float wTap = saturate( float(dx + dy) * 0.5f + 0.1f );
                    if (wTap < 0.0001f) wTap = 0.0001f;

                    aoSum += wTap;
                    giSum += wTap.xxx;
                    wSum  += wTap;
                }
            }

            if (aoSum > 100.0f) aoSum = giSum.x + wSum;
        }
    )HLSL";

    std::vector< uint32_t > spirvDebug;
    std::string error;
    bool compiled = vhCompileShader(
        "UnrollAccumRepro",
        src,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0 | VRHI_SHADER_DEBUG,
        spirvDebug,
        "main",
        {},
        {},
        &error
    );
    ASSERT_TRUE_MSG( compiled, error.c_str() );
    ASSERT_GT( spirvDebug.size(), 0u );
    EXPECT_EQ( spirvDebug[0], 0x07230203u );

    auto fnCollectError = []( std::string& sink )
    {
        return [&sink]( spv_message_level_t, const char*, const spv_position_t&, const char* message )
        {
            if ( !sink.empty() ) sink += "\n";
            sink += message;
        };
    };

    std::string preOptError;
    spvtools::SpirvTools preOpt( SPV_ENV_VULKAN_1_3 );
    preOpt.SetMessageConsumer( fnCollectError( preOptError ) );
    ASSERT_TRUE_MSG( preOpt.Validate( spirvDebug.data(), spirvDebug.size() ), preOptError.c_str() );

    std::vector< uint32_t > spirvOptimised;
    std::string optError;
    {
        spvtools::Optimizer optimiser( SPV_ENV_VULKAN_1_3 );
        optimiser.SetMessageConsumer( fnCollectError( optError ) );
        optimiser.RegisterPerformancePasses();
        ASSERT_TRUE_MSG( optimiser.Run( spirvDebug.data(), spirvDebug.size(), &spirvOptimised ), optError.c_str() );
    }

    std::string postOptError;
    spvtools::SpirvTools postOpt( SPV_ENV_VULKAN_1_3 );
    postOpt.SetMessageConsumer( fnCollectError( postOptError ) );
    ASSERT_TRUE_MSG( postOpt.Validate( spirvOptimised.data(), spirvOptimised.size() ), postOptError.c_str() );

    std::vector< uint32_t > spirvFull;
    compiled = vhCompileShader(
        "UnrollAccumRepro",
        src,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirvFull,
        "main",
        {},
        {},
        &error
    );
    ASSERT_TRUE_MSG( compiled, error.c_str() );
    ASSERT_GT( spirvFull.size(), 0u );

    std::string fullError;
    spvtools::SpirvTools fullVal( SPV_ENV_VULKAN_1_3 );
    fullVal.SetMessageConsumer( fnCollectError( fullError ) );
    ASSERT_TRUE_MSG( fullVal.Validate( spirvFull.data(), spirvFull.size() ), fullError.c_str() );
}

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
#include <vrhi.h>
#include <vrhi_internal.h>

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
extern std::string vhBuildShaderFlagArgs_Internal( uint64_t flags );
extern bool vhRunExe( const std::string& command, std::string& outOutput );

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

UTEST_F( Shader, BuildFlags )
{
    // Test 1: Default/Release
    {
        uint64_t flags = 0;
        std::string args = vhBuildShaderFlagArgs_Internal( flags );
        // ShaderMake uses -m for model, profile is in config file
        EXPECT_TRUE( args.find( "-m 6_5" ) != std::string::npos );
        EXPECT_TRUE( args.find( "-O 3" ) != std::string::npos );
    }

    // Test 2: Debug & SM 6.0 & Vertex
    {
        uint64_t flags = VRHI_SHADER_DEBUG | VRHI_SHADER_SM_6_0 | VRHI_SHADER_STAGE_VERTEX;
        std::string args = vhBuildShaderFlagArgs_Internal( flags );
        EXPECT_TRUE( args.find( "-m 6_0" ) != std::string::npos );
        EXPECT_TRUE( args.find( "-O 0" ) != std::string::npos );
        EXPECT_TRUE( args.find( "--embedPDB" ) != std::string::npos );
    }

    // Test 3: Matrix & Warnings
    {
        uint64_t flags = VRHI_SHADER_ROW_MAJOR | VRHI_SHADER_WARNINGS_AS_ERRORS;
        std::string args = vhBuildShaderFlagArgs_Internal( flags );
        EXPECT_TRUE( args.find( "--matrixRowMajor" ) != std::string::npos );
        EXPECT_TRUE( args.find( "--WX" ) != std::string::npos );
    }
}

UTEST_F( Shader, RunExe )
{
    std::string output;
    bool success = vhRunExe( "echo HelloVRHI", output );
    EXPECT_TRUE( success );
    EXPECT_TRUE( output.find( "HelloVRHI" ) != std::string::npos );
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
float3 u_float3;
float4 u_float4;

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

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


#ifdef _WIN32
#include <windows.h>
#endif
#include "utest.h"

#define VRHI_UNIT_TEST
#define VRHI_SHADER_COMPILER
#ifdef VRHI_SHARDED_BUILD
    #include "vrhi_impl_backend.h"
    // Backend state defined here in sharded builds, in vrhi_impl.h for unity builds
    vhCmdBackendState g_vhCmdBackendState;
#endif

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;
extern bool vhBackend_UNITTEST_GetFrameBuffer( const std::vector< vhTexture >& colours, vhTexture depth );

#ifdef VRHI_UNIT_TEST
class vhCmdBackendStateTest
{
public:
    static vhCmdBackendState& Get() 
    { 
        static vhCmdBackendState s_instance;
        return s_instance; 
    }

    static bool Util_ShaderStageMatches( uint64_t flags, bool useCompute, bool useGraphics )
    {
        return Get().BE_Util_ShaderStageMatches( flags, useCompute, useGraphics );
    }

    static int32_t Util_ResolveBindingSlot( const char* name, nvrhi::ResourceType type, vhBackendShader& shader )
    {
        return Get().BE_Util_ResolveBindingSlot( name, type, shader );
    }

    // Direct access to state maps
    static void InsertDummyTexture( vhTexture handle, vhBackendTexture* tex )
    {
        Get().backendTextures[handle].reset( tex );
    }
    
    // Test access for Pipeline Validation
    static bool PreSubmitCommon_PipelineDesc( 
        vhState& state, 
        vhBackendShader* shaders, 
        int shaderCount,
        nvrhi::ComputePipelineDesc* compute,
        nvrhi::GraphicsPipelineDesc* graphics
        )
    {
        return Get().BE_PresubmitCommon_PipelineDesc( state, shaders, shaderCount, compute, graphics );
    }
};
#endif


UTEST( BackendInternal, ShaderStageMatches )
{
    // Compute must be compute only
    EXPECT_TRUE( vhCmdBackendStateTest::Util_ShaderStageMatches( VRHI_SHADER_STAGE_COMPUTE, true, false ) );
    EXPECT_FALSE( vhCmdBackendStateTest::Util_ShaderStageMatches( VRHI_SHADER_STAGE_COMPUTE, false, true ) );

    // Graphics stages
    EXPECT_TRUE( vhCmdBackendStateTest::Util_ShaderStageMatches( VRHI_SHADER_STAGE_VERTEX, false, true ) );
    EXPECT_TRUE( vhCmdBackendStateTest::Util_ShaderStageMatches( VRHI_SHADER_STAGE_PIXEL, false, true ) );
    EXPECT_FALSE( vhCmdBackendStateTest::Util_ShaderStageMatches( VRHI_SHADER_STAGE_VERTEX, true, false ) );

    // Mixed/Invalid flags (should fail strict checks if implemented, but primarily focusing on basic routing)
}

UTEST( BackendInternal, ResolveBindingSlot )
{
    vhBackendShader shader;
    vhShaderReflectionResource res1;
    res1.name = "MyTexture";
    res1.slot = 5;
    res1.type = nvrhi::ResourceType::Texture_SRV;
    
    vhShaderReflectionResource res2;
    res2.name = "MyBuffer";
    res2.slot = 8;
    res2.type = nvrhi::ResourceType::StructuredBuffer_SRV;
    
    shader.reflection.push_back( res1 );
    shader.reflection.push_back( res2 );

    // Known binding
    EXPECT_EQ( vhCmdBackendStateTest::Util_ResolveBindingSlot( "MyTexture", nvrhi::ResourceType::Texture_SRV, shader ), 5 );
    EXPECT_EQ( vhCmdBackendStateTest::Util_ResolveBindingSlot( "MyBuffer", nvrhi::ResourceType::StructuredBuffer_SRV, shader ), 8 );

    // Wrong Type
    EXPECT_EQ( vhCmdBackendStateTest::Util_ResolveBindingSlot( "MyTexture", nvrhi::ResourceType::Texture_UAV, shader ), -1 );

    // Unknown Name
    EXPECT_EQ( vhCmdBackendStateTest::Util_ResolveBindingSlot( "NonExistent", nvrhi::ResourceType::Texture_SRV, shader ), -1 );
}

UTEST( BackendInternal, PipelineValidation )
{
    // Construct dummy setup for PreSubmitCommon_PipelineDesc
    // We want to verify it catches layout mismatches or missing shaders without needing a full device
    
    vhState state;
    vhBackendShader shader;
    shader.name = "TestShader";
    shader.flags = VRHI_SHADER_STAGE_COMPUTE;
    shader.threadGroupSize = { 8, 8, 1 };
    
    // Add a resource requirement
    vhShaderReflectionResource res;
    res.name = "InTex";
    res.slot = 0;
    res.type = nvrhi::ResourceType::Texture_SRV;
    shader.reflection.push_back( res );

    nvrhi::ComputePipelineDesc computeDesc;
    
    // Case: Vertex Attribute Mismatch (Validation Failure)
    // We expect this to return FALSE because the shader expects an input (Loc 0) 
    // but the state has no vertex buffers bound.
    
    nvrhi::GraphicsPipelineDesc graphicsDesc;
    
    vhVertexLayoutDef inputDef;
    inputDef.format = nvrhi::Format::RGBA32_FLOAT;
    inputDef.location = 0;
    inputDef.offset = 0;
    shader.inputLayout.push_back(inputDef);
    shader.flags = VRHI_SHADER_STAGE_VERTEX; // Must be vertex to trigger input layout check

    // We must pass a valid shader pointer and count > 0 to satisfy assertions.
    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shader, 1, nullptr, &graphicsDesc ) );

    // Case 2: Valid shader, no pipeline desc
    // Returns true (success) because it simply matches no stages and exits cleanly.
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shader, 1, nullptr, nullptr ) );
}

UTEST( Backend, FramebufferCaching )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhTexture colour = vhAllocTexture();
    vhTexture depth = vhAllocTexture();

    vhCreateTexture2D( colour, glm::ivec2( 128, 128 ), 2, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhCreateTexture2D( depth, glm::ivec2( 128, 128 ), 2, nvrhi::Format::D24S8, VRHI_TEXTURE_RT );
    vhFinish();

    // Verify caching/deduplication
    EXPECT_TRUE( vhBackend_UNITTEST_GetFrameBuffer( { colour }, depth ) );

    vhDestroyTexture( colour );
    vhDestroyTexture( depth );
    vhFinish();
}
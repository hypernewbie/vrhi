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
#endif // _WIN32
#include "test.h"
#include <vrhi_internal.h>
#include <vrhi.h>
#include <vrhi_backend.h>
#include "test_gpu_gate.h"
#include <glm/gtc/matrix_transform.hpp>
#include <atomic>
#include <thread>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;
extern nvrhi::Format vhNvFormatFromVkFormat( VkFormat vkFormat );
extern vhCmdBackendState g_vhCmdBackendState;

class vhCmdBackendStateTest
{
public:
    static vhCmdBackendState& Get()
    {
        static vhCmdBackendState s_instance;
        return s_instance;
    }

    static void Init()
    {
        Get().init();
    }

    static void Shutdown()
    {
        Get().shutdown();
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

    static void InsertDummyBuffer( vhBuffer handle, vhBackendBuffer* buf )
    {
        Get().backendBuffers[handle].reset( buf );
    }

    // Test access for Pipeline Validation
    static bool PreSubmitCommon_PipelineDesc(
        vhState& state,
        vhBackendShader* const* shaders,
        int shaderCount,
        nvrhi::ComputePipelineDesc* compute,
        nvrhi::GraphicsPipelineDesc* graphics
    )
    {
        return Get().BE_PresubmitCommon_PipelineDesc( state, shaders, shaderCount, compute, graphics );
    }

    static bool PreSubmitCommon_State(
        vhState& state,
        vhBackendShader* const* shaders,
        int shaderCount,
        nvrhi::ComputeState* compute,
        nvrhi::GraphicsState* graphics
    )
    {
        return Get().BE_PreSubmitCommon_State( state, 0, shaders, shaderCount, compute, graphics );
    }

    static void PreSubmitCommon_ResolveStateCache(
        const vhState& state,
        vhBackendShader* const* shaders,
        int shaderCount,
        vhStateResolveCache& scache
    )
    {
        Get().BE_PreSubmitCommon_ResolveStateCache( state, shaders, shaderCount, scache );
    }

    static bool GetFrameBuffer( const std::vector< vhState::RenderTarget >& colors, const vhState::RenderTarget& depth )
    {
        auto fb1 = Get().BE_GetFrameBuffer( colors, depth, VRHI_INVALID_HANDLE );
        auto fb2 = Get().BE_GetFrameBuffer( colors, depth, VRHI_INVALID_HANDLE );

        if ( !fb1 || !fb2 ) return false;
        return fb1.Get() == fb2.Get();
    }

    static nvrhi::FramebufferHandle GetFrameBufferHandle( const std::vector< vhState::RenderTarget >& colors, const vhState::RenderTarget& depth )
    {
        return Get().BE_GetFrameBuffer( colors, depth, VRHI_INVALID_HANDLE );
    }

    static bool PreSubmitCommon_FindResource(
        const vhState& state,
        const uint32_t stage,
        const vhStateResolveCache& scache,
        const nvrhi::BindingLayoutItem& item,
        nvrhi::BindingSetItem& outItem
    )
    {
        return Get().BE_PreSubmitCommon_FindResource( state, stage, scache, item, outItem );
    }

    static int64_t Util_WriteGlobalUniform( const vhState& state, vhTransientBuffer& tbuf )
    {
        return Get().m_globalUniformCache.Write( state, tbuf );
    }

    static int64_t Util_WriteWorldUniform( const vhState& state, vhTransientBuffer& tbuf )
    {
        return Get().m_worldUniformCache.Write( state, tbuf );
    }

    static uint64_t GetDirtyBits( vhStateId id )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        // Use global backend state for accurate dirty bit check
        auto it = g_vhCmdBackendState.backendStates.find( id );
        if ( it != g_vhCmdBackendState.backendStates.end() )
            return it->second.dirty;
        return 0;
    }

    // Get vertex layout override for a specific state and stream
    static std::string GetVertexLayoutOverride( vhStateId id, uint8_t stream )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        auto it = g_vhCmdBackendState.backendStates.find( id );
        if ( it != g_vhCmdBackendState.backendStates.end() )
        {
            if ( stream < it->second.vertexBindings.size() )
                return it->second.vertexBindings[stream].layoutOverride;
        }
        return "";
    }

    static bool GetVertexBindingIsInstanced( vhStateId id, uint8_t stream )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        auto it = g_vhCmdBackendState.backendStates.find( id );
        if ( it != g_vhCmdBackendState.backendStates.end() )
        {
            if ( stream < it->second.vertexBindings.size() )
                return it->second.vertexBindings[stream].isInstanced;
        }
        return false;
    }

    static size_t GetVertexBindingsSize( vhStateId id )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        auto it = g_vhCmdBackendState.backendStates.find( id );
        if ( it != g_vhCmdBackendState.backendStates.end() )
            return it->second.vertexBindings.size();
        return 0;
    }
    static uint64_t GetGlobalResourceVersion()
    {
        return g_vhCmdBackendState.s_globalResourceVersion;
    }

    static uint64_t GetGlobalPipelineVersion()
    {
        return g_vhCmdBackendState.s_globalPipelineVersion;
    }

    static uint64_t GlobalPublishHash()
    {
        return Get().m_globalUniformCache.publication.bytesHash;
    }

    static uint64_t WorldPublishHash()
    {
        return Get().m_worldUniformCache.publication.bytesHash;
    }

    static uint64_t LayoutReuseTestCount()
    {
#ifndef NDEBUG
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        return g_vhCmdBackendState.s_layoutReuseTestCount;
#else
        return 0;
#endif
    }

};


uint64_t TestBindingLayoutReuseCount()
{
    return vhCmdBackendStateTest::LayoutReuseTestCount();
}


UTEST( BackendInternal, UniformSourceCache_WorldEffectiveCount )
{
    vhWorldUniformSources source;
    vhState full;
    full.worldMatrix = { glm::mat4( 1 ), glm::mat4( 2 ), glm::mat4( 3 ), glm::mat4( 4 ) };
    vhState shortState = full;
    shortState.worldMatrix.resize( 1 );

    EXPECT_FALSE( source.Matches( full ) );
    source.Capture( full );
    EXPECT_TRUE( source.Matches( full ) );
    EXPECT_FALSE( source.Matches( shortState ) );
    source.Capture( shortState );
    EXPECT_FALSE( source.Matches( full ) );

    full.worldMatrix.push_back( glm::mat4( 9 ) );
    source.Capture( full );
    full.worldMatrix.back() = glm::mat4( 8 );
    EXPECT_TRUE( source.Matches( full ) );

    full.worldMatrix.clear();
    source.Capture( full );
    EXPECT_TRUE( source.Matches( full ) );
    EXPECT_FALSE( source.Matches( shortState ) );
    source.Invalidate();
    EXPECT_FALSE( source.Matches( full ) );
}


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

UTEST( BackendInternal, VhNvFormatFromVkFormat_CoversSwapchainFormats )
{
    EXPECT_EQ( vhNvFormatFromVkFormat( VK_FORMAT_B8G8R8A8_UNORM ),           nvrhi::Format::BGRA8_UNORM );
    EXPECT_EQ( vhNvFormatFromVkFormat( VK_FORMAT_R8G8B8A8_UNORM ),           nvrhi::Format::RGBA8_UNORM );
    EXPECT_EQ( vhNvFormatFromVkFormat( VK_FORMAT_A2B10G10R10_UNORM_PACK32 ), nvrhi::Format::R10G10B10A2_UNORM );
    EXPECT_EQ( vhNvFormatFromVkFormat( VK_FORMAT_R16G16B16A16_SFLOAT ),      nvrhi::Format::RGBA16_FLOAT );
    EXPECT_EQ( vhNvFormatFromVkFormat( VK_FORMAT_B10G11R11_UFLOAT_PACK32 ),  nvrhi::Format::R11G11B10_FLOAT );
    EXPECT_EQ( vhNvFormatFromVkFormat( VK_FORMAT_R32G32B32A32_UINT ),        nvrhi::Format::UNKNOWN );
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
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Construct dummy setup for PreSubmitCommon_PipelineDesc
    // We want to verify it catches layout mismatches or missing shaders without needing a full device

    vhState state;
    vhBackendShader shader;
    shader.name = "TestShader";
    shader.flags = VRHI_SHADER_STAGE_COMPUTE;
    shader.threadGroupSize = { 8, 8, 1 };

    // Create a dummy layout to satisfy assertions in BE_PresubmitCommon_PipelineDesc
    nvrhi::BindingLayoutDesc layoutDesc = { .bindingOffsets = { 0, 0, 0, 0 } };
    layoutDesc.visibility = nvrhi::ShaderType::All;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

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
    shader.inputLayout.push_back( inputDef );
    shader.flags = VRHI_SHADER_STAGE_VERTEX; // Must be vertex to trigger input layout check

    // We must pass a valid shader pointer and count > 0 to satisfy assertions.
    vhBackendShader* shaderPtr = &shader;
    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shaderPtr, 1, nullptr, &graphicsDesc ) );

    // Case 2: Valid shader, no pipeline desc
    // Returns true (success) because it simply matches no stages and exits cleanly.
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shaderPtr, 1, nullptr, nullptr ) );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = nullptr;
    }
    vhFinish();
}

UTEST( BackendInternal, PreSubmitCommon_PipelineDesc_Compute )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state;
    vhBackendShader shader;
    shader.handle = nullptr; 
    shader.flags = VRHI_SHADER_STAGE_COMPUTE;
    
    nvrhi::BindingLayoutDesc layoutDesc = { .bindingOffsets = { 0, 0, 0, 0 } };
    layoutDesc.visibility = nvrhi::ShaderType::All;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::ComputePipelineDesc computeDesc;

    // Happy Path
    vhBackendShader* shaderPtr = &shader;
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shaderPtr, 1, &computeDesc, nullptr ) );
    EXPECT_EQ( computeDesc.CS.Get(), shader.handle.Get() );
    EXPECT_EQ( computeDesc.bindingLayouts.size(), 2u );
    EXPECT_EQ( computeDesc.bindingLayouts[1].Get(), shader.layout.Get() );

    // Mixed Shaders - Only compute should be picked up
    vhBackendShader shaders[2];
    vhBackendShader* shaderPtrs[2];
    shaders[0] = shader;
    shaders[1].handle = nullptr;
    shaders[1].flags = VRHI_SHADER_STAGE_VERTEX;
    shaderPtrs[0] = &shaders[0];
    shaderPtrs[1] = &shaders[1];
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shaders[1].layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::ComputePipelineDesc computeDesc2;
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, shaderPtrs, 2, &computeDesc2, nullptr ) );
    EXPECT_EQ( computeDesc2.CS.Get(), shaders[0].handle.Get() );
    EXPECT_EQ( computeDesc2.bindingLayouts.size(), 2u );
    EXPECT_EQ( computeDesc2.bindingLayouts[1].Get(), shaders[0].layout.Get() );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = nullptr;
        shaders[0].layout = nullptr;
        shaders[1].layout = nullptr;
    }
    vhFinish();
}

class MockComputePipeline : public nvrhi::RefCounter<nvrhi::IComputePipeline>
{
    nvrhi::ComputePipelineDesc desc;
public:
    MockComputePipeline( const nvrhi::BindingLayoutVector& layouts )
    {
        desc.bindingLayouts = layouts;
    }
    const nvrhi::ComputePipelineDesc& getDesc() const override { return desc; }
};

UTEST( BackendInternal, PreSubmitCommon_State_Compute )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping hardware-specific state validation in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state;
    vhBackendShader shader;
    shader.handle = nullptr; 
    shader.flags = VRHI_SHADER_STAGE_COMPUTE;
    
    // Create a real binding layout to satisfy getDesc() calls
    nvrhi::BindingLayoutDesc layoutDesc = { .bindingOffsets = { 0, 0, 0, 0 } };
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    
    shader.layoutDesc = layoutDesc;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    // Add reflection matching the layout
    vhShaderReflectionResource res = {};
    res.name = "TestTex";
    res.slot = 0;
    res.type = nvrhi::ResourceType::Texture_SRV;
    res.dim = nvrhi::TextureDimension::Texture2D;
    res.format = nvrhi::Format::RGBA8_UNORM;
    res.arraySize = 1;
    shader.reflection.push_back( res );

    nvrhi::BindingLayoutVector layouts;
    layouts.push_back( shader.layout );

    nvrhi::ComputePipelineHandle mockPipeline = nvrhi::ComputePipelineHandle::Create( new MockComputePipeline( layouts ) );
    nvrhi::ComputeState computeState;
    computeState.pipeline = mockPipeline;
    state.debugFlags = VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH;

    // CASE: Missing reflection (Should fail)
    nvrhi::BindingLayoutDesc layoutDescFail = { .bindingOffsets = { 0, 0, 0, 0 } };
    layoutDescFail.visibility = nvrhi::ShaderType::All;
    layoutDescFail.addItem( nvrhi::BindingLayoutItem::PushConstants( 1, 64 ) ); // Slot 1 not in reflection
    nvrhi::BindingLayoutHandle layoutFail;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layoutFail = g_vhDevice->createBindingLayout( layoutDescFail );
    }
    nvrhi::BindingLayoutVector layoutsFail;
    layoutsFail.push_back( layoutFail );
    
    // We must pass a shader that "owns" this layout
    vhBackendShader shaderFail = shader;
    shaderFail.layout = layoutFail;

    nvrhi::ComputePipelineHandle mockPipelineFail = nvrhi::ComputePipelineHandle::Create( new MockComputePipeline( layoutsFail ) );
    nvrhi::ComputeState computeStateFail;
    computeStateFail.pipeline = mockPipelineFail;

    vhBackendShader* shaderFailPtr = &shaderFail;
    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderFailPtr, 1, &computeStateFail, nullptr ) );

    // CASE: Happy Path
    vhBackendShader* shaderPtr = &shader;
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderPtr, 1, &computeState, nullptr ) );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layoutFail = nullptr;
        shader.layout = nullptr;
    }
    vhFinish();
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

    // Helper to register dummy textures into the test instance
    auto RegisterTex = []( vhTexture h, nvrhi::TextureDimension dim, uint32_t width, uint32_t height, uint32_t mips, uint32_t layers, nvrhi::Format fmt )
    {
        auto btex = new vhBackendTexture();
        nvrhi::TextureDesc desc;
        desc.dimension = dim;
        desc.width = width;
        desc.height = height;
        desc.mipLevels = mips;
        desc.arraySize = layers;
        desc.format = fmt;
        desc.isRenderTarget = true;
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            btex->handle = g_vhDevice->createTexture( desc );
        }
        btex->info.format = fmt;
        btex->info.mipLevels = mips;
        btex->info.arrayLayers = layers;
        btex->info.dimensions = { width, height, 1 };
        
        vhCmdBackendStateTest::InsertDummyTexture( h, btex );
    };

    RegisterTex( colour, nvrhi::TextureDimension::Texture2D, 128, 128, 1, 1, nvrhi::Format::RGBA8_UNORM );
    RegisterTex( depth, nvrhi::TextureDimension::Texture2D, 128, 128, 1, 1, nvrhi::Format::D32S8 );

    // Verify caching/deduplication
    vhState::RenderTarget rtColor;
    rtColor.texture = colour;
    vhState::RenderTarget rtDepth;
    rtDepth.texture = depth;
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtColor }, rtDepth ) );

    // Mip level tests : Needs matching depth buffer dimensions
    vhTexture colourMips = vhAllocTexture();
    RegisterTex( colourMips, nvrhi::TextureDimension::Texture2D, 128, 128, 4, 1, nvrhi::Format::RGBA8_UNORM );

    vhTexture depthMips = vhAllocTexture();
    RegisterTex( depthMips, nvrhi::TextureDimension::Texture2D, 128, 128, 4, 1, nvrhi::Format::D32S8 );

    vhState::RenderTarget rtMip0; rtMip0.texture = colourMips; rtMip0.mipLevel = 0;
    vhState::RenderTarget rtDepthMip0; rtDepthMip0.texture = depthMips; rtDepthMip0.mipLevel = 0;

    vhState::RenderTarget rtMip2; rtMip2.texture = colourMips; rtMip2.mipLevel = 2;
    vhState::RenderTarget rtDepthMip2; rtDepthMip2.texture = depthMips; rtDepthMip2.mipLevel = 2;

    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtMip0 }, rtDepthMip0 ) );
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtMip2 }, rtDepthMip2 ) );

    // Ensure they produce different FBOs
    {
        auto fb0 = vhCmdBackendStateTest::GetFrameBufferHandle( { rtMip0 }, rtDepthMip0 );
        auto fb2 = vhCmdBackendStateTest::GetFrameBufferHandle( { rtMip2 }, rtDepthMip2 );
        EXPECT_NE( fb0.Get(), fb2.Get() );
    }

    // Array layer tests
    vhTexture colourArray = vhAllocTexture();
    RegisterTex( colourArray, nvrhi::TextureDimension::Texture2DArray, 128, 128, 4, 10, nvrhi::Format::RGBA8_UNORM ); // Note: layers

    vhState::RenderTarget rtLayer0; rtLayer0.texture = colourArray; rtLayer0.arrayLayer = 0;
    vhState::RenderTarget rtLayer2; rtLayer2.texture = colourArray; rtLayer2.arrayLayer = 2;
    // Standard depth buffer is fine for layers as long as dimensions match (128x128)

    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtLayer0 }, rtDepth ) );
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtLayer2 }, rtDepth ) );

    {
        auto fb0 = vhCmdBackendStateTest::GetFrameBufferHandle( { rtLayer0 }, rtDepth );
        auto fb2 = vhCmdBackendStateTest::GetFrameBufferHandle( { rtLayer2 }, rtDepth );
        EXPECT_NE( fb0.Get(), fb2.Get() );
    }

    // Format override tests
    vhState::RenderTarget rtFormatDefault; rtFormatDefault.texture = colour;
    vhState::RenderTarget rtFormatOverride; rtFormatOverride.texture = colour; rtFormatOverride.formatOverride = nvrhi::Format::RGBA8_UNORM;

    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtFormatDefault }, rtDepth ) );
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtFormatOverride }, rtDepth ) );

    // Multiple render targets
    vhTexture colour2 = vhAllocTexture();
    RegisterTex( colour2, nvrhi::TextureDimension::Texture2D, 128, 128, 1, 1, nvrhi::Format::RGBA8_UNORM );

    vhState::RenderTarget rtColor2;
    rtColor2.texture = colour2;
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtColor, rtColor2 }, rtDepth ) );

    // Read-only tests
    vhState::RenderTarget rtDepthRO; rtDepthRO.texture = depth; rtDepthRO.readOnly = true;
    vhState::RenderTarget rtDepthRW; rtDepthRW.texture = depth; rtDepthRW.readOnly = false;

    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtColor }, rtDepthRO ) );
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { rtColor }, rtDepthRW ) );

    {
        auto fbRO = vhCmdBackendStateTest::GetFrameBufferHandle( { rtColor }, rtDepthRO );
        auto fbRW = vhCmdBackendStateTest::GetFrameBufferHandle( { rtColor }, rtDepthRW );
        EXPECT_NE( fbRO.Get(), fbRW.Get() );
        // Distinct handles are not enough: the read-only flag must actually
        // reach the framebuffer descriptor, or the depth attachment silently
        // becomes writable. This is the mechanism claim behind DS01's
        // read-only pass, checked here without inspecting public state.
        if ( fbRO && fbRW )
        {
            EXPECT_TRUE( fbRO->getDesc().depthAttachment.isReadOnly );
            EXPECT_FALSE( fbRW->getDesc().depthAttachment.isReadOnly );
        }
        EXPECT_TRUE( fbRO != nullptr );
        EXPECT_TRUE( fbRW != nullptr );
    }

    // Cleanup not strictly necessary for test instance but good practice
    vhCmdBackendStateTest::Shutdown();

    vhDestroyTexture( colour );
    vhDestroyTexture( colour2 );
    vhDestroyTexture( colourMips );
    vhDestroyTexture( depthMips );
    vhDestroyTexture( colourArray );
    vhDestroyTexture( depth );
    vhFinish();
}

UTEST( BackendInternal, FindResource )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping transient buffer mapping test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhCmdBackendStateTest::Init();

    // Create dummy resources
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "BackendTestTex", { 64, 64 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    
    vhBuffer buf = vhAllocBuffer();
    vhCreateStorageBuffer( buf, "TestBuf", nullptr, 1024, VRHI_BUFFER_COMPUTE_READ_WRITE, sizeof( uint32_t ) );

    vhFinish();

    nvrhi::TextureHandle hTex = vhGetTextureNvrhiHandle( tex );
    nvrhi::BufferHandle hBuf = vhGetBufferNvrhiHandle( buf );

    EXPECT_TRUE( hTex != nullptr );
    EXPECT_TRUE( hBuf != nullptr );

    // Setup State Binding (Dummy)
    vhState::TextureBinding texBind;
    texBind.texture = tex;
    texBind.formatOverride = nvrhi::Format::RGBA8_UNORM;
    texBind.subresources = nvrhi::TextureSubresourceSet( 0, 1, 0, 1 );

    vhState::BufferBinding bufBind;
    bufBind.buffer = buf;
    bufBind.byteOffset = 256;
    bufBind.byteSize = 512;

    // Setup Cache
    vhStateResolveCache scache;
    scache.init = true;
    
    // Resize btex/bbuf to satisfy size assertions
    // We need to attach them to a state to match sizes, 
    // but the test primarily mocks the scache logic manually.
    // The assertions check against 'state' passed in.
    vhState state;
    state.textures.resize( 1 );
    state.buffers.resize( 1 );
    scache.btex.resize( 1 );
    scache.bbuf.resize( 1 );

    // Mock Stage Binding
    uint32_t stage = VRHI_SHADER_STAGE_COMPUTE;
    scache.stageBindingActive[stage] = true;
    auto& stageTable = scache.stageBindingStorage[stage];

    // Resize vectors before indexing
    stageTable.textureTable.resize( 1 );
    stageTable.bufferTable.resize( 2 );
    stageTable.uavTable.resize( 4 );

    stageTable.textureTable[0] = { hTex, &texBind };
    stageTable.bufferTable[1] = { hBuf, &bufBind };
    stageTable.uavTable[2].first = { hTex, &texBind };
    stageTable.uavTable[3].second = { hBuf, &bufBind };
    
    // Set up system uniform slots for testing
    stageTable.globalUniformsSlot = g_vhInit.shaderMake_bRegShift + 0;
    stageTable.worldUniformsSlot = g_vhInit.shaderMake_bRegShift + 1;

    nvrhi::BindingSetItem outItem;

    // Test Global Uniform (Slot 0)
    nvrhi::BindingLayoutItem layoutGlobalUniform = nvrhi::BindingLayoutItem::ConstantBuffer( g_vhInit.shaderMake_bRegShift + 0 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutGlobalUniform, outItem ) );
    EXPECT_NE( outItem.resourceHandle, nullptr ); 
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::ConstantBuffer );
    EXPECT_EQ( outItem.range.byteSize, sizeof( vhGlobalUniform ) );

    // Test World Uniform (Slot 1)
    nvrhi::BindingLayoutItem layoutWorldUniform = nvrhi::BindingLayoutItem::ConstantBuffer( g_vhInit.shaderMake_bRegShift + 1 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutWorldUniform, outItem ) );
    EXPECT_NE( outItem.resourceHandle, nullptr );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::ConstantBuffer );
    EXPECT_EQ( outItem.range.byteSize, sizeof( vhWorldUniform ) );

    // Test Texture SRV
    nvrhi::BindingLayoutItem layoutTexSRV = nvrhi::BindingLayoutItem::Texture_SRV( 0 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutTexSRV, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hTex );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::Texture_SRV );
    EXPECT_EQ( outItem.format, nvrhi::Format::RGBA8_UNORM );

    // Test Buffer SRV
    nvrhi::BindingLayoutItem layoutBufSRV = nvrhi::BindingLayoutItem::RawBuffer_SRV( 1 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutBufSRV, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hBuf );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::RawBuffer_SRV );
    EXPECT_EQ( outItem.range.byteOffset, 256u );
    EXPECT_EQ( outItem.range.byteSize, 512u );

    // Test Structured Buffer SRV (Format::UNKNOWN allowed)
    nvrhi::BindingLayoutItem layoutStructBufSRV = nvrhi::BindingLayoutItem::StructuredBuffer_SRV( 1 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutStructBufSRV, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hBuf );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::StructuredBuffer_SRV );

    // Test Texture UAV
    nvrhi::BindingLayoutItem layoutTexUAV = nvrhi::BindingLayoutItem::Texture_UAV( 2 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutTexUAV, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hTex );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::Texture_UAV );

    // Test Buffer UAV
    nvrhi::BindingLayoutItem layoutBufUAV = nvrhi::BindingLayoutItem::RawBuffer_UAV( 3 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutBufUAV, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hBuf );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::RawBuffer_UAV );

    // Test Structured Buffer UAV (Format::UNKNOWN allowed)
    nvrhi::BindingLayoutItem layoutStructBufUAV = nvrhi::BindingLayoutItem::StructuredBuffer_UAV( 3 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutStructBufUAV, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hBuf );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::StructuredBuffer_UAV );

    // Test Sampler
    uint64_t samplerFlags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP;
    nvrhi::SamplerHandle hSampler = vhGetSamplerHandle( samplerFlags );
    EXPECT_TRUE( hSampler != nullptr );
    stageTable.samplerTable.resize( 5 );
    stageTable.samplerTable[4] = hSampler;

    nvrhi::BindingLayoutItem layoutSampler = nvrhi::BindingLayoutItem::Sampler( 4 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutSampler, outItem ) );
    EXPECT_EQ( outItem.resourceHandle, hSampler );
    EXPECT_EQ( outItem.type, nvrhi::ResourceType::Sampler );

    // Test Fail: Missing Slot
    nvrhi::BindingLayoutItem layoutMissing = nvrhi::BindingLayoutItem::Texture_SRV( 99 );
    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutMissing, outItem ) );

    nvrhi::BindingLayoutItem layoutMissingSampler = nvrhi::BindingLayoutItem::Sampler( 99 );
    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, layoutMissingSampler, outItem ) );
    
    vhCmdBackendStateTest::Shutdown();

    vhDestroyTexture( tex );
    vhDestroyBuffer( buf );
    vhFinish();
}

UTEST( BackendInternal, ResolveStateCacheFallbackSlots )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhCmdBackendStateTest::Init();

    vhTexture texSrv = vhAllocTexture();
    vhCreateTexture2D( texSrv, "FallbackSlotSRV", { 16, 16 }, 1, nvrhi::Format::RGBA8_UNORM );

    vhTexture texUav = vhAllocTexture();
    vhCreateTexture2D( texUav, "FallbackSlotUAV", { 16, 16 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    vhBuffer cbuf = vhAllocBuffer();
    vhCreateUniformBuffer( cbuf, "FallbackSlotCB", nullptr, 256 );

    vhFinish();

    nvrhi::TextureHandle hTexSrv = vhGetTextureNvrhiHandle( texSrv );
    nvrhi::TextureHandle hTexUav = vhGetTextureNvrhiHandle( texUav );
    nvrhi::BufferHandle hCbuf = vhGetBufferNvrhiHandle( cbuf );
    nvrhi::SamplerHandle hSampler = vhGetSamplerHandle( VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP );

    ASSERT_TRUE( hTexSrv != nullptr );
    ASSERT_TRUE( hTexUav != nullptr );
    ASSERT_TRUE( hCbuf != nullptr );
    ASSERT_TRUE( hSampler != nullptr );

    {
        auto btex = new vhBackendTexture();
        btex->name = "FallbackSlotSRV";
        btex->handle = hTexSrv;
        auto desc = hTexSrv->getDesc();
        btex->info.format = desc.format;
        btex->info.mipLevels = desc.mipLevels;
        btex->info.arrayLayers = desc.arraySize;
        btex->info.dimensions = { ( int ) desc.width, ( int ) desc.height, ( int ) desc.depth };
        vhCmdBackendStateTest::InsertDummyTexture( texSrv, btex );
    }
    {
        auto btex = new vhBackendTexture();
        btex->name = "FallbackSlotUAV";
        btex->handle = hTexUav;
        auto desc = hTexUav->getDesc();
        btex->info.format = desc.format;
        btex->info.mipLevels = desc.mipLevels;
        btex->info.arrayLayers = desc.arraySize;
        btex->info.dimensions = { ( int ) desc.width, ( int ) desc.height, ( int ) desc.depth };
        vhCmdBackendStateTest::InsertDummyTexture( texUav, btex );
    }
    {
        auto bbuf = new vhBackendBuffer();
        bbuf->name = "FallbackSlotCB";
        bbuf->handle = hCbuf;
        bbuf->desc = hCbuf->getDesc();
        vhCmdBackendStateTest::InsertDummyBuffer( cbuf, bbuf );
    }

    vhBackendShader shader;
    shader.flags = VRHI_SHADER_STAGE_COMPUTE;
    vhBackendShader* shaderPtr = &shader;
    const uint32_t stage = VRHI_SHADER_STAGE_COMPUTE;

    {
        vhState state;
        state.SetTexture( 0, texSrv, 0, nullptr, false );
        state.SetBuffer( 0, cbuf, 0, nullptr, 0, 0, false );

        vhStateResolveCache scache;
        int32_t startErrors = g_vhErrorCounter.load();
        vhCmdBackendStateTest::PreSubmitCommon_ResolveStateCache( state, &shaderPtr, 1, scache );
        EXPECT_TRUE( scache.init );
        EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

        if ( scache.init )
        {
            nvrhi::BindingSetItem outItem;
            EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, nvrhi::BindingLayoutItem::Texture_SRV( g_vhInit.shaderMake_tRegShift + 0 ), outItem ) );
            EXPECT_EQ( outItem.resourceHandle, hTexSrv );
            EXPECT_EQ( outItem.type, nvrhi::ResourceType::Texture_SRV );

            EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, nvrhi::BindingLayoutItem::ConstantBuffer( g_vhInit.shaderMake_bRegShift + 0 ), outItem ) );
            EXPECT_EQ( outItem.resourceHandle, hCbuf );
            EXPECT_EQ( outItem.type, nvrhi::ResourceType::ConstantBuffer );
        }
    }

    {
        vhState state;
        state.SetSampler( 0, VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP, 0, nullptr );

        vhStateResolveCache scache;
        int32_t startErrors = g_vhErrorCounter.load();
        vhCmdBackendStateTest::PreSubmitCommon_ResolveStateCache( state, &shaderPtr, 1, scache );
        EXPECT_TRUE( scache.init );
        EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

        if ( scache.init )
        {
            nvrhi::BindingSetItem outItem;
            EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, nvrhi::BindingLayoutItem::Sampler( g_vhInit.shaderMake_sRegShift + 0 ), outItem ) );
            EXPECT_EQ( outItem.resourceHandle, hSampler );
            EXPECT_EQ( outItem.type, nvrhi::ResourceType::Sampler );
        }
    }

    {
        vhState state;
        state.SetTexture( 0, texUav, 0, nullptr, true );

        vhStateResolveCache scache;
        int32_t startErrors = g_vhErrorCounter.load();
        vhCmdBackendStateTest::PreSubmitCommon_ResolveStateCache( state, &shaderPtr, 1, scache );
        EXPECT_TRUE( scache.init );
        EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

        if ( scache.init )
        {
            nvrhi::BindingSetItem outItem;
            EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_FindResource( state, stage, scache, nvrhi::BindingLayoutItem::Texture_UAV( g_vhInit.shaderMake_uRegShift + 0 ), outItem ) );
            EXPECT_EQ( outItem.resourceHandle, hTexUav );
            EXPECT_EQ( outItem.type, nvrhi::ResourceType::Texture_UAV );
        }
    }

    vhCmdBackendStateTest::Shutdown();

    vhDestroyTexture( texSrv );
    vhDestroyTexture( texUav );
    vhDestroyBuffer( cbuf );
    vhFinish();
}



UTEST( Backend, Util_WriteGlobalUniform )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping uniform buffer write test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Use a local transient buffer for testing logic
    vhTransientBuffer tb;
    nvrhi::BufferDesc desc;
    desc.setByteSize( 1024 * 1024 ); // 1MB
    desc.setIsConstantBuffer( true );
    desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
    desc.setDebugName( "TestTransient" );
    
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Init_DeviceStateLocked( desc );
    }

    vhState state = {};
    state.viewRect = glm::vec4( 0, 0, 100, 100 );

    // First Write
    int64_t offset1 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    
    EXPECT_GE( offset1, 0 );
    EXPECT_EQ( offset1, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );

    // Duplicate Write (Dedup)
    int64_t offset2 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    EXPECT_EQ( offset2, offset1 ); // Should reuse
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) ); // Should not advance

    // New Write (Mod state)
    state.viewRect = glm::vec4( 10, 10, 200, 200 );
    int64_t offset3 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    
    EXPECT_GT( offset3, offset1 );
    
    // Verify alignment
    EXPECT_EQ( offset3 % VRHI_CBUF_ALIGN, 0 );
    
    // Check it advanced
    EXPECT_GT( tb.offset, offset3 );

    // Create Staging Buffer
    nvrhi::BufferDesc stgDesc;
    stgDesc.setByteSize( sizeof( vhGlobalUniform ) );
    stgDesc.setCpuAccess( nvrhi::CpuAccessMode::Read );
    stgDesc.setDebugName( "TestStaging" );
    nvrhi::BufferHandle stagingBuffer = g_vhDevice->createBuffer( stgDesc );
    EXPECT_NE( stagingBuffer, nullptr );

    // Copy from Transient Buffer (at offset3) to Staging Buffer
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Flush_DeviceStateLocked();
        auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        cmdList->copyBuffer( stagingBuffer, 0, tb.handle[tb.frameIdx], offset3, sizeof( vhGlobalUniform ) );
    }

    // Flush and Wait — use vhFinish to keep all NVRHI calls on the backend thread.
    vhFinish();

    // Map and Verify
    void* pData = g_vhDevice->mapBuffer( stagingBuffer, nvrhi::CpuAccessMode::Read );
    EXPECT_NE( pData, nullptr );
    if ( pData )
    {
        vhGlobalUniform* u = ( vhGlobalUniform* ) pData;
        // vhWriteStateToGlobalUniform maps viewRect directly to u_viewRect
        EXPECT_EQ( u->u_viewRect.x, 10.0f );
        EXPECT_EQ( u->u_viewRect.y, 10.0f );
        EXPECT_EQ( u->u_viewRect.z, 200.0f );
        EXPECT_EQ( u->u_viewRect.w, 200.0f );

        g_vhDevice->unmapBuffer( stagingBuffer );
    }

    // Force buffer rollover logic simulation
    // Reset offset manually (simulate Step/Reset) - requires friend access or just calling Step?
    tb.Step();
    
    int64_t offset4 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    EXPECT_EQ( offset4, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );

    // Shutdown
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Shutdown_DeviceStateLocked();
    }
    vhFinish();
}

UTEST( Backend, Util_WriteGlobalUniform_FailedWriteNoDedupHit )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping uniform buffer write test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhTransientBuffer tb;
    nvrhi::BufferDesc desc;
    desc.setByteSize( sizeof( vhGlobalUniform ) );
    desc.setIsConstantBuffer( true );
    desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
    desc.setDebugName( "T01Transient" );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Init_DeviceStateLocked( desc );
    }

    vhState state = {};
    state.viewRect = glm::vec4( 0, 0, 100, 100 );
    state.viewMatrix = glm::mat4( 1.0f );

    int64_t offsetA = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    EXPECT_EQ( offsetA, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );
    const vhState stateA = state;
    const uint64_t hashA = vhCmdBackendStateTest::GlobalPublishHash();
    vhGlobalUniform expected;
    vhWriteStateToGlobalUniform( stateA, expected );

    // Capacity exhausted: any further write to this slot must fail.
    state.viewRect = glm::vec4( 10, 20, 300, 400 );
    state.viewMatrix = glm::translate( glm::mat4( 1.0f ), glm::vec3( 1, 2, 3 ) );

    int64_t offsetB1 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    EXPECT_EQ( offsetB1, -1 );

    // Re-attempting with the same rejected value must NOT resolve to the previous successful
    // offset. A dedup hit for bytes the buffer never received is a failure, not a return value.
    int64_t offsetB2 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
    EXPECT_EQ( offsetB2, -1 );
    EXPECT_EQ( vhCmdBackendStateTest::GlobalPublishHash(), hashA );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );
    EXPECT_EQ( vhCmdBackendStateTest::Util_WriteGlobalUniform( stateA, tb ), offsetA );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );

    // Stage the slot and verify offset 0 still contains A's bytes, not B's.
    nvrhi::BufferDesc stgDesc;
    stgDesc.setByteSize( sizeof( vhGlobalUniform ) );
    stgDesc.setCpuAccess( nvrhi::CpuAccessMode::Read );
    stgDesc.setDebugName( "T01Staging" );
    nvrhi::BufferHandle staging;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        staging = g_vhDevice->createBuffer( stgDesc );
        tb.Flush_DeviceStateLocked();
        auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        cmdList->copyBuffer( staging, 0, tb.handle[tb.frameIdx], 0, sizeof( vhGlobalUniform ) );
    }
    ASSERT_NE( staging, nullptr );

    vhFinish();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        const vhGlobalUniform* u = ( const vhGlobalUniform* ) g_vhDevice->mapBuffer( staging, nvrhi::CpuAccessMode::Read );
        ASSERT_NE( u, nullptr );
        EXPECT_EQ( u->u_viewRect.x, 0.0f );
        EXPECT_EQ( u->u_viewRect.y, 0.0f );
        EXPECT_EQ( u->u_viewRect.z, 100.0f );
        EXPECT_EQ( u->u_viewRect.w, 100.0f );
        EXPECT_EQ( memcmp( u, &expected, sizeof( expected ) ), 0 );
        g_vhDevice->unmapBuffer( staging );
        tb.Shutdown_DeviceStateLocked();
        staging = nullptr;
    }
    vhFinish();
}

UTEST( Backend, Util_WriteGlobalUniform_ExactCapacity )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping uniform buffer write test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    for ( uint32_t capacityRecords : { 1u, 2u } )
    {
        vhTransientBuffer tb;
        nvrhi::BufferDesc desc;
        desc.setByteSize( capacityRecords * sizeof( vhGlobalUniform ) );
        desc.setIsConstantBuffer( true );
        desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
        desc.setDebugName( "T08Transient" );
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            tb.Init_DeviceStateLocked( desc );
        }

        vhState state = {};
        state.viewRect = glm::vec4( 1, 2, 3, 4 );
        state.viewMatrix = glm::mat4( 1.0f );

        for ( uint32_t i = 0; i < capacityRecords; ++i )
        {
            state.viewRect = glm::vec4( ( float ) i + 1, 0, 0, 0 );
            int64_t offset = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
            EXPECT_EQ( offset, ( int64_t )( i * sizeof( vhGlobalUniform ) ) );
        }

        // Next record at exact-capacity: every byte is consumed, must fail with -1.
        state.viewRect = glm::vec4( 99, 0, 0, 0 );
        int64_t overflow = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
        EXPECT_EQ( overflow, -1 );
        EXPECT_EQ( tb.offset, ( int64_t )( capacityRecords * sizeof( vhGlobalUniform ) ) );

        // Step to the next slot; a write must succeed and start at offset 0.
        tb.Step();
        EXPECT_EQ( tb.offset, 0 );
        state.viewRect = glm::vec4( 7, 0, 0, 0 );
        int64_t reuseOffset = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb );
        EXPECT_EQ( reuseOffset, 0 );
        EXPECT_EQ( tb.offset, ( int64_t ) sizeof( vhGlobalUniform ) );

        // Stage the new slot at offset 0 and verify the last write's bytes are present.
        nvrhi::BufferDesc stgDesc;
        stgDesc.setByteSize( sizeof( vhGlobalUniform ) );
        stgDesc.setCpuAccess( nvrhi::CpuAccessMode::Read );
        stgDesc.setDebugName( "T08Staging" );
        nvrhi::BufferHandle staging;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            staging = g_vhDevice->createBuffer( stgDesc );
            tb.Flush_DeviceStateLocked();
            auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
            cmdList->copyBuffer( staging, 0, tb.handle[tb.frameIdx], 0, sizeof( vhGlobalUniform ) );
        }
        ASSERT_NE( staging, nullptr );
        vhFinish();
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            const vhGlobalUniform* u = ( const vhGlobalUniform* ) g_vhDevice->mapBuffer( staging, nvrhi::CpuAccessMode::Read );
            ASSERT_NE( u, nullptr );
            EXPECT_EQ( u->u_viewRect.x, 7.0f );
            g_vhDevice->unmapBuffer( staging );
            tb.Shutdown_DeviceStateLocked();
            staging = nullptr;
        }
        vhFinish();
    }
}

UTEST( Backend, Util_WriteWorldUniform_FailedWriteNoDedupHit )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping world uniform write test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhTransientBuffer tb;
    nvrhi::BufferDesc desc;
    desc.setByteSize( sizeof( vhWorldUniform ) );
    desc.setIsConstantBuffer( true );
    desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
    desc.setDebugName( "T01TransientWorld" );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Init_DeviceStateLocked( desc );
    }

    vhState state = {};
    state.viewMatrix = glm::mat4( 1.0f );
    state.worldMatrix.push_back( glm::mat4( 1.0f ) );

    int64_t offsetA = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
    EXPECT_EQ( offsetA, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );
    const vhState stateA = state;
    const uint64_t hashA = vhCmdBackendStateTest::WorldPublishHash();
    vhWorldUniform expected;
    vhWriteStateToWorldUniform( stateA, expected );

    state.worldMatrix[0] = glm::translate( glm::mat4( 1.0f ), glm::vec3( 1, 0, 0 ) );
    int64_t offsetB1 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
    EXPECT_EQ( offsetB1, -1 );

    int64_t offsetB2 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
    EXPECT_EQ( offsetB2, -1 );
    EXPECT_EQ( vhCmdBackendStateTest::WorldPublishHash(), hashA );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );
    EXPECT_EQ( vhCmdBackendStateTest::Util_WriteWorldUniform( stateA, tb ), offsetA );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );

    nvrhi::BufferDesc stgDesc;
    stgDesc.setByteSize( sizeof( vhWorldUniform ) );
    stgDesc.setCpuAccess( nvrhi::CpuAccessMode::Read );
    stgDesc.setDebugName( "T01WorldStaging" );
    nvrhi::BufferHandle staging;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        staging = g_vhDevice->createBuffer( stgDesc );
        tb.Flush_DeviceStateLocked();
        auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        cmdList->copyBuffer( staging, 0, tb.handle[tb.frameIdx], 0, sizeof( vhWorldUniform ) );
    }
    ASSERT_NE( staging, nullptr );
    vhFinish();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        const vhWorldUniform* u = ( const vhWorldUniform* ) g_vhDevice->mapBuffer( staging, nvrhi::CpuAccessMode::Read );
        ASSERT_NE( u, nullptr );
        EXPECT_EQ( memcmp( u, &expected, sizeof( expected ) ), 0 );
        g_vhDevice->unmapBuffer( staging );
        tb.Shutdown_DeviceStateLocked();
        staging = nullptr;
    }
    vhFinish();
}

UTEST( Backend, Util_WriteWorldUniform_ExactCapacity )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping world uniform write test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    for ( uint32_t capacityRecords : { 1u, 2u } )
    {
        vhTransientBuffer tb;
        nvrhi::BufferDesc desc;
        desc.setByteSize( capacityRecords * sizeof( vhWorldUniform ) );
        desc.setIsConstantBuffer( true );
        desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
        desc.setDebugName( "T08TransientWorld" );
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            tb.Init_DeviceStateLocked( desc );
        }

        vhState state = {};
        state.viewMatrix = glm::mat4( 1.0f );
        state.worldMatrix.push_back( glm::mat4( 1.0f ) );

        for ( uint32_t i = 0; i < capacityRecords; ++i )
        {
            state.worldMatrix[0] = glm::translate( glm::mat4( 1.0f ), glm::vec3( ( float ) i, 0, 0 ) );
            int64_t offset = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
            EXPECT_EQ( offset, ( int64_t )( i * sizeof( vhWorldUniform ) ) );
        }

        state.worldMatrix[0] = glm::translate( glm::mat4( 1.0f ), glm::vec3( 99, 0, 0 ) );
        int64_t overflow = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
        EXPECT_EQ( overflow, -1 );
        EXPECT_EQ( tb.offset, ( int64_t )( capacityRecords * sizeof( vhWorldUniform ) ) );

        tb.Step();
        state.worldMatrix[0] = glm::translate( glm::mat4( 1.0f ), glm::vec3( 7, 0, 0 ) );
        int64_t reuseOffset = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
        EXPECT_EQ( reuseOffset, 0 );

        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            tb.Shutdown_DeviceStateLocked();
        }
        vhFinish();
    }
}

UTEST( Backend, VertexIndexBufferBinding )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    vhCmdBackendStateTest::Init();

    // Test Single Vertex Buffer Binding
    vhBuffer vb = vhAllocBuffer();

    // Manually populate backend buffer for the test instance
    vhBackendBuffer* bbuf = new vhBackendBuffer();
    nvrhi::BufferDesc desc;
    desc.setByteSize( 1024 );
    desc.setIsVertexBuffer( true );
    desc.setDebugName( "VB1" );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        bbuf->handle = g_vhDevice->createBuffer( desc );
    }
    ASSERT_TRUE( bbuf->handle );
    bbuf->stride = 12;
    bbuf->flags = 0;
    vhCmdBackendStateTest::InsertDummyBuffer( vb, bbuf );
    
    // Add dummy RT for Framebuffer
    vhTexture rtTex = vhAllocTexture();
    {
        auto btex = new vhBackendTexture();
        nvrhi::TextureDesc tdesc;
        tdesc.width = 16; tdesc.height = 16;
        tdesc.format = nvrhi::Format::RGBA8_UNORM; tdesc.isRenderTarget = true;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            btex->handle = g_vhDevice->createTexture( tdesc );
        }
        btex->info.format = nvrhi::Format::RGBA8_UNORM;
        btex->info.mipLevels = 1; btex->info.arrayLayers = 1;
        btex->info.dimensions = { 16, 16, 1 };
        vhCmdBackendStateTest::InsertDummyTexture( rtTex, btex );
    }

    vhState state;
    vhState::RenderTarget rt; rt.texture = rtTex;
    state.colourAttachment.push_back( rt );
    state.SetVertexBuffer( vb, 0, 0, 0, ( 1024 / 12 ) );

    nvrhi::GraphicsState gstate;
    
    // Dummy shader required for PreSubmitCommon_State
    vhBackendShader shader;
    shader.flags = VRHI_SHADER_STAGE_VERTEX;
    vhBackendShader* shaderPtr = &shader;
    
    nvrhi::BindingLayoutDesc layoutDesc = {};
    layoutDesc.visibility = nvrhi::ShaderType::All;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = g_vhDevice->createBindingLayout( layoutDesc );
    }
    ASSERT_TRUE( shader.layout );
    
    nvrhi::BindingLayoutVector layouts;
    layouts.push_back( shader.layout );
    nvrhi::GraphicsPipelineDesc gdesc;
    gdesc.bindingLayouts = layouts;
    
    class MockGraphicsPipeline : public nvrhi::RefCounter<nvrhi::IGraphicsPipeline>
    {
        nvrhi::GraphicsPipelineDesc desc;
    public:
        MockGraphicsPipeline( const nvrhi::BindingLayoutVector& layouts ) { desc.bindingLayouts = layouts; }
        const nvrhi::GraphicsPipelineDesc& getDesc() const override { return desc; }
        const nvrhi::FramebufferInfo& getFramebufferInfo() const override { static nvrhi::FramebufferInfo i; return i; }
    };

    auto mgp = std::make_unique< MockGraphicsPipeline >( layouts );
    gstate.pipeline = mgp.get();
    
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderPtr, 1, nullptr, &gstate ) );
    
    EXPECT_EQ( gstate.vertexBuffers.size(), 1u );
    if ( gstate.vertexBuffers.size() > 0 )
    {
        EXPECT_EQ( gstate.vertexBuffers[0].buffer, bbuf->handle.Get() );
    }

    // Test Multiple Vertex Streams
    vhBuffer vb2 = vhAllocBuffer();
    vhBuffer vb3 = vhAllocBuffer();
    {
        {
            auto bbuf2 = new vhBackendBuffer();
            nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
            { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf2->handle = g_vhDevice->createBuffer( desc ); }
            bbuf2->stride = 12;
            bbuf2->flags = 0;
            vhCmdBackendStateTest::InsertDummyBuffer( vb2, bbuf2 );
        }

        {
            auto bbuf3 = new vhBackendBuffer();
            nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
            { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf3->handle = g_vhDevice->createBuffer( desc ); }
            bbuf3->stride = 12;
            bbuf3->flags = 0;
            vhCmdBackendStateTest::InsertDummyBuffer( vb3, bbuf3 );
        }
    }

    state.SetVertexBuffer( vb2, 1, 64 );
    state.SetVertexBuffer( vb3, 2, 128 );

    gstate.vertexBuffers = {}; // Reset
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderPtr, 1, nullptr, &gstate ) );
    EXPECT_EQ( gstate.vertexBuffers.size(), 3u );
    for( auto& v : gstate.vertexBuffers )
    {
        if ( v.slot == 0 ) EXPECT_EQ( v.offset, 0u );
        if ( v.slot == 1 ) EXPECT_EQ( v.offset, 64u );
        if ( v.slot == 2 ) EXPECT_EQ( v.offset, 128u );
    }

    // Test 32-bit Index Buffer
    vhBuffer ib32 = vhAllocBuffer();
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsIndexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->flags = VRHI_BUFFER_INDEX32;
        bbuf->stride = 4;
        vhCmdBackendStateTest::InsertDummyBuffer( ib32, bbuf );
    }

    state.SetIndexBuffer( ib32, 256 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderPtr, 1, nullptr, &gstate ) );
    
    EXPECT_NE( gstate.indexBuffer.buffer, nullptr );
    EXPECT_EQ( gstate.indexBuffer.offset, 256u );
    EXPECT_EQ( gstate.indexBuffer.format, nvrhi::Format::R32_UINT );

    // Test 16-bit Index Buffer
    vhBuffer ib16 = vhAllocBuffer();
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsIndexBuffer( true );
        {std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->flags = 0;
        bbuf->stride = 2;
        vhCmdBackendStateTest::InsertDummyBuffer( ib16, bbuf );
    }

    state.SetIndexBuffer( ib16, 128 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderPtr, 1, nullptr, &gstate ) );

    EXPECT_NE( gstate.indexBuffer.buffer, nullptr );
    EXPECT_EQ( gstate.indexBuffer.offset, 128u );
    EXPECT_EQ( gstate.indexBuffer.format, nvrhi::Format::R16_UINT );

    // Cleanup
    vhDestroyBuffer( vb );
    vhDestroyBuffer( vb2 );
    vhDestroyBuffer( vb3 );
    vhDestroyBuffer( ib32 );
    vhDestroyBuffer( ib16 );
    vhDestroyTexture( rtTex );
    
    gstate.bindings.fill( nullptr );
    gstate.pipeline = nullptr;
    gdesc.bindingLayouts.fill( nullptr );
    layouts.fill( nullptr );
    mgp.reset( nullptr );
    
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = nullptr;
    }
    vhBindingSetCacheClear();
    vhCmdBackendStateTest::Shutdown();
    vhFinish();
}

UTEST( Backend, TransientBufferMappedRanges )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
    const int32_t errorsBefore = g_vhErrorCounter.load();

    for ( uint32_t bytes : { 256u, 768u, 4096u } )
    {
        vhTransientBuffer tb;
        nvrhi::BufferDesc desc;
        desc.byteSize = bytes;
        desc.isConstantBuffer = true;
        desc.cpuAccess = nvrhi::CpuAccessMode::Write;
        nvrhi::BufferDesc stagingDesc;
        stagingDesc.byteSize = 16;
        stagingDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        nvrhi::BufferHandle staging;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            tb.Init_DeviceStateLocked( desc );
            staging = g_vhDevice->createBuffer( stagingDesc );
        }
        ASSERT_NE( staging, nullptr );
        const uint32_t expected[4] = { bytes, 17, 31, 47 };
        ASSERT_EQ( tb.Write( expected, sizeof( expected ) ), 0 );
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            tb.Flush_DeviceStateLocked();
            auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
            cmdList->copyBuffer( staging, 0, tb.handle[tb.frameIdx], 0, sizeof( expected ) );
        }
        vhFinish();
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            void* data = g_vhDevice->mapBuffer( staging, nvrhi::CpuAccessMode::Read );
            ASSERT_NE( data, nullptr );
            EXPECT_EQ( memcmp( data, expected, sizeof( expected ) ), 0 );
            g_vhDevice->unmapBuffer( staging );
            tb.Shutdown_DeviceStateLocked();
            staging = nullptr;
        }
    }
    EXPECT_EQ( g_vhErrorCounter.load(), errorsBefore );
}

UTEST( Backend, Util_WriteWorldUniform )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping world uniform write test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhTransientBuffer tb;
    nvrhi::BufferDesc desc;
    desc.setByteSize( 16 * 1024 * sizeof( vhWorldUniform ) );
    desc.setIsConstantBuffer( true );
    desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
    desc.setDebugName( "TestTransientWorld" );
    
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Init_DeviceStateLocked( desc );
    }

    vhState state = {};
    state.worldMatrix.push_back( glm::mat4( 1.0f ) ); // Identity

    // First Write
    int64_t offset1 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
    EXPECT_GE( offset1, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );

    // Dedup
    int64_t offset2 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
    EXPECT_EQ( offset2, offset1 );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );

    // Mod
    state.worldMatrix[0] = glm::translate( glm::mat4( 1.0f ), glm::vec3( 1, 0, 0 ) );
    int64_t offset3 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb );
    EXPECT_GT( offset3, offset1 );
    EXPECT_GT( tb.offset, offset3 );

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Shutdown_DeviceStateLocked();
    }
    vhFinish();
}

UTEST( Backend, PushConstantsDirtyBit )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();

    // Create a new state
    vhStateId sid = 1;
    
    // Set Push Constants
    glm::vec4 pcData( 10, 20, 30, 40 );
    vhCmdSetStatePushConstants( sid, pcData );
    
    // Flush to process commands
    vhFlush();
    
    // Check Dirty Bit
    uint64_t dirty = vhCmdBackendStateTest::GetDirtyBits( sid );
    EXPECT_NE( dirty & VRHI_DIRTY_PUSH_CONSTANTS, 0ull );
    
    // Set World Transform
    vhCmdSetStateWorldTransform( sid, { glm::mat4( 1.0f ) } );
    vhFlush();
    
    dirty = vhCmdBackendStateTest::GetDirtyBits( sid );
    EXPECT_NE( dirty & VRHI_DIRTY_WORLD, 0ull );
    
    // Test vhSetPushConstant execution (crash check)
    vhState state;
    vhGetState( sid, state );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        auto cmdlist = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        vhSetPushConstant_DeviceStateLocked( cmdlist, state );
    }
    vhFinish();
}


UTEST( Backend, TimerQueryBasic )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Timestamp queries not supported in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    {
        nvrhi::TimerQueryHandle timer = g_vhDevice->createTimerQuery();
        if ( !timer )
        {
            UTEST_SKIP( "Timestamp queries not supported on this device/backend" );
        }
    }

    vhTimerID timerID = 0x12345;

    // Initially should be 0.0f
    EXPECT_EQ( vhGetTimerQueryTime( timerID ), 0.0f );

    // Begin Timer
    vhBeginTimerQuery( timerID );

    // End Timer
    vhEndTimerQuery( timerID );

    vhFinish();

    // Still should be 0.0f because ring buffer delay (3 frames)
    EXPECT_EQ( vhGetTimerQueryTime( timerID ), 0.0f );

    // Simulate frames to advance ring buffer
    for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT + 1; ++i )
    {
        vhBeginTimerQuery( timerID );
        vhEndTimerQuery( timerID );
        vhFinish();
    }

    // Now we should have a result
    float time = vhGetTimerQueryTime( timerID );
#ifdef __APPLE__ // MoltenVK headless seems to create queries that don't work. timestampValidBits returns 0.
    if ( time == 0.0f ) { UTEST_SKIP( "Timestamp queries not producing results on this device/backend" ); }
#endif // __APPLE__
    EXPECT_GT( time, 0.0f );
    vhFinish();
}

UTEST( Backend, TimerQueryMultiple )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Timestamp queries not supported in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    {
        nvrhi::TimerQueryHandle timer = g_vhDevice->createTimerQuery();
        if ( !timer )
        {
            UTEST_SKIP( "Timestamp queries not supported on this device/backend" );
        }
    }

    vhTimerID timer1 = 0x111;
    vhTimerID timer2 = 0x222;

    for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT + 2; ++i )
    {
        vhBeginTimerQuery( timer1 );
        // Simulate some work
        vhBeginTimerQuery( timer2 );
        vhEndTimerQuery( timer2 );
        vhEndTimerQuery( timer1 );

        vhFinish();
    }

    float t1 = vhGetTimerQueryTime( timer1 );
    float t2 = vhGetTimerQueryTime( timer2 );
#ifdef __APPLE__ // MoltenVK headless seems to create queries that don't work. timestampValidBits returns 0.
    if ( t1 == 0.0f || t2 == 0.0f ) { UTEST_SKIP( "Timestamp queries not producing results on this device/backend" ); }
#endif // __APPLE__
    EXPECT_GT( t1, 0.0f );
    EXPECT_GT( t2, 0.0f );
    vhFinish();
}

UTEST( Backend, TimerQueryErrors )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // End without Begin (Should log error and not crash)
    vhEndTimerQuery( 0x999 );
    vhFinish();
    
    // Check invalid ID returns 0
    EXPECT_EQ( vhGetTimerQueryTime( 0x999 ), 0.0f );
    vhFinish();
}

UTEST( Backend, ComputeBasicBinding )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();

    vhTexture outTex = vhAllocTexture();
    vhCreateTexture2D( outTex, "SparseBindingsOut", { 4, 4 }, 1, nvrhi::Format::R8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );

    const char* csSource = R"(
        [[vk::image_format("r8")]] RWTexture2D<float> g_Out : register(u0, VRHI_STAGE_SPACE);

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = 1.0;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "CS_SparseBindings",
        csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirv,
        "main",
        {}, {}, &error
    );
    if ( !success ) UTEST_PRINTF( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_SparseBindings", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    vhState state = g_state0;
    state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
    state.SetProgram( vhCreateComputeProgram( cs ) );

    vhState::TextureBinding tb;
    tb.name = "g_Out";
    tb.texture = outTex;
    tb.dimensionOverride = nvrhi::TextureDimension::Texture2D;
    tb.formatOverride = nvrhi::Format::R8_UNORM;
    tb.computeUAV = true;
    state.SetTexture( 1, tb );

    vhStateId sid = 200;
    vhSetState( sid, state );
    vhDispatch( sid, { 1, 1, 1 } );

    vhDestroyShader( cs );
    vhDestroyTexture( outTex );
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhFinish();
}

UTEST( Backend, HeapAliasing )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Skipping texture readback test in Null RHI mode" );
    }

    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

#ifdef __APPLE__
    // MoltenVK/Metal does not support texture memory aliasing in the same way as Vulkan
    UTEST_SKIP( "Texture heap aliasing not supported on macOS/MoltenVK" );
#endif

    vhHeap heap = vhAllocHeap();
    ASSERT_TRUE( vhCreateHeap( heap, 64ull * 1024ull * 1024ull, "TestHeap" ) );
    vhFlush();

    vhTexture texA = vhAllocTexture();
    vhTexture texB = vhAllocTexture();

    vhCreateTexture2D( texA, "HeapAliasA", { 64, 64 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE | VRHI_TEXTURE_VIRTUAL );
    vhCreateTexture2D( texB, "HeapAliasB", { 64, 64 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE | VRHI_TEXTURE_VIRTUAL );
    vhFlush();

    glm::u64vec2 reqA = vhGetTextureMemoryRequirements( texA );
    ASSERT_GT( reqA.x, 0ull );
    ASSERT_GT( reqA.y, 0ull );

    glm::u64vec2 allocation = vhHeapAlloc( heap, reqA.x, reqA.y );
    ASSERT_GT( allocation.y, 0ull );

    vhBindTextureMemory( texA, heap, allocation.x );
    vhBindTextureMemory( texB, heap, allocation.x );
    vhFlush();

    const char* csSource = R"(
        [[vk::image_format("rgba8")]] RWTexture2D<float4> g_Out : register(u0, VRHI_STAGE_SPACE);

        cbuffer globalParams : register(b300, VRHI_STAGE_SPACE)
        {
            float4 g_Colour;
        };

        [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID)
        {
            g_Out[id.xy] = g_Colour;
        }
    )";

    std::vector< uint32_t > spirv;
    std::string error;
    bool success = vhCompileShader(
        "CS_HeapAlias",
        csSource,
        VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0,
        spirv,
        "main",
        {}, {}, &error
    );
    if ( !success ) printf( "Shader Compile Error: %s\n", error.c_str() );
    ASSERT_TRUE( success );

    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "CS_HeapAlias", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );

    auto DispatchWithColour = [&]( vhTexture tex, const glm::vec4& colour, vhStateId sid )
    {
        vhState state = g_state0;
        state.SetDebugFlags( VRHI_STATE_DEBUG_ALL );
        state.SetProgram( vhCreateComputeProgram( cs ) );

        vhState::TextureBinding tb;
        tb.name = "g_Out";
        tb.texture = tex;
        tb.dimensionOverride = nvrhi::TextureDimension::Texture2D;
        tb.formatOverride = nvrhi::Format::RGBA8_UNORM;
        tb.computeUAV = true;
        state.SetTexture( 0, tb );

        vhState::UniformBufferValue ub;
        ub.name = "g_Colour";
        ub.data = { colour };
        state.SetUniform( 0, ub );

        vhSetState( sid, state );
        vhDispatch( sid, { 64, 64, 1 } );
        vhFinish();
    };

    DispatchWithColour( texA, glm::vec4( 0.0f, 1.0f, 0.0f, 1.0f ), 310 );
    vhSetState( 310, g_state0, VRHI_DIRTY_ALL );
    DispatchWithColour( texB, glm::vec4( 1.0f, 0.0f, 0.0f, 1.0f ), 311 );
    vhSetState( 311, g_state0, VRHI_DIRTY_ALL );

    vhMem readData;
    vhReadTextureSlow( texA, 0, 0, &readData );
    vhFinish();

    vhTexInfo info = vhGetTextureInfo( texA );
    ASSERT_EQ( readData.size(), ( size_t ) info.dimensions.x * info.dimensions.y * 4 );

    uint8_t r = readData[0];
    uint8_t g = readData[1];
    uint8_t b = readData[2];
    uint8_t a = readData[3];
    EXPECT_GT( r, 200 );
    EXPECT_LT( g, 50 );
    EXPECT_LT( b, 50 );
    EXPECT_GT( a, 200 );

    vhDestroyShader( cs );
    vhSetState( 310, g_state0, VRHI_DIRTY_ALL );
    vhSetState( 311, g_state0, VRHI_DIRTY_ALL );
    vhDestroyTexture( texA );
    vhDestroyTexture( texB );
    vhDestroyHeap( heap );
    vhFinish();
}

UTEST( Backend, BufferHeapAllocation )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Create heap
    vhHeap heap = vhAllocHeap();
    ASSERT_TRUE( vhCreateHeap( heap, 64 * 1024 * 1024, "BufferHeap" ) );
    vhFlush();

    // Create virtual buffers
    vhBuffer bufA = vhAllocBuffer();
    vhBuffer bufB = vhAllocBuffer();

    // Create vertex buffer with VIRTUAL flag
    vhCreateVertexBuffer( bufA, "HeapBufferA", nullptr, "float3", 1000,
        VRHI_BUFFER_VIRTUAL | VRHI_BUFFER_ALLOW_RESIZE );

    // Create uniform buffer with VIRTUAL flag
    vhCreateUniformBuffer( bufB, "HeapBufferB", nullptr, 4096,
        VRHI_BUFFER_VIRTUAL );
    vhFlush();

    // Query memory requirements
    glm::u64vec2 reqA = vhGetBufferMemoryRequirements( bufA );
    ASSERT_GT( reqA.x, 0ull );
    ASSERT_EQ( reqA.y, 256ull ); // Standard 256-byte alignment

    glm::u64vec2 reqB = vhGetBufferMemoryRequirements( bufB );
    ASSERT_GT( reqB.x, 0ull );
    ASSERT_EQ( reqB.y, 256ull );

    // Allocate from heap
    glm::u64vec2 allocA = vhHeapAlloc( heap, reqA.x, reqA.y );
    ASSERT_GT( allocA.y, 0ull );

    glm::u64vec2 allocB = vhHeapAlloc( heap, reqB.x, reqB.y );
    ASSERT_GT( allocB.y, 0ull );

    // Verify allocations don't overlap
    uint64_t endA = allocA.x + reqA.x;
    uint64_t endB = allocB.x + reqB.x;
    bool overlap = ( allocA.x < endB ) && ( allocB.x < endA );
    ASSERT_FALSE( overlap );

    // Bind buffers to heap memory
    vhBindBufferMemory( bufA, heap, allocA.x );
    vhBindBufferMemory( bufB, heap, allocB.x );
    vhFlush();

    // Test buffer operations
    std::vector< uint8_t > testData( reqA.x, 0xAB );
    vhMem* uploadMem = vhAllocMem( testData );
    vhUpdateVertexBuffer( bufA, uploadMem, 0, 0 );
    vhFlush();

    // Test convenience wrapper
    vhBuffer bufC = vhAllocBuffer();
    vhCreateStorageBuffer( bufC, "HeapBufferC", nullptr, 8192,
        VRHI_BUFFER_VIRTUAL | VRHI_BUFFER_COMPUTE_READ_WRITE );
    vhFlush();

    glm::u64vec2 allocC = vhAllocBindBufferMemory( bufC, heap );
    ASSERT_GT( allocC.y, 0ull );
    vhFlush();

    // Cleanup
    vhDestroyBuffer( bufA );
    vhDestroyBuffer( bufB );
    vhDestroyBuffer( bufC );
    vhDestroyHeap( heap );
    vhFinish();
}

UTEST( Backend, BufferHeapAllocationErrorCases )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhHeap heap = vhAllocHeap();
    ASSERT_TRUE( vhCreateHeap( heap, 64 * 1024 * 1024, "ErrorTestHeap" ) );
    vhFlush();

    // Test invalid handle
    glm::u64vec2 result = vhGetBufferMemoryRequirements( VRHI_INVALID_HANDLE );
    EXPECT_EQ( result.x, 0ull );
    EXPECT_EQ( result.y, 0ull );

    // Test non-virtual buffer binding (should fail validation)
    vhBuffer nonVirtualBuf = vhAllocBuffer();
    vhCreateUniformBuffer( nonVirtualBuf, "NonVirtualBuf", nullptr, 1024, VRHI_BUFFER_NONE );
    vhFlush();

    // Try to bind non-virtual buffer - this should log an error but not crash
    vhBindBufferMemory( nonVirtualBuf, heap, 0 );
    vhFlush();

    // Test double-allocation prevention
    vhBuffer bufA = vhAllocBuffer();
    vhCreateUniformBuffer( bufA, "DoubleAllocTest", nullptr, 1024, VRHI_BUFFER_VIRTUAL );
    vhFlush();

    glm::u64vec2 req = vhGetBufferMemoryRequirements( bufA );
    glm::u64vec2 alloc = vhHeapAlloc( heap, req.x, req.y );
    ASSERT_GT( alloc.y, 0ull );

    vhBindBufferMemory( bufA, heap, alloc.x );
    vhFlush();

    // Cleanup
    vhDestroyBuffer( nonVirtualBuf );
    vhDestroyBuffer( bufA );
    vhDestroyHeap( heap );
    vhFinish();
}

UTEST( Backend, ProfilingCallback )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    static std::atomic<int> callbackCount = 0;
    callbackCount = 0;

    g_vhInit.fnProfileCallback = []( const char* name, bool begin )
    {
        callbackCount++;
    };

    // Perform an action that we know is profiled
    vhBuffer buf = vhAllocBuffer();
    vhCreateUniformBuffer( buf, "ProfileTestBuf", nullptr, 256 );
    
    vhFlush();
    vhFinish();

    // Expect at least 2 calls (begin/end for Handle_vhCreateUniformBuffer)
    EXPECT_GT( callbackCount.load(), 0 );

    // Cleanup (Reset test state)
    g_vhInit.fnProfileCallback = nullptr;
    vhDestroyBuffer( buf );
    vhFlush();
    vhFinish();
}

// End-to-end test for layout override transmission through the backend.
// This test verifies that layoutOverride set via SetVertexBuffer is actually
// received and stored by the backend when binding vertex buffers.
UTEST( Backend, VertexLayoutOverrideTransmission )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state;
    vhStateId sid = 9999; // Unique state ID for this test

    // Create vertex buffer with original layout: position + colour (28 bytes)
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "TestVBLayoutOverride", vhAllocMem( 1024 ), "float3 float4" );
    vhFlush();

    // Set up state with layout override to only use position (12 bytes)
    // This simulates a shader that only reads position, ignoring colour
    state.SetVertexBuffer( buf, 0, 0, 0, 100, "float3" );

    // Apply the state - this transmits the layoutOverride to backend
    vhSetState( sid, state );
    vhFlush(); // Ensure backend processes the command

    // Verify layoutOverride was transmitted to backend state
    std::string layoutOverride = vhCmdBackendStateTest::GetVertexLayoutOverride( sid, 0 );
    EXPECT_FALSE( layoutOverride.empty() );
    EXPECT_STREQ( layoutOverride.c_str(), "float3" );

    // Test with empty override - should remain empty after transmission
    vhState state2;
    vhStateId sid2 = 10000;
    state2.SetVertexBuffer( buf, 0, 0, 0, 100, nullptr );
    vhSetState( sid2, state2 );
    vhFlush();

    std::string layoutOverride2 = vhCmdBackendStateTest::GetVertexLayoutOverride( sid2, 0 );
    EXPECT_TRUE( layoutOverride2.empty() );

    // Cleanup
    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( Backend, VertexBindingInstancedTransmission )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "TestVBInstanced", vhAllocMem( 1024 ), "float4:i ATTR8 float4:i ATTR9" );
    vhFlush();

    vhState state;
    vhStateId sid = 9998;
    state.SetVertexBuffer( buf, 0, 0, 0, 100, "float4:i ATTR8 float4:i ATTR9" );
    state.vertexBindings[0].isInstanced = true;

    vhSetState( sid, state );
    vhFlush();

    bool isInstanced = vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid, 0 );
    EXPECT_TRUE( isInstanced );

    vhState state2;
    vhStateId sid2 = 9997;
    state2.SetVertexBuffer( buf, 0 );
    vhSetState( sid2, state2 );
    vhFlush();

    bool isInstanced2 = vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid2, 0 );
    EXPECT_FALSE( isInstanced2 );

    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST( Backend, VertexBindingInstancedAutoDetectFromLayout )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // 1) Auto-detect on: :i in layout string sets isInstanced without manual assignment
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "TestVBAutoDetect", vhAllocMem( 1024 ), "float4 ATTR0" );
    vhFlush();

    vhState state;
    vhStateId sid = 9990;
    state.SetVertexBuffer( buf, 1, 0, 0, 100, "float4:i ATTR8 float4:i ATTR9" );

    vhSetState( sid, state );
    vhFlush();

    EXPECT_TRUE( vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid, 1 ) );

    // 2) Auto-detect off: no :i in layout string keeps isInstanced false
    vhState state2;
    vhStateId sid2 = 9991;
    state2.SetVertexBuffer( buf, 0, 0, 0, 100, "float3 float2" );

    vhSetState( sid2, state2 );
    vhFlush();

    EXPECT_FALSE( vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid2, 0 ) );

    // 3) Null / empty layout: no crash, isInstanced stays false
    vhState state3;
    vhStateId sid3 = 9992;
    state3.SetVertexBuffer( buf, 2, 0, 0, 100, nullptr );

    vhSetState( sid3, state3 );
    vhFlush();

    EXPECT_FALSE( vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid3, 2 ) );

    vhState state4;
    vhStateId sid4 = 9993;
    state4.SetVertexBuffer( buf, 3, 0, 0, 100, "" );

    vhSetState( sid4, state4 );
    vhFlush();

    EXPECT_FALSE( vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid4, 3 ) );

    // 4) Sticky-state flip: :i then no :i on same state + same stream flips isInstanced back
    vhState state5;
    vhStateId sid5 = 9994;
    state5.SetVertexBuffer( buf, 0, 0, 0, 100, "float4:i ATTR8" );
    EXPECT_TRUE( state5.vertexBindings[0].isInstanced );
    state5.SetVertexBuffer( buf, 0, 0, 0, 100, "float3 ATTR0" );
    EXPECT_FALSE( state5.vertexBindings[0].isInstanced );

    vhSetState( sid5, state5 );
    vhFlush();

    EXPECT_FALSE( vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid5, 0 ) );

    // 5) Mixed-rate layout: any :i present sets isInstanced = true
    vhState state6;
    vhStateId sid6 = 9995;
    state6.SetVertexBuffer( buf, 0, 0, 0, 100, "float4:i ATTR8 float4 ATTR9" );

    vhSetState( sid6, state6 );
    vhFlush();

    EXPECT_TRUE( vhCmdBackendStateTest::GetVertexBindingIsInstanced( sid6, 0 ) );

    vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid2, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid3, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid4, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid5, g_state0, VRHI_DIRTY_ALL );
    vhSetState( sid6, g_state0, VRHI_DIRTY_ALL );
    vhDestroyBuffer( buf );
    vhFlush();
}

struct TestAllocDestructionMarker
{
    static constexpr uint64_t kMagic = 0xDEADBEEF;
    uint64_t MAGIC = kMagic;
    bool* destructorCalled;

    TestAllocDestructionMarker() = default;

    TestAllocDestructionMarker( bool* flag )
        : destructorCalled( flag )
    {
        *destructorCalled = false;
    }

    ~TestAllocDestructionMarker()
    {
        if ( destructorCalled )
            *destructorCalled = true;
    }
};

struct TestAllocOverflow
{
    static constexpr uint64_t kMagic = 0xCAFEBABE;
    uint64_t MAGIC = kMagic;
    uint8_t data[256];

    TestAllocOverflow() = default;
};

UTEST( Backend, CommandArenaInitShutdown )
{
    vhCommandArena arena;
    arena.Init();
    arena.Shutdown();
}

UTEST( Backend, CommandArenaBasicAllocRelease )
{
    vhCommandArena arena;
    arena.Init();

    for ( int i = 0; i < 1000; i++ )
    {
        void* mem = arena.Allocate( 64, 8 );
        ASSERT_TRUE( mem != nullptr );
    }

    arena.Shutdown();
}

UTEST( Backend, CommandArenaDestructorCalled )
{
    vhCommandArena arena;
    arena.Init();

    bool destroyed = false;
    {
        TestAllocDestructionMarker* cmd = static_cast< TestAllocDestructionMarker* >( arena.Allocate( sizeof( TestAllocDestructionMarker ), alignof( TestAllocDestructionMarker ) ) );
        new ( cmd ) TestAllocDestructionMarker( &destroyed );
        ASSERT_FALSE( destroyed );
        cmd->~TestAllocDestructionMarker();
        ASSERT_TRUE( destroyed );
    }

    arena.Shutdown();
}

UTEST( Backend, CommandArenaAlignment )
{
    vhCommandArena arena;
    arena.Init();

    for ( int i = 0; i < 1000; i++ )
    {
        void* mem = arena.Allocate( 31, 8 );
        ASSERT_TRUE( mem != nullptr );
        uintptr_t addr = reinterpret_cast< uintptr_t >( mem );
        EXPECT_EQ( addr % 8, 0ull );
    }

    arena.Shutdown();
}

UTEST( Backend, CommandArenaTripleBufferRotation )
{
    vhCommandArena arena;
    arena.Init();

    std::vector< void* > ptrs;
    for ( int rot = 0; rot < 5; rot++ )
    {
        for ( int i = 0; i < 100; i++ )
        {
            void* mem = arena.Allocate( 128, 8 );
            ptrs.push_back( mem );
        }
        arena.Rotate();
    }

    arena.Shutdown();
}

UTEST( Backend, CommandArenaVhCmdAllocRelease )
{
    vhCommandArena arena;
    arena.Init();

    bool destroyed = false;
    auto* cmd = static_cast< TestAllocDestructionMarker* >( arena.Allocate( sizeof( TestAllocDestructionMarker ), alignof( TestAllocDestructionMarker ) ) );
    ASSERT_TRUE( cmd != nullptr );

    new ( cmd ) TestAllocDestructionMarker( &destroyed );
    ASSERT_FALSE( destroyed );
    ASSERT_EQ( cmd->MAGIC, TestAllocDestructionMarker::kMagic );

    vhCmdRelease( cmd );
    ASSERT_TRUE( destroyed );

    arena.Shutdown();
}

UTEST( Backend, CommandArenaConcurrentAllocateRotate )
{
    vhCommandArena arena;
    arena.Init();

    std::atomic< bool > start = false;
    std::atomic< bool > producerDone = false;
    std::atomic< bool > failed = false;
    std::atomic< int > allocationCount = 0;

    std::thread producer( [&]() {
        while ( !start.load() ) std::this_thread::yield();

        for ( int i = 0; i < 50000; ++i )
        {
            size_t size = 32 + ( ( i % 8 ) * 16 );
            void* mem = arena.Allocate( size, 16 );
            if ( mem == nullptr )
            {
                failed.store( true );
                break;
            }

            uintptr_t addr = reinterpret_cast< uintptr_t >( mem );
            if ( addr % 16 != 0 )
            {
                failed.store( true );
                break;
            }

            uint8_t* bytes = static_cast< uint8_t* >( mem );
            bytes[0] = static_cast< uint8_t >( i & 0xFF );
            bytes[size - 1] = static_cast< uint8_t >( ( i * 7 ) & 0xFF );
            allocationCount.fetch_add( 1 );

            if ( ( i % 256 ) == 0 ) std::this_thread::yield();
        }

        producerDone.store( true );
    } );

    std::thread rotator( [&]() {
        while ( !start.load() ) std::this_thread::yield();

        for ( int i = 0; i < 6000; ++i )
        {
            arena.Rotate();
            if ( producerDone.load() ) break;
            if ( ( i % 16 ) == 0 ) std::this_thread::yield();
        }

        while ( !producerDone.load() )
        {
            arena.Rotate();
            std::this_thread::yield();
        }
    } );

    start.store( true );
    producer.join();
    rotator.join();

    EXPECT_FALSE( failed.load() );
    EXPECT_EQ( allocationCount.load(), 50000 );

    arena.Shutdown();
}

UTEST( Backend, CommandArenaInitShutdownChurn )
{
    for ( int cycle = 0; cycle < 64; ++cycle )
    {
        vhCommandArena arena;
        arena.Init();

        for ( int rot = 0; rot < 8; ++rot )
        {
            for ( int i = 0; i < 256; ++i )
            {
                void* mem = arena.Allocate( 128 + ( ( i % 4 ) * 32 ), 16 );
                ASSERT_TRUE( mem != nullptr );
            }

            arena.Rotate();
        }

        arena.Shutdown();
    }
}

UTEST( Backend, CommandArenaBoundaryPressure )
{
    vhCommandArena arena;
    arena.Init();

    constexpr size_t kChunkSize = 256 * 1024;
    for ( int rot = 0; rot < 6; ++rot )
    {
        for ( int i = 0; i < 16; ++i )
        {
            void* mem = arena.Allocate( kChunkSize, 64 );
            ASSERT_TRUE( mem != nullptr );

            uintptr_t addr = reinterpret_cast< uintptr_t >( mem );
            EXPECT_EQ( addr % 64, 0ull );

            uint8_t* bytes = static_cast< uint8_t* >( mem );
            bytes[0] = static_cast< uint8_t >( rot );
            bytes[kChunkSize - 1] = static_cast< uint8_t >( i );
        }

        arena.Rotate();
    }

    arena.Shutdown();
}

UTEST( Backend, CommandArenaOverflowFallback )
{
    vhCommandArena arena;
    arena.Init();

    void* largeAlloc = arena.Allocate( ( 4 * 1024 * 1024 ) + 64, 8 );
    ASSERT_TRUE( largeAlloc != nullptr );

    arena.Shutdown();
}

UTEST( Backend, VertexBindingSnapshotFlushShrinks )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    vhCmdBackendStateTest::Init();

    // Phase A: Bind 3 vertex streams, flush via vhSetState
    vhBuffer vb0 = vhAllocBuffer();
    vhBuffer vb1 = vhAllocBuffer();
    vhBuffer vb2 = vhAllocBuffer();

    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb0, bbuf );
    }
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb1, bbuf );
    }
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb2, bbuf );
    }

    vhStateId sid = 42;
    vhState state;
    state.SetVertexBuffer( vb0, 0, 0, 0, 256 );
    state.SetVertexBuffer( vb1, 1, 0, 0, 128 );
    state.SetVertexBuffer( vb2, 2, 0, 0, 64 );
    vhSetState( sid, state );
    vhFlush();

    // Backend should have exactly 3 bindings
    EXPECT_EQ( vhCmdBackendStateTest::GetVertexBindingsSize( sid ), 3u );

    // Phase B: Shrink to 1 binding via vhSetState with VRHI_DIRTY_VERTEX_INDEX
    state.vertexBindings.clear();
    state.vertexBindings.resize( 1 );
    state.vertexBindings[0] = { vb0, 0, 0, 0, 0, {}, false };
    state.dirty |= VRHI_DIRTY_VERTEX_INDEX;

    vhSetState( sid, state );
    vhFlush();

    // The bug: backend vertexBindings vector should shrink to 1, not retain 3
    EXPECT_EQ( vhCmdBackendStateTest::GetVertexBindingsSize( sid ), 1u );

    // Cleanup
    vhDestroyBuffer( vb0 );
    vhDestroyBuffer( vb1 );
    vhDestroyBuffer( vb2 );
    vhCmdBackendStateTest::Shutdown();
    vhFinish();
}

// vhEnumerateProfileScopes should hand back a non-empty list of non-null, non-empty names
// containing at least one well-known scope ("vhFlush"). Used by AE to pre-register names.
UTEST( Backend, ProfileScopeEnumeration )
{
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    struct Ctx
    {
        int count = 0;
        bool foundVhFlush = false;
        bool anyNull = false;
        bool anyEmpty = false;
    };
    Ctx ctx;

    vhEnumerateProfileScopes( []( const char* name, void* user )
    {
        Ctx& c = *( Ctx* ) user;
        c.count++;
        if ( !name ) { c.anyNull = true; return; }
        if ( name[0] == '\0' ) { c.anyEmpty = true; return; }
        if ( strcmp( name, "vhFlush" ) == 0 ) c.foundVhFlush = true;
    }, &ctx );

    EXPECT_GT( ctx.count, 0 );
    EXPECT_FALSE( ctx.anyNull );
    EXPECT_FALSE( ctx.anyEmpty );
    EXPECT_TRUE( ctx.foundVhFlush );

    // Null callback must not crash.
    vhEnumerateProfileScopes( nullptr, nullptr );
}

// B0: NVRHI command list handles are retained across vhFlush() so the upload chunk pool
// inside the command list survives instead of being torn down every flush.
UTEST( Backend, PersistentCommandListHandle )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    // Drain so the backend is idle and g_vhCmdLists is in a known state.
    vhFinish();

    nvrhi::ICommandList* before = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        before = g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Graphics].Get();
    }

    // writeBuffer pushes work through the Graphics command list, forcing creation.
    auto fnDriveGraphicsWork = []()
    {
        vhBuffer buf = vhAllocBuffer();
        vhCreateUniformBuffer( buf, "PersistTestBuf", nullptr, 256 );
        vhMem* data = vhAllocMem( 256 );
        vhUpdateUniformBuffer( buf, data, 0, 256 );
        vhFlush();
        vhDestroyBuffer( buf );
        vhFinish();
    };

    if ( !before )
    {
        fnDriveGraphicsWork();
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        before = g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Graphics].Get();
    }
    ASSERT_TRUE( before != nullptr );

    // Drive several flush cycles and confirm the handle pointer never changes.
    for ( int i = 0; i < 4; i++ )
    {
        fnDriveGraphicsWork();

        nvrhi::ICommandList* after = nullptr;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            after = g_vhCmdLists[( uint64_t ) nvrhi::CommandQueue::Graphics].Get();
        }
        EXPECT_EQ( after, before );
    }
}

// B1: gcFlushInterval debounces runGarbageCollection(). A very large interval should suppress
// GC across normal vhFlush() calls, but vhFinish() (waitForGPU) must always force a GC.
UTEST( Backend, GCFlushIntervalDebounce )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    // Default value sanity check.
    EXPECT_EQ( g_vhInit.gcFlushInterval, 64u );

    static std::atomic<int> gcCount{0};
    gcCount = 0;

    auto savedCB = g_vhInit.fnProfileCallback;
    g_vhInit.fnProfileCallback = []( const char* name, bool begin )
    {
        if ( begin && name && strcmp( name, "Handle_vhFlushInternal_GC" ) == 0 )
            gcCount.fetch_add( 1, std::memory_order_relaxed );
    };

    const uint32_t savedInterval = g_vhInit.gcFlushInterval;

    // Drain anything queued before the test so our counter measures only what we drive.
    vhFinish();
    gcCount = 0;

    // Huge interval — non-waiting flushes should not trigger GC.
    g_vhInit.gcFlushInterval = 100000;
    for ( int i = 0; i < 4; i++ ) vhFlush( true );
    EXPECT_EQ( gcCount.load(), 0 );

    // vhFinish() forces waitForGPU which must always GC.
    vhFinish();
    EXPECT_GE( gcCount.load(), 1 );

    // Tight interval — every flush should GC.
    gcCount = 0;
    g_vhInit.gcFlushInterval = 1;
    for ( int i = 0; i < 3; i++ ) vhFlush( true );
    EXPECT_GE( gcCount.load(), 3 );

    g_vhInit.gcFlushInterval = savedInterval;
    g_vhInit.fnProfileCallback = savedCB;
    vhFinish();
}

UTEST( Backend, BindingSetCacheRetainedOnGC )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    nvrhi::BufferHandle buffer;
    {
        nvrhi::BufferDesc bufDesc;
        bufDesc.byteSize = 16 * 1024;
        bufDesc.isConstantBuffer = true;
        bufDesc.debugName = "TestBSetEvictCB";
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        buffer = g_vhDevice->createBuffer( bufDesc );
    }
    ASSERT_NE( buffer, nullptr );

    nvrhi::BindingLayoutHandle layout;
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::All;
        layoutDesc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 0 ) );
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }
    ASSERT_NE( layout, nullptr );

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, buffer.Get(), nvrhi::BufferRange( 0, 256 ) ) );
    nvrhi::BindingSetHandle before = vhGetBindingSet( desc, layout.Get() );
    ASSERT_NE( before, nullptr );
    EXPECT_EQ( vhGetBindingSet( desc, layout.Get() ).Get(), before.Get() );

    nvrhi::BindingSetDesc churn = desc;
    churn.bindings[0].range = nvrhi::BufferRange( 256, 256 );
    EXPECT_NE( vhGetBindingSet( churn, layout.Get() ).Get(), before.Get() );

    const uint32_t savedInterval = g_vhInit.gcFlushInterval;
    g_vhInit.gcFlushInterval = 1;
    vhFlush( true );
    EXPECT_EQ( vhGetBindingSet( desc, layout.Get() ).Get(), before.Get() );

    vhFinish();
    nvrhi::BindingSetHandle after = vhGetBindingSet( desc, layout.Get() );
    EXPECT_NE( after.Get(), before.Get() );

    g_vhInit.gcFlushInterval = savedInterval;
    vhBindingSetCacheClear();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        before = nullptr;
        after = nullptr;
        layout = nullptr;
        buffer = nullptr;
    }
    vhFinish();
}

UTEST( Backend, BindingSetCacheInvalidatedOnDestroy )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    vhBuffer id = vhAllocBuffer();
    vhCreateUniformBuffer( id, "BindingSetCacheInvalidatedOnDestroy", nullptr, 256 );
    vhFlush( true );
    nvrhi::BufferHandle buffer = vhGetBufferNvrhiHandle( id );
    ASSERT_NE( buffer, nullptr );

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 0 ) );
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }
    ASSERT_NE( layout, nullptr );

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, buffer.Get(), nvrhi::BufferRange( 0, 256 ) ) );
    nvrhi::BindingSetHandle before = vhGetBindingSet( desc, layout.Get() );
    vhDestroyBuffer( id );
    vhFlush( true );
    EXPECT_NE( vhGetBindingSet( desc, layout.Get() ).Get(), before.Get() );
    vhBindingSetCacheClear();
    before = nullptr;
    buffer = nullptr;
    layout = nullptr;
    vhFinish();
}

UTEST( Backend, BindingSetCacheBounded )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    nvrhi::BufferDesc bufDesc;
    bufDesc.byteSize = 4097 * 256;
    bufDesc.isConstantBuffer = true;
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 0 ) );
    nvrhi::BufferHandle buffer;
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        buffer = g_vhDevice->createBuffer( bufDesc );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }
    ASSERT_NE( buffer, nullptr );
    ASSERT_NE( layout, nullptr );

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, buffer.Get(), nvrhi::BufferRange( 0, 256 ) ) );
    nvrhi::BindingSetHandle first = vhGetBindingSet( desc, layout.Get() );
    for ( uint32_t i = 1; i < 4096; i++ )
    {
        desc.bindings[0].range = nvrhi::BufferRange( i * 256, 256 );
        ASSERT_NE( vhGetBindingSet( desc, layout.Get() ), nullptr );
    }
    desc.bindings[0].range = nvrhi::BufferRange( 0, 256 );
    EXPECT_EQ( vhGetBindingSet( desc, layout.Get() ).Get(), first.Get() );
    desc.bindings[0].range = nvrhi::BufferRange( 4096 * 256, 256 );
    ASSERT_NE( vhGetBindingSet( desc, layout.Get() ), nullptr );
    desc.bindings[0].range = nvrhi::BufferRange( 0, 256 );
    EXPECT_NE( vhGetBindingSet( desc, layout.Get() ).Get(), first.Get() );
    vhFinish();
}

UTEST( Backend, BindingSetCacheEvictionBeforeSubmit )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
    const int32_t errorsBefore = g_vhErrorCounter.load();

    const char* source = R"(
        cbuffer Params : register(b0)
        {
            uint index;
            uint value;
        };
        RWStructuredBuffer<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main()
        {
            output[index] = value;
        }
    )";
    std::vector< uint32_t > spirv;
    std::string error;
    bool compiled = vhCompileShader( "BindingSetCacheEviction", source, VRHI_SHADER_STAGE_COMPUTE | VRHI_SHADER_SM_6_0, spirv, "main", {}, {}, &error );
    if ( !compiled ) { UTEST_PRINTF( "Shader Compile Error: %s\n", error.c_str() ); }
    ASSERT_TRUE( compiled );
    vhShader cs = vhAllocShader();
    vhCreateShader( cs, "BindingSetCacheEviction", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
    vhFinish();

    std::vector< vhShaderReflectionResource > resources;
    vhGetShaderInfo( cs, nullptr, &resources );
    uint32_t cbSlot = UINT32_MAX;
    uint32_t uavSlot = UINT32_MAX;
    for ( const auto& resource : resources )
    {
        ASSERT_EQ( resource.set, 0u );
        if ( resource.type == nvrhi::ResourceType::ConstantBuffer ) { cbSlot = resource.slot; }
        if ( resource.type == nvrhi::ResourceType::StructuredBuffer_UAV ) { uavSlot = resource.slot; }
    }
    ASSERT_NE( cbSlot, UINT32_MAX );
    ASSERT_NE( uavSlot, UINT32_MAX );

    const uint32_t count = 8193;
    std::vector< uint32_t > params( count * 64, 0 );
    std::vector< uint32_t > zeros( count, 0 );
    for ( uint32_t i = 0; i < count; i++ )
    {
        params[i * 64] = i;
        params[i * 64 + 1] = 0x12340000u + i;
    }

    nvrhi::BufferDesc cbDesc;
    cbDesc.byteSize = params.size() * sizeof( uint32_t );
    cbDesc.isConstantBuffer = true;
    cbDesc.enableAutomaticStateTracking( nvrhi::ResourceStates::ConstantBuffer );
    nvrhi::BufferDesc outDesc;
    outDesc.byteSize = count * sizeof( uint32_t );
    outDesc.structStride = sizeof( uint32_t );
    outDesc.canHaveUAVs = true;
    outDesc.enableAutomaticStateTracking( nvrhi::ResourceStates::UnorderedAccess );
    nvrhi::BufferDesc stagingDesc;
    stagingDesc.byteSize = outDesc.byteSize;
    stagingDesc.cpuAccess = nvrhi::CpuAccessMode::Read;

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindingOffsets = { 0, 0, 0, 0 };
    layoutDesc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( cbSlot ) );
    layoutDesc.addItem( nvrhi::BindingLayoutItem::StructuredBuffer_UAV( uavSlot ) );
    nvrhi::BufferHandle cb, output, staging;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::ComputePipelineHandle pipeline;
    nvrhi::ShaderHandle shader = vhGetShaderNvrhiHandle( cs );
    ASSERT_NE( shader, nullptr );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cb = g_vhDevice->createBuffer( cbDesc );
        output = g_vhDevice->createBuffer( outDesc );
        staging = g_vhDevice->createBuffer( stagingDesc );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = shader;
        pipelineDesc.bindingLayouts.push_back( layout );
        pipeline = g_vhDevice->createComputePipeline( pipelineDesc );
    }
    ASSERT_NE( cb, nullptr );
    ASSERT_NE( output, nullptr );
    ASSERT_NE( staging, nullptr );
    ASSERT_NE( layout, nullptr );
    ASSERT_NE( pipeline, nullptr );

    nvrhi::CommandListHandle cmdList;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        cmdList->writeBuffer( cb, params.data(), cbDesc.byteSize );
        cmdList->writeBuffer( output, zeros.data(), outDesc.byteSize );
    }
    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( cbSlot, cb, nvrhi::BufferRange( 0, 256 ) ) );
    desc.addItem( nvrhi::BindingSetItem::StructuredBuffer_UAV( uavSlot, output ) );
    nvrhi::BindingSetHandle first = vhGetBindingSet( desc, layout );
    ASSERT_NE( first, nullptr );
    for ( uint32_t i = 0; i < count; i++ )
    {
        desc.bindings[0].range = nvrhi::BufferRange( i * 256, 256 );
        nvrhi::BindingSetHandle bset = vhGetBindingSet( desc, layout );
        ASSERT_NE( bset, nullptr );
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        nvrhi::ComputeState state;
        state.pipeline = pipeline;
        state.bindings.push_back( bset );
        cmdList->setComputeState( state );
        cmdList->dispatch( 1, 1, 1 );
    }
    desc.bindings[0].range = nvrhi::BufferRange( 0, 256 );
    EXPECT_NE( vhGetBindingSet( desc, layout ).Get(), first.Get() );

    // Record -> evict twice -> release caller refs -> submit -> read every result.
    vhBindingSetCacheClear();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdList->copyBuffer( staging, 0, output, 0, outDesc.byteSize );
        first = nullptr;
        cb = nullptr;
        output = nullptr;
        layout = nullptr;
        pipeline = nullptr;
        shader = nullptr;
        cmdList = nullptr;
    }
    vhDestroyShader( cs );
    vhFlush( true );
    vhFinish();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        const uint32_t* data = ( const uint32_t* ) g_vhDevice->mapBuffer( staging, nvrhi::CpuAccessMode::Read );
        ASSERT_NE( data, nullptr );
        for ( uint32_t i = 0; i < count; i++ )
        {
            EXPECT_EQ( data[i], 0x12340000u + i );
        }
        g_vhDevice->unmapBuffer( staging );
        staging = nullptr;
    }
    EXPECT_EQ( g_vhErrorCounter.load(), errorsBefore );
}

UTEST( Backend, BindingSetCacheOwnsResourcesOnShutdown )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    TestEnsureShutdown();
    for ( int i = 0; i < 4; i++ )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
        vhFinish();
        const int32_t errorsBefore = g_vhErrorCounter.load();
        g_vhInit.gcFlushInterval = 1;
        {
            nvrhi::BufferDesc bufDesc;
            bufDesc.byteSize = 256;
            bufDesc.isConstantBuffer = true;
            nvrhi::TextureDesc texDesc;
            texDesc.width = 4;
            texDesc.height = 4;
            texDesc.format = nvrhi::Format::RGBA8_UNORM;
            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::All;
            layoutDesc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 0 ) );
            layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
            layoutDesc.addItem( nvrhi::BindingLayoutItem::Sampler( 0 ) );
            nvrhi::BufferHandle buffer;
            nvrhi::TextureHandle texture;
            nvrhi::SamplerHandle sampler;
            nvrhi::BindingLayoutHandle layout;
            {
                std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
                buffer = g_vhDevice->createBuffer( bufDesc );
                texture = g_vhDevice->createTexture( texDesc );
                sampler = g_vhDevice->createSampler( nvrhi::SamplerDesc() );
                layout = g_vhDevice->createBindingLayout( layoutDesc );
            }
            ASSERT_NE( buffer, nullptr );
            ASSERT_NE( texture, nullptr );
            ASSERT_NE( sampler, nullptr );
            ASSERT_NE( layout, nullptr );
            nvrhi::BindingSetDesc desc;
            desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, buffer ) );
            desc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, texture ) );
            desc.addItem( nvrhi::BindingSetItem::Sampler( 0, sampler ) );
            nvrhi::BindingSetHandle bset = vhGetBindingSet( desc, layout );
            ASSERT_NE( bset, nullptr );
            nvrhi::IBindingLayout* layoutPtr = layout.Get();
            nvrhi::IBindingSet* bsetPtr = bset.Get();
            buffer = nullptr;
            texture = nullptr;
            sampler = nullptr;
            layout = nullptr;
            bset = nullptr;
            vhFlush( true );
            EXPECT_EQ( vhGetBindingSet( desc, layoutPtr ).Get(), bsetPtr );
            EXPECT_EQ( vhGetBindingSet( desc, layoutPtr )->getLayout(), layoutPtr );
        }
        // Cache is the only owner; no vhFinish() before device teardown.
        TestEnsureShutdown();
        EXPECT_EQ( g_vhErrorCounter.load(), errorsBefore );
        EXPECT_EQ( g_vhDevice, nullptr );
    }
}

UTEST( Backend, ProfileMarkerNesting )
{
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }

    // fnProfileCallback fires from the backend RHI thread; use atomics.
    std::atomic<int> opens{0}, closes{0};
    auto savedCB = g_vhInit.fnProfileCallback;
    g_vhInit.fnProfileCallback = [&]( const char* name, bool begin )
    {
        if ( begin ) opens.fetch_add( 1, std::memory_order_relaxed );
        else         closes.fetch_add( 1, std::memory_order_relaxed );
    };

    vhBeginMarker( "Outer" );
    vhBeginMarker( "Middle" );
    vhBeginMarker( "Inner" );
    vhEndMarker();
    vhEndMarker();
    vhEndMarker();
    vhFinish();

    g_vhInit.fnProfileCallback = savedCB;

    EXPECT_EQ( opens.load(), closes.load() );
    EXPECT_GT( opens.load(), 0 );
}

UTEST( Backend, BlockingBackendMode_Equivalence )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }

    TestEnsureShutdown();
    g_vhInit = vhInitData{};
    g_vhInit.debugBlockWaitForBackend = true;
    vhInit( g_testInitQuiet );
    g_testInit = true;

    // Just verify init/shutdown works in blocking mode
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, "BlockTex", glm::ivec2(4,4), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE );
    vhFlush();
    vhDestroyTexture( tex );
    vhFinish();

    // No errors expected
    EXPECT_EQ( g_vhErrorCounter.load(), 0 );

    vhShutdown( g_testInitQuiet );
    g_testInit = false;
}

UTEST( Backend, VertexBindingSnapshotFlushEmpty )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    vhCmdBackendStateTest::Init();

    // Bind 2 vertex streams
    vhBuffer vb0 = vhAllocBuffer();
    vhBuffer vb1 = vhAllocBuffer();

    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb0, bbuf );
    }
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb1, bbuf );
    }

    vhStateId sid = 43;
    vhState state;
    state.SetVertexBuffer( vb0, 0, 0, 0, 256 );
    state.SetVertexBuffer( vb1, 1, 0, 0, 128 );
    vhSetState( sid, state );
    vhFlush();

    EXPECT_EQ( vhCmdBackendStateTest::GetVertexBindingsSize( sid ), 2u );

    // Clear all vertex bindings
    state.vertexBindings.clear();
    state.dirty |= VRHI_DIRTY_VERTEX_INDEX;
    vhSetState( sid, state );
    vhFlush();

    EXPECT_EQ( vhCmdBackendStateTest::GetVertexBindingsSize( sid ), 0u );

    vhDestroyBuffer( vb0 );
    vhDestroyBuffer( vb1 );
    vhCmdBackendStateTest::Shutdown();
    vhFinish();
}

UTEST( Backend, VertexBindingSnapshotFlushGrows )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    vhCmdBackendStateTest::Init();

    // Start with 1 binding
    vhBuffer vb0 = vhAllocBuffer();
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb0, bbuf );
    }

    vhStateId sid = 44;
    vhState state;
    state.SetVertexBuffer( vb0, 0, 0, 0, 256 );
    vhSetState( sid, state );
    vhFlush();

    EXPECT_EQ( vhCmdBackendStateTest::GetVertexBindingsSize( sid ), 1u );

    // Grow to 3 bindings
    vhBuffer vb1 = vhAllocBuffer();
    vhBuffer vb2 = vhAllocBuffer();
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb1, bbuf );
    }
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsVertexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->stride = 12;
        vhCmdBackendStateTest::InsertDummyBuffer( vb2, bbuf );
    }

    state.vertexBindings.clear();
    state.vertexBindings.resize( 3 );
    state.vertexBindings[0] = { vb0, 0, 0, 0, 0, {}, false };
    state.vertexBindings[1] = { vb1, 1, 0, 0, 0, {}, false };
    state.vertexBindings[2] = { vb2, 2, 0, 0, 0, {}, false };
    state.dirty |= VRHI_DIRTY_VERTEX_INDEX;

    vhSetState( sid, state );
    vhFlush();

    EXPECT_EQ( vhCmdBackendStateTest::GetVertexBindingsSize( sid ), 3u );

    vhDestroyBuffer( vb0 );
    vhDestroyBuffer( vb1 );
    vhDestroyBuffer( vb2 );
    vhCmdBackendStateTest::Shutdown();
    vhFinish();
}

UTEST( Backend, M01_ProgramEqualVectors_NoVersionBump )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5001;
    for ( const vhProgram& program : { vhProgram{}, vhProgram{ 1001 }, vhProgram{ 1001, 1002 }, vhProgram{ 1002, 1001 }, vhProgram{} } )
    {
        vhState before, stored;
        vhBackendQueryState( sid, before );
        uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        uint64_t resource = vhCmdBackendStateTest::GetGlobalResourceVersion();
        vhCmdSetStateProgram( sid, program );
        vhFlush();
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        EXPECT_TRUE( stored.program == program );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + ( before.program != program ) );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
        const uint64_t dirty = vhCmdBackendStateTest::GetDirtyBits( sid );
        EXPECT_EQ( ( dirty & VRHI_DIRTY_PROGRAM ) != 0, before.program != program );
        pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        resource = vhCmdBackendStateTest::GetGlobalResourceVersion();
        vhProgram duplicate = program;
        vhCmdSetStateProgram( sid, duplicate );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
    }
}

UTEST( Backend, M02_StateFlagsAllBits_NoBumpOnEqual )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5002;
    for ( uint32_t bit = 0; bit < 64; bit++ )
    {
        vhCmdSetStateFlags( sid, 0 );
        vhFlush();
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        const uint64_t flags = 1ULL << bit;
        vhCmdSetStateFlags( sid, flags );
        vhFlush();
        vhState stored;
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        EXPECT_EQ( stored.stateFlags, flags );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        vhCmdSetStateFlags( sid, flags );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
    }
}

UTEST( Backend, M03_StencilFullPacked_EqualNoBump )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5003;
    for ( uint32_t bit = 0; bit < 64; bit++ )
    {
        vhCmdSetStateStencil( sid, 0 );
        vhFlush();
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        const uint64_t stencil = 1ULL << bit;
        vhCmdSetStateStencil( sid, stencil );
        vhFlush();
        vhState stored;
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        EXPECT_EQ( stored.stencilState, stencil );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        vhCmdSetStateStencil( sid, stencil );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
    }
}

UTEST( Backend, M04_VertexBindingEachMember_BumpsOnChange )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5004;
    const vhState::VertexBinding original = { 101, 0, 2, 3, 16, "float3", false };
    for ( int member = 0; member < 7; member++ )
    {
        vhCmdSetStateVertexBindings( sid, { original } );
        vhFlush();
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        auto changed = original;
        switch ( member )
        {
            case 0: changed.buffer = 102; break;
            case 1: changed.stream = 1; break;
            case 2: changed.startVertex = 4; break;
            case 3: changed.numVertices = 6; break;
            case 4: changed.byteOffset = 32; break;
            case 5: changed.layoutOverride = "float4"; break;
            case 6: changed.isInstanced = true; break;
        }
        UTEST_PRINTF( "Vertex member %d\n", member );
        vhCmdSetStateVertexBindings( sid, { changed } );
        vhFlush();
        vhState stored;
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        ASSERT_EQ( stored.vertexBindings.size(), 1u );
        const auto& binding = stored.vertexBindings[0];
        EXPECT_EQ( binding.buffer, changed.buffer );
        EXPECT_EQ( binding.stream, changed.stream );
        EXPECT_EQ( binding.startVertex, changed.startVertex );
        EXPECT_EQ( binding.numVertices, changed.numVertices );
        EXPECT_EQ( binding.byteOffset, changed.byteOffset );
        EXPECT_STREQ( binding.layoutOverride.c_str(), changed.layoutOverride.c_str() );
        EXPECT_EQ( binding.isInstanced, changed.isInstanced );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        vhCmdSetStateVertexBindings( sid, { changed } );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
    }
    vhCmdSetStateVertexBindings( sid, {} );
    vhFlush();
}

UTEST( Backend, M05_VertexListShape_EmptySingleManyReorder )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5005;
    const vhState::VertexBinding a = { 101, 0, 2, 3, 16, "float3", false };
    const vhState::VertexBinding b = { 102, 1, 4, 6, 32, "float4", true };
    const std::vector< std::vector< vhState::VertexBinding > > lists = { {}, { a }, { a, b }, { b, a }, { b }, { b, a, b }, {} };
    for ( const auto& list : lists )
    {
        vhState before, stored;
        vhBackendQueryState( sid, before );
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        vhCmdSetStateVertexBindings( sid, list );
        vhFlush();
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        ASSERT_EQ( stored.vertexBindings.size(), list.size() );
        for ( size_t i = 0; i < list.size(); i++ )
        {
            const auto& binding = stored.vertexBindings[i];
            EXPECT_EQ( binding.buffer, list[i].buffer );
            EXPECT_EQ( binding.stream, list[i].stream );
            EXPECT_EQ( binding.startVertex, list[i].startVertex );
            EXPECT_EQ( binding.numVertices, list[i].numVertices );
            EXPECT_EQ( binding.byteOffset, list[i].byteOffset );
            EXPECT_STREQ( binding.layoutOverride.c_str(), list[i].layoutOverride.c_str() );
            EXPECT_EQ( binding.isInstanced, list[i].isInstanced );
        }
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + ( !before.vertexBindings.empty() || !list.empty() ) );
        const uint64_t changed = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        vhCmdSetStateVertexBindings( sid, list );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), changed );
    }
}

UTEST( Backend, M06_IndexBindingEachMember_BumpsOnChange )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5006;
    const vhState::IndexBinding original = { 101, 2, 3, 16 };
    for ( int member = 0; member < 4; member++ )
    {
        vhCmdSetStateIndexBuffer( sid, original.buffer, original.byteOffset, original.firstIndex, original.numIndices );
        vhFlush();
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        auto changed = original;
        switch ( member )
        {
            case 0: changed.buffer = 102; break;
            case 1: changed.firstIndex = 4; break;
            case 2: changed.numIndices = 6; break;
            case 3: changed.byteOffset = 32; break;
        }
        UTEST_PRINTF( "Index member %d\n", member );
        vhCmdSetStateIndexBuffer( sid, changed.buffer, changed.byteOffset, changed.firstIndex, changed.numIndices );
        vhFlush();
        vhState stored;
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        EXPECT_EQ( stored.indexBinding.buffer, changed.buffer );
        EXPECT_EQ( stored.indexBinding.firstIndex, changed.firstIndex );
        EXPECT_EQ( stored.indexBinding.numIndices, changed.numIndices );
        EXPECT_EQ( stored.indexBinding.byteOffset, changed.byteOffset );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        vhCmdSetStateIndexBuffer( sid, changed.buffer, changed.byteOffset, changed.firstIndex, changed.numIndices );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
    }
    vhCmdSetStateIndexBuffer( sid, VRHI_INVALID_HANDLE, 0, 0, UINT32_MAX );
    vhFlush();
}

UTEST( Backend, M07_BlendConstants_PerChannel_BumpsOnChange )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5007;
    const glm::vec4 original( 0.1f, 0.2f, 0.3f, 0.4f );
    for ( int channel = 0; channel < 4; channel++ )
    {
        vhCmdSetStateFlags( sid, 0 );
        vhCmdSetStateBlendConstants( sid, original );
        vhFlush();
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        auto changed = original;
        changed[channel] = 0.75f;
        vhCmdSetStateBlendConstants( sid, changed );
        vhFlush();
        vhState stored;
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        EXPECT_TRUE( stored.blendConstantColor == changed );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        vhCmdSetStateBlendConstants( sid, changed );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
    }
    vhCmdSetStateBlendConstants( sid, glm::vec4( 0.0f ) );
    vhFlush();
    const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
    vhCmdSetStateBlendConstants( sid, glm::vec4( -0.0f, 0.0f, 0.0f, 0.0f ) );
    vhFlush();
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
}

UTEST( Backend, M08_DescriptorTableList_BumpsOnAnyChange )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5008;
    const std::vector< vhState::DescriptorTableBinding > original = { { 101, 1 }, { 102, 2 } };
    for ( int member = 0; member < 5; member++ )
    {
        vhCmdSetStateDescriptorTables( sid, original );
        vhFlush();
        const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
        const uint64_t resource = vhCmdBackendStateTest::GetGlobalResourceVersion();
        auto changed = original;
        switch ( member )
        {
            case 0: changed[0].table = 103; break;
            case 1: changed[1].registerSpace = 3; break;
            case 2: changed.resize( 1 ); break;
            case 3: std::swap( changed[0], changed[1] ); break;
            case 4: changed.clear(); break;
        }
        vhCmdSetStateDescriptorTables( sid, changed );
        vhFlush();
        vhState stored;
        ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
        ASSERT_EQ( stored.descriptorTables.size(), changed.size() );
        for ( size_t i = 0; i < changed.size(); i++ )
        {
            EXPECT_EQ( stored.descriptorTables[i].table, changed[i].table );
            EXPECT_EQ( stored.descriptorTables[i].registerSpace, changed[i].registerSpace );
        }
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
        EXPECT_NE( vhCmdBackendStateTest::GetDirtyBits( sid ) & VRHI_DIRTY_DESCRIPTOR_TABLES, 0u );
        vhCmdSetStateDescriptorTables( sid, changed );
        vhFlush();
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
        EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
    }
}

UTEST( Backend, M09_DefaultCommandsCreateBackendState )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5009;
    vhState stored;
    EXPECT_FALSE( vhBackendQueryState( sid, stored ) );
    const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
    const uint64_t resource = vhCmdBackendStateTest::GetGlobalResourceVersion();
    vhCmdSetStateProgram( sid, {} );
    vhCmdSetStateFlags( sid, 0 );
    vhCmdSetStateStencil( sid, 0 );
    vhCmdSetStateVertexBindings( sid, {} );
    vhCmdSetStateDescriptorTables( sid, {} );
    vhFlush();
    ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
    EXPECT_TRUE( stored.program.empty() );
    EXPECT_TRUE( stored.vertexBindings.empty() );
    EXPECT_TRUE( stored.descriptorTables.empty() );
    EXPECT_EQ( stored.stateFlags, 0u );
    EXPECT_EQ( stored.stencilState, 0u );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
    vhCmdSetStateProgram( sid, { 101 } );
    vhFlush();
    ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
    ASSERT_EQ( stored.program.size(), 1u );
    EXPECT_EQ( stored.program[0], 101u );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
    EXPECT_NE( vhCmdBackendStateTest::GetDirtyBits( sid ) & VRHI_DIRTY_PROGRAM, 0u );
}

UTEST( Backend, M10_InvalidCreationThenValidRecovery )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    EXPECT_EQ( vhCreateVertexBuffer( VRHI_INVALID_HANDLE, "M10InvalidHandle", nullptr, "float3", 3 ), VRHI_INVALID_HANDLE );
    vhBuffer vb = vhAllocBuffer();
    vhFlush();
    const int32_t errors = g_vhErrorCounter.load();
    const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
    const uint64_t resource = vhCmdBackendStateTest::GetGlobalResourceVersion();
    vhCreateVertexBuffer( vb, "M10InvalidLayout", nullptr, "invalid_layout", 3 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), errors + 1 );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
    EXPECT_EQ( vhGetBufferNvrhiHandle( vb ), nullptr );
    vhCreateVertexBuffer( vb, "M10Valid", nullptr, "float3", 3 );
    vhFlush();
    EXPECT_NE( vhGetBufferNvrhiHandle( vb ), nullptr );
    EXPECT_EQ( vhGetBufferInfo( vb ), 36u );
    EXPECT_EQ( g_vhErrorCounter.load(), errors + 1 );
    vhDestroyBuffer( vb );
    vhFinish();
}

UTEST( Backend, P01_IdenticalSetters_KeepPipelineResourceVersion )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5101;
    vhState state;
    state.SetProgram( { 101, 102 } );
    state.SetStateFlags( VRHI_STATE_WRITE_MASK );
    state.SetStencil( VRHI_STENCIL_FUNC_REF( 1 ) );
    state.SetVertexBuffer( 101, 0, 16, 2, 3, "float3" );
    state.SetIndexBuffer( 102, 32, 4, 6 );
    state.SetBlendConstColor( glm::vec4( 0.25f, 0.5f, 0.75f, 1.0f ) );
    state.SetDescriptorTable( 0, 103, 1 );
    vhSetState( sid, state );
    vhFlush();
    const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
    const uint64_t resource = vhCmdBackendStateTest::GetGlobalResourceVersion();
    vhState duplicate = state;
    duplicate.dirty = VRHI_DIRTY_PROGRAM | VRHI_DIRTY_PIPELINE | VRHI_DIRTY_VERTEX_INDEX | VRHI_DIRTY_DESCRIPTOR_TABLES;
    vhSetState( sid, duplicate );
    vhFlush();
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalResourceVersion(), resource );
}

UTEST( Backend, P06_VertexOverrideEqualContentsSeparateStorage )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5201;
    std::vector< vhState::VertexBinding > first = { { 101, 0, 2, 3, 16, "float3", false } };
    auto duplicate = first;
    ASSERT_NE( first[0].layoutOverride.data(), duplicate[0].layoutOverride.data() );
    vhCmdSetStateVertexBindings( sid, first );
    vhFlush();
    const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
    vhCmdSetStateVertexBindings( sid, duplicate );
    vhFlush();
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
    duplicate[0].layoutOverride = "float4";
    vhCmdSetStateVertexBindings( sid, duplicate );
    vhFlush();
    vhState stored;
    ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
    ASSERT_EQ( stored.vertexBindings.size(), 1u );
    EXPECT_STREQ( stored.vertexBindings[0].layoutOverride.c_str(), "float4" );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
}

UTEST( Backend, P10_VertexBindingEarlyReturn_CommandInputOwnership )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFlush();
    const vhStateId sid = 5301;
    vhCmdSetStateVertexBindings( sid, { { 101, 0, 2, 3, 16, "float3", false } } );
    vhFlush();
    const uint64_t pipeline = vhCmdBackendStateTest::GetGlobalPipelineVersion();
    for ( int i = 0; i < 4; i++ )
    {
        std::vector< vhState::VertexBinding > duplicate = { { 101, 0, 2, 3, 16, "float3", false } };
        vhCmdSetStateVertexBindings( sid, duplicate );
        duplicate[0] = { 999, 1, 4, 6, 32, "float4", true };
    }
    vhFlush();
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline );
    {
        std::vector< vhState::VertexBinding > changed = { { 102, 1, 4, 6, 32, "float4", true } };
        vhCmdSetStateVertexBindings( sid, changed );
        changed[0] = {};
    }
    vhFlush();
    vhState stored;
    ASSERT_TRUE( vhBackendQueryState( sid, stored ) );
    ASSERT_EQ( stored.vertexBindings.size(), 1u );
    const auto& binding = stored.vertexBindings[0];
    EXPECT_EQ( binding.buffer, 102u );
    EXPECT_EQ( binding.stream, 1u );
    EXPECT_EQ( binding.startVertex, 4u );
    EXPECT_EQ( binding.numVertices, 6u );
    EXPECT_EQ( binding.byteOffset, 32u );
    EXPECT_STREQ( binding.layoutOverride.c_str(), "float4" );
    EXPECT_TRUE( binding.isInstanced );
    EXPECT_EQ( vhCmdBackendStateTest::GetGlobalPipelineVersion(), pipeline + 1 );
}

UTEST( Backend, PipelineHashesIncludeBindlessLayoutIdentity )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Native layout identity requires GPU" );
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhFinish();
    nvrhi::BindlessLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.maxCapacity = 4;
    layoutDesc.registerSpaces.push_back( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    nvrhi::BindingLayoutHandle a, b;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        a = g_vhDevice->createBindlessLayout( layoutDesc );
        b = g_vhDevice->createBindlessLayout( layoutDesc );
    }
    ASSERT_NE( a, nullptr );
    ASSERT_NE( b, nullptr );
    ASSERT_NE( a, b );
    ASSERT_EQ( a->getDesc(), nullptr );
    ASSERT_EQ( b->getDesc(), nullptr );
    nvrhi::ComputePipelineDesc compute;
    compute.bindingLayouts.push_back( a );
    uint64_t computeA = vhHashComputePipeline( compute );
    EXPECT_EQ( vhHashComputePipeline( compute ), computeA );
    compute.bindingLayouts[0] = b;
    EXPECT_NE( vhHashComputePipeline( compute ), computeA );
    nvrhi::GraphicsPipelineDesc graphics;
    nvrhi::FramebufferInfo framebuffer;
    graphics.bindingLayouts.push_back( a );
    uint64_t graphicsA = vhHashGraphicsPipeline( graphics, framebuffer );
    EXPECT_EQ( vhHashGraphicsPipeline( graphics, framebuffer ), graphicsA );
    graphics.bindingLayouts[0] = b;
    EXPECT_NE( vhHashGraphicsPipeline( graphics, framebuffer ), graphicsA );
}

// T02: a held reader retains every byte from the first ring traversal.

UTEST( Backend, Transient_T02_BlockedReaderStopsSlotReuse )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Native GPU reader requires Vulkan" );
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
    const int32_t errors = g_vhErrorCounter.load();
    struct Buffer : vhTransientBuffer
    {
        ~Buffer()
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            Shutdown_DeviceStateLocked();
        }
    } buffer;
    nvrhi::BufferDesc desc;
    desc.setByteSize( sizeof( vhWorldUniform ) * 4 ).setIsConstantBuffer( true ).setCpuAccess( nvrhi::CpuAccessMode::Write );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        buffer.Init_DeviceStateLocked( desc );
    }
    nvrhi::BufferDesc readDesc;
    readDesc.setByteSize( sizeof( vhWorldUniform ) * 4 ).setCpuAccess( nvrhi::CpuAccessMode::Read );
    auto staging = g_vhDevice->createBuffer( readDesc );
    ASSERT_NE( staging, nullptr );
    vhWorldUniform markers[4];
    for ( size_t i = 0; i < 4; ++i ) std::memset( &markers[i], ( int ) ( 0x30 + i ), sizeof( markers[i] ) );
    std::promise< void > entered;
    auto attempt = entered.get_future();
    int64_t writeOffset = -1;
    {
        vrhi_test::VhGpuGate gate( utest_result );
        if ( !gate.Supported() ) UTEST_SKIP( "Timeline semaphore unsupported" );
        ASSERT_EQ( buffer.Write( &markers[0], sizeof( markers[0] ) ), 0 );
        gate.WaitBefore();
        for ( size_t i = 0; i < 3; ++i )
        {
            if ( i ) ASSERT_EQ( buffer.Write( &markers[i], sizeof( markers[i] ) ), 0 );
            {
                std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
                buffer.Flush_DeviceStateLocked();
                auto commands = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
                commands->copyBuffer( staging, i * sizeof( vhWorldUniform ), buffer.handle[buffer.frameIdx], 0, sizeof( vhWorldUniform ) );
                vhCmdListFlush_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
                g_vhDevice->setEventQuery( buffer.fence[buffer.frameIdx][0], nvrhi::CommandQueue::Graphics );
                buffer.Step();
            }
        }
        gate.CaptureReader();
        ASSERT_TRUE( gate.Pending() );
        ASSERT_EQ( buffer.frameIdx, 0u );
        gate.StartProducer( [&]
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            entered.set_value();
            writeOffset = buffer.Alloc( sizeof( markers[3] ) );
            uint8_t* mapped = buffer.Map_DeviceStateLocked();
            if ( mapped && writeOffset >= 0 ) std::memcpy( mapped + writeOffset, &markers[3], sizeof( markers[3] ) );
        } );
        ASSERT_EQ( attempt.wait_for( std::chrono::seconds( 2 ) ), std::future_status::ready );
        EXPECT_FALSE( gate.ProducerFinished( std::chrono::milliseconds( 100 ) ) );
        EXPECT_TRUE( gate.Pending() );
        EXPECT_FALSE( gate.TimedOut() );
        gate.Release();
        gate.JoinProducer();
        ASSERT_EQ( writeOffset, 0 );
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            buffer.Flush_DeviceStateLocked();
            auto commands = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
            commands->copyBuffer( staging, 3 * sizeof( vhWorldUniform ), buffer.handle[0], 0, sizeof( vhWorldUniform ) );
            vhCmdListFlush_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
            g_vhDevice->setEventQuery( buffer.fence[0][0], nvrhi::CommandQueue::Graphics );
            buffer.Step();
        }
    }
    const void* data = g_vhDevice->mapBuffer( staging, nvrhi::CpuAccessMode::Read );
    ASSERT_NE( data, nullptr );
    EXPECT_EQ( std::memcmp( data, markers, sizeof( markers ) ), 0 );
    g_vhDevice->unmapBuffer( staging );
    vhFinish();
    EXPECT_EQ( g_vhErrorCounter.load(), errors );
}

// A clear-only submission must retain its texture after the public handle is destroyed.
UTEST( Backend, ClearOnlyRetainsTextureUntilGpuCompletion )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Clear lifetime requires Vulkan" );
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
    const int32_t errors = g_vhErrorCounter.load();
    struct ClearCase
    {
        nvrhi::Format format;
        uint16_t flags;
    };
    const ClearCase cases[] = {
        { nvrhi::Format::RGBA8_UNORM, VRHI_CLEAR_COLOR },
        { nvrhi::Format::R32_UINT, VRHI_CLEAR_COLOR | VRHI_CLEAR_UINT },
        { nvrhi::Format::D32, VRHI_CLEAR_DEPTH },
        { nvrhi::Format::D32S8, VRHI_CLEAR_STENCIL },
        { nvrhi::Format::D32S8, VRHI_CLEAR_DEPTH | VRHI_CLEAR_STENCIL }
    };
    const vhStateId sid = 19990;
    for ( const auto& test : cases )
    {
        vhTexture texture = vhAllocTexture();
        vhCreateTexture2D( texture, "ClearOnlyLifetime", { 16, 16 }, 1, test.format, VRHI_TEXTURE_RT );
        vhState state;
        if ( test.flags & VRHI_CLEAR_COLOR ) state.SetColourAttachment( 0, texture );
        else state.SetDepthAttachment( texture );
        state.SetViewClear( test.flags, glm::vec4( 0.25f ), 0.5f, 7 );
        vhSetState( sid, state );
        vhFinish();

        // Keep the negative case safe: the test owns one reference, so a missing GPU
        // reference fails the count check without freeing an image still in flight.
        auto keepAlive = vhGetTextureNvrhiHandle( texture );
        ASSERT_NE( keepAlive, nullptr );
        {
            vrhi_test::VhGpuGate gate( utest_result );
            if ( !gate.Supported() )
            {
                vhDestroyTexture( texture );
                vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
                vhFinish();
                UTEST_SKIP( "Timeline semaphore unsupported" );
            }
            gate.WaitBefore();
            vhClear( sid, test.flags );
            vhFlush( true );
            gate.CaptureReader();
            ASSERT_TRUE( gate.Pending() );

            vhDestroyTexture( texture );
            vhSetState( sid, g_state0, VRHI_DIRTY_ALL );
            vhFlush( true );
            EXPECT_TRUE( gate.Pending() );
            EXPECT_GT( keepAlive->GetRefCount(), 1ul );
            EXPECT_FALSE( gate.TimedOut() );
            gate.Release();
        }
        vhFinish();
        EXPECT_EQ( keepAlive->GetRefCount(), 1ul );
    }
    EXPECT_EQ( g_vhErrorCounter.load(), errors );
}

UTEST( Backend, GpuGate_ArmedReturnConsumesWait )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Native gate requires Vulkan" );
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
    const int32_t errors = g_vhErrorCounter.load();
    {
        vrhi_test::VhGpuGate gate( utest_result );
        if ( !gate.Supported() ) UTEST_SKIP( "Timeline semaphore unsupported" );
        gate.WaitBefore();
        // An early return before submission must still consume the queued wait.
    }
    vhFinish();
    EXPECT_EQ( g_vhErrorCounter.load(), errors );
}

UTEST( Backend, GpuGate_EarlyReturnReleasesReaderAndProducer )
{
    if ( g_vhInit.nullMode ) UTEST_SKIP( "Native gate requires Vulkan" );
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
    const int32_t errors = g_vhErrorCounter.load();
    vrhi_test::VhGpuGate supported( utest_result );
    if ( !supported.Supported() ) UTEST_SKIP( "Timeline semaphore unsupported" );
    auto reader = g_vhDevice->createEventQuery();
    std::atomic< bool > finished = false;
    auto earlyReturn = [&]
    {
        vrhi_test::VhGpuGate gate( utest_result );
        if ( !gate.Supported() ) return;
        gate.WaitBefore();
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            auto commands = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
            vhCmdListFlush_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
            g_vhDevice->setEventQuery( reader, nvrhi::CommandQueue::Graphics );
        }
        EXPECT_FALSE( g_vhDevice->pollEventQuery( reader ) );
        gate.StartProducer( [&]
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            g_vhDevice->waitEventQuery( reader );
            finished.store( true );
        } );
        return;
    };
    earlyReturn();
    EXPECT_TRUE( finished.load() );
    EXPECT_TRUE( g_vhDevice->pollEventQuery( reader ) );
    vhFinish();
    EXPECT_EQ( g_vhErrorCounter.load(), errors );
}

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
#include "utest.h"
#include "test.h"
#include <vrhi.h>
#include <vrhi_backend.h>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;

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

    static bool PreSubmitCommon_State(
        vhState& state,
        vhBackendShader* shaders,
        int shaderCount,
        nvrhi::ComputeState* compute,
        nvrhi::GraphicsState* graphics
    )
    {
        return Get().BE_PreSubmitCommon_State( state, shaders, shaderCount, compute, graphics );
    }

    static bool GetFrameBuffer( const std::vector< vhTexture >& colors, vhTexture depth )
    {
        auto fb1 = Get().BE_GetFrameBuffer( colors, depth, 0, 0 );
        auto fb2 = Get().BE_GetFrameBuffer( colors, depth, 0, 0 );

        if ( !fb1 || !fb2 ) return false;
        return fb1.Get() == fb2.Get();
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
};


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
    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shader, 1, nullptr, &graphicsDesc ) );

    // Case 2: Valid shader, no pipeline desc
    // Returns true (success) because it simply matches no stages and exits cleanly.
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shader, 1, nullptr, nullptr ) );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = nullptr;
    }
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
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, &shader, 1, &computeDesc, nullptr ) );
    EXPECT_EQ( computeDesc.CS.Get(), shader.handle.Get() );
    EXPECT_EQ( computeDesc.bindingLayouts.size(), 1u );
    EXPECT_EQ( computeDesc.bindingLayouts[0].Get(), shader.layout.Get() );

    // Mixed Shaders - Only compute should be picked up
    vhBackendShader shaders[2];
    shaders[0] = shader;
    shaders[1].handle = nullptr;
    shaders[1].flags = VRHI_SHADER_STAGE_VERTEX;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shaders[1].layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::ComputePipelineDesc computeDesc2;
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_PipelineDesc( state, shaders, 2, &computeDesc2, nullptr ) );
    EXPECT_EQ( computeDesc2.CS.Get(), shaders[0].handle.Get() );
    EXPECT_EQ( computeDesc2.bindingLayouts.size(), 1u );
    EXPECT_EQ( computeDesc2.bindingLayouts[0].Get(), shaders[0].layout.Get() );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = nullptr;
        shaders[0].layout = nullptr;
        shaders[1].layout = nullptr;
    }
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

    EXPECT_FALSE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shaderFail, 1, &computeStateFail, nullptr ) );

    // CASE: Happy Path
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shader, 1, &computeState, nullptr ) );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layoutFail = nullptr;
        shader.layout = nullptr;
    }
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
    EXPECT_TRUE( vhCmdBackendStateTest::GetFrameBuffer( { colour }, depth ) );

    vhDestroyTexture( colour );
    vhDestroyTexture( depth );
    vhFinish();
}

UTEST( BackendInternal, FindResource )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Create dummy resources
    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, { 64, 64 }, 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_COMPUTE_WRITE );
    
    vhBuffer buf = vhAllocBuffer();
    vhCreateStorageBuffer( buf, "TestBuf", nullptr, 1024 );

    vhFinish();

    nvrhi::TextureHandle hTex = ( nvrhi::ITexture* ) vhGetTextureNvrhiHandle( tex );
    nvrhi::BufferHandle hBuf = ( nvrhi::IBuffer* ) vhGetBufferNvrhiHandle( buf );

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
    scache.stageBinding[stage] = std::make_unique< vhStateResolveCache::ShaderStageBindingSlotState >();
    auto& stageTable = *scache.stageBinding[stage];

    stageTable.textureTable[0] = { hTex, &texBind };
    stageTable.bufferTable[1] = { hBuf, &bufBind };
    stageTable.uavTable[2].first = { hTex, &texBind };
    stageTable.uavTable[3].second = { hBuf, &bufBind };

    nvrhi::BindingSetItem outItem;

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

    // Test Sampler
    uint64_t samplerFlags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP;
    nvrhi::SamplerHandle hSampler = vhGetSamplerHandle( samplerFlags );
    EXPECT_TRUE( hSampler != nullptr );
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

    vhDestroyTexture( tex );
    vhDestroyBuffer( buf );
    vhFinish();
}
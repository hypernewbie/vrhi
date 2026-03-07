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
#include <vrhi.h>
#include <vrhi_internal.h>
#include <vrhi_backend.h>
#include <glm/gtc/matrix_transform.hpp>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;
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
        nvrhi::GraphicsState* graphics,
        nvrhi::CommandListHandle cmdList = nullptr
    )
    {
        return Get().BE_PreSubmitCommon_State( cmdList, state, shaders, shaderCount, compute, graphics );
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

    static int64_t Util_WriteGlobalUniform( const vhState& state, vhTransientBuffer& tbuf, uint64_t& lastHash )
    {
        return Get().BE_Util_WriteGlobalUniform( state, tbuf, lastHash );
    }

    static int64_t Util_WriteWorldUniform( const vhState& state, vhTransientBuffer& tbuf, uint64_t& lastHash )
    {
        return Get().BE_Util_WriteWorldUniform( state, tbuf, lastHash );
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
    vhCreateStorageBuffer( buf, "TestBuf", nullptr, 1024 );

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

    uint64_t lastHash = 0;
    vhState state = {};
    state.viewRect = glm::vec4( 0, 0, 100, 100 );

    // First Write
    int64_t offset1 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb, lastHash );
    
    EXPECT_GE( offset1, 0 );
    EXPECT_EQ( offset1, 0 );
    EXPECT_NE( lastHash, 0ull );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );

    // Duplicate Write (Dedup)
    int64_t offset2 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb, lastHash );
    EXPECT_EQ( offset2, offset1 ); // Should reuse
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) ); // Should not advance

    // New Write (Mod state)
    state.viewRect = glm::vec4( 10, 10, 200, 200 );
    int64_t offset3 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb, lastHash );
    
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
    auto cmdList = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
         std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
         cmdList->copyBuffer( stagingBuffer, 0, tb.handle[tb.frameIdx], offset3, sizeof( vhGlobalUniform ) );
    }

    // Flush and Wait
    vhCmdListFlush( nvrhi::CommandQueue::Graphics );
    g_vhDevice->waitForIdle();

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
    
    // lastHash is maintained by caller.
    // If we write same state again, it should NOT dedup because offset is 0
    int64_t offset4 = vhCmdBackendStateTest::Util_WriteGlobalUniform( state, tb, lastHash );
    EXPECT_EQ( offset4, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhGlobalUniform ) );

    // Shutdown
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tb.Shutdown_DeviceStateLocked();
    }
    vhFinish();
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

    uint64_t lastHash = 0;
    vhState state = {};
    state.worldMatrix.push_back( glm::mat4( 1.0f ) ); // Identity

    // First Write
    int64_t offset1 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb, lastHash );
    EXPECT_GE( offset1, 0 );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );

    // Dedup
    int64_t offset2 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb, lastHash );
    EXPECT_EQ( offset2, offset1 );
    EXPECT_EQ( tb.offset, sizeof( vhWorldUniform ) );

    // Mod
    state.worldMatrix[0] = glm::translate( glm::mat4( 1.0f ), glm::vec3( 1, 0, 0 ) );
    int64_t offset3 = vhCmdBackendStateTest::Util_WriteWorldUniform( state, tb, lastHash );
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
        auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
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

    vhFlush();
    g_vhDevice->waitForIdle();

    // Still should be 0.0f because ring buffer delay (3 frames)
    EXPECT_EQ( vhGetTimerQueryTime( timerID ), 0.0f );

    // Simulate frames to advance ring buffer
    for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT + 1; ++i )
    {
        vhBeginTimerQuery( timerID );
        vhEndTimerQuery( timerID );
        vhFlush();
        g_vhDevice->waitForIdle();
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
        
        vhFlush();
        g_vhDevice->waitForIdle();
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
    vhFlush(); 
    g_vhDevice->waitForIdle();
    
    // Check invalid ID returns 0
    EXPECT_EQ( vhGetTimerQueryTime( 0x999 ), 0.0f );
    vhFinish();
}

UTEST( Backend, SparseBindings )
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

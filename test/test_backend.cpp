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
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }
    vhCmdBackendStateTest::Init();

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
    
    vhCmdBackendStateTest::Shutdown();

    vhDestroyTexture( tex );
    vhDestroyBuffer( buf );
    vhFinish();
}



UTEST( Backend, Util_WriteGlobalUniform )
{
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

    gstate.pipeline = new MockGraphicsPipeline( layouts );
    
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shader, 1, nullptr, &gstate ) );
    
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
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shader, 1, nullptr, &gstate ) );
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
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shader, 1, nullptr, &gstate ) );
    
    EXPECT_NE( gstate.indexBuffer.buffer, nullptr );
    EXPECT_EQ( gstate.indexBuffer.offset, 256u );
    EXPECT_EQ( gstate.indexBuffer.format, nvrhi::Format::R32_UINT );

    // Test 16-bit Index Buffer
    vhBuffer ib16 = vhAllocBuffer();
    {
        auto bbuf = new vhBackendBuffer();
        nvrhi::BufferDesc desc; desc.setByteSize( 1024 ); desc.setIsIndexBuffer( true );
        { std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); bbuf->handle = g_vhDevice->createBuffer( desc ); }
        bbuf->flags = 0;
        bbuf->stride = 2;
        vhCmdBackendStateTest::InsertDummyBuffer( ib16, bbuf );
    }

    state.SetIndexBuffer( ib16, 128 );
    EXPECT_TRUE( vhCmdBackendStateTest::PreSubmitCommon_State( state, &shader, 1, nullptr, &gstate ) );

    EXPECT_NE( gstate.indexBuffer.buffer, nullptr );
    EXPECT_EQ( gstate.indexBuffer.offset, 128u );
    EXPECT_EQ( gstate.indexBuffer.format, nvrhi::Format::R16_UINT );

    // Cleanup
    vhCmdBackendStateTest::Shutdown();
    vhDestroyBuffer( vb );
    vhDestroyBuffer( vb2 );
    vhDestroyBuffer( vb3 );
    vhDestroyBuffer( ib32 );
    vhDestroyBuffer( ib16 );
    vhDestroyTexture( rtTex );
    
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        shader.layout = nullptr;
    }
}

UTEST( Backend, Util_WriteWorldUniform )
{
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
}
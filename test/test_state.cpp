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

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "utest.h"
#include "test.h"
#include <vrhi.h>
#include <vrhi_internal.h>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;

UTEST( Translate, VertexAttribute )
{
    vhVertexLayoutDef def;
    def.format = nvrhi::Format::RGB32_FLOAT;
    def.location = 5;
    def.offset = 12;

    nvrhi::VertexAttributeDesc attr = vhTranslateVertexAttribute( def, 2 );

    EXPECT_EQ( attr.format, nvrhi::Format::RGB32_FLOAT );
    EXPECT_STREQ( attr.name.c_str(), "ATTR5" );
    EXPECT_EQ( attr.bufferIndex, 2 );
    EXPECT_EQ( attr.offset, 12 );
    EXPECT_EQ( attr.elementStride, 0 ); // Default
    EXPECT_FALSE( attr.isInstanced );
}

UTEST( State, MultipleSlots )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state1 = {}, state2 = {};
    state1.SetViewRect( glm::vec4( 0, 0, 100, 100 ) );
    state2.SetViewRect( glm::vec4( 0, 0, 200, 200 ) );

    vhStateId id1 = 10, id2 = 20;
    vhSetState( id1, state1 );
    vhSetState( id2, state2 );
    vhFlush();

    vhState r1 = {}, r2 = {};
    ASSERT_TRUE( vhGetState( id1, r1 ) );
    ASSERT_TRUE( vhGetState( id2, r2 ) );

    EXPECT_EQ( r1.viewRect, state1.viewRect );
    EXPECT_EQ( r2.viewRect, state2.viewRect );
}

UTEST( State, InvalidId )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state = {};
    vhStateId nonExistent = 999999;

    // GetState should return false for non-existent ID
    ASSERT_FALSE( vhGetState( nonExistent, state ) );
}

UTEST( State, BasicSetGet )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhState state = {};
    state.SetViewRect( glm::vec4( 0, 0, 1280, 720 ) )
        .SetViewTransform( glm::mat4( 1.0f ), glm::mat4( 2.0f ) )
        .SetWorldTransform( glm::mat4( 3.0f ), 1 );

    vhStateId id = 1;
    ASSERT_TRUE( vhSetState( id, state ) );
    vhFlush(); // Wait for command to process

    vhState retrieved = {};
    ASSERT_TRUE( vhGetState( id, retrieved ) );

    EXPECT_EQ( retrieved.viewRect, state.viewRect );
    EXPECT_EQ( retrieved.viewMatrix, state.viewMatrix );
    EXPECT_EQ( retrieved.projMatrix, state.projMatrix );
    ASSERT_GT( retrieved.worldMatrix.size(), 0 );
    EXPECT_EQ( retrieved.worldMatrix[0], state.worldMatrix[0] );
}

UTEST( State, Attachments )
{
    vhState state = {};
    vhState::RenderTarget rt;
    rt.texture = 101;
    rt.mipLevel = 1;

    std::vector< vhState::RenderTarget > colours = { rt };
    vhState::RenderTarget depth;
    depth.texture = 201;

    state.SetAttachments( colours, depth );

    vhStateId id = 500;
    ASSERT_TRUE( vhSetState( id, state ) );
    vhFlush();

    vhState retrieved = {};
    ASSERT_TRUE( vhGetState( id, retrieved ) );

    ASSERT_EQ( retrieved.colourAttachment.size(), ( size_t ) 1 );
    EXPECT_EQ( retrieved.colourAttachment[0].texture, 101u );
    EXPECT_EQ( retrieved.colourAttachment[0].mipLevel, 1u );
    EXPECT_EQ( retrieved.depthAttachment.texture, 201u );
}

UTEST( State, Extensions )
{
    // Test 1: Dirty Flags for Vertex/Index
    {
        vhState state;
        state.SetVertexBuffer( 1, 0 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_VERTEX_INDEX, VRHI_DIRTY_VERTEX_INDEX );
        state.dirty = 0;
        state.SetIndexBuffer( 2 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_VERTEX_INDEX, VRHI_DIRTY_VERTEX_INDEX );
    }

    // Test 2: Dirty Flags for Textures/Samplers
    {
        vhState state;
        state.SetTextures( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_TEXTURE_SAMPLERS, VRHI_DIRTY_TEXTURE_SAMPLERS );
        state.dirty = 0;
        state.SetSamplers( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_TEXTURE_SAMPLERS, VRHI_DIRTY_TEXTURE_SAMPLERS );
    }

    // Test 3: Dirty Flags for Buffers
    {
        vhState state;
        state.SetBuffers( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_BUFFERS, VRHI_DIRTY_BUFFERS );
    }

    // Test 4: Dirty Flags for Constants
    {
        vhState state;
        state.SetConstants( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_CONSTANTS, VRHI_DIRTY_CONSTANTS );
    }

    // Test 5: Dirty Flags for PushConstants
    {
        vhState state;
        state.SetPushConstants( glm::vec4( 1.0f ) );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_PUSH_CONSTANTS, VRHI_DIRTY_PUSH_CONSTANTS );
    }

    // Test 6: Dirty Flags for Program
    {
        vhState state;
        state.SetProgram( { 777 } );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_PROGRAM, VRHI_DIRTY_PROGRAM );
    }

    // Test 7: Dirty Flags for Uniforms
    {
        vhState state;
        state.SetUniforms( {} );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_UNIFORMS, VRHI_DIRTY_UNIFORMS );
    }
}

UTEST( State, BackendPropagation )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    vhStateId id = 123;
    vhState state;

    // Set some state
    state.SetPushConstants( glm::vec4( 1.1f, 2.2f, 3.3f, 4.4f ) );

    vhState::TextureBinding tex;
    tex.name = "PropTex";
    tex.slot = 3;
    tex.texture = 101;
    state.SetTextures( { tex } );

    vhSetState( id, state );
    vhFlush();

    // Verify backend state
    vhState backendState;
    EXPECT_TRUE( vhGetState( id, backendState ) );

    EXPECT_NEAR( backendState.pushConstants.x, 1.1f, 0.001f );
    EXPECT_NEAR( backendState.pushConstants.y, 2.2f, 0.001f );
    EXPECT_NEAR( backendState.pushConstants.z, 3.3f, 0.001f );
    EXPECT_NEAR( backendState.pushConstants.w, 4.4f, 0.001f );

    ASSERT_EQ( backendState.textures.size(), 1u );
    EXPECT_STREQ( backendState.textures[0].name, "PropTex" );
    EXPECT_EQ( backendState.textures[0].slot, 3 );
    EXPECT_EQ( backendState.textures[0].texture, 101u );

    // Verify it doesn't bleed to other states
    vhState otherState;
    EXPECT_FALSE( vhGetState( 999, otherState ) );
}

UTEST( State, IndividualAccessors )
{
    vhState state;

    // Texture with auto-resize
    {
        vhState::TextureBinding tex;
        tex.name = "ResizeTex";
        tex.slot = 10;
        state.SetTexture( 5, tex ); // Index 5, so size should be 6

        EXPECT_EQ( state.textures.size(), 6u );
        EXPECT_STREQ( state.textures[5].name, "ResizeTex" );
        EXPECT_EQ( state.textures[5].slot, 10 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_TEXTURE_SAMPLERS, VRHI_DIRTY_TEXTURE_SAMPLERS );

        // Get with resize
        state.GetTexture( 8 ); // Index 8, size -> 9
        EXPECT_EQ( state.textures.size(), 9u );
    }

    // Sampler with auto-resize
    {
        vhState::SamplerDefinition samp;
        samp.slot = 20;
        state.SetSampler( 3, samp );

        EXPECT_EQ( state.samplers.size(), 4u );
        EXPECT_EQ( state.samplers[3].slot, 20 );

        state.GetSampler( 6 );
        EXPECT_EQ( state.samplers.size(), 7u );
    }

    // Buffer with auto-resize
    {
        vhState::BufferBinding buf;
        buf.slot = 30;
        state.SetBuffer( 4, buf );

        EXPECT_EQ( state.buffers.size(), 5u );
        EXPECT_EQ( state.buffers[4].slot, 30 );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_BUFFERS, VRHI_DIRTY_BUFFERS );

        state.GetBuffer( 5 );
        EXPECT_EQ( state.buffers.size(), 6u );
    }

    // Constant with auto-resize
    {
        vhState::ConstantBufferValue c;
        c.name = "ConstBuf";
        state.SetConstant( 2, c );

        EXPECT_EQ( state.constants.size(), 3u );
        EXPECT_STREQ( state.constants[2].name, "ConstBuf" );
        EXPECT_EQ( state.dirty & VRHI_DIRTY_CONSTANTS, VRHI_DIRTY_CONSTANTS );

        state.GetConstant( 4 );
        EXPECT_EQ( state.constants.size(), 5u );
    }
}

UTEST( State, IndividualAttachments )
{
    vhState state;

    // Set color attachment at index 2 (forces resize)
    state.SetColourAttachment( 2, 101, 1, 2, nvrhi::Format::RGBA8_UNORM, true );

    EXPECT_EQ( state.colourAttachment.size(), 3u );
    EXPECT_EQ( state.colourAttachment[2].texture, 101u );
    EXPECT_EQ( state.colourAttachment[2].mipLevel, 1u );
    EXPECT_EQ( state.colourAttachment[2].arrayLayer, 2u );
    EXPECT_EQ( state.colourAttachment[2].formatOverride, nvrhi::Format::RGBA8_UNORM );
    EXPECT_TRUE( state.colourAttachment[2].readOnly );
    EXPECT_EQ( ( state.dirty & VRHI_DIRTY_ATTACHMENTS ), VRHI_DIRTY_ATTACHMENTS );

    // Reset dirty and check depth
    state.dirty = 0;
    state.SetDepthAttachment( 201, 0, 0, nvrhi::Format::D32, false );

    EXPECT_EQ( state.depthAttachment.texture, 201u );
    EXPECT_EQ( state.depthAttachment.formatOverride, nvrhi::Format::D32 );
    EXPECT_FALSE( state.depthAttachment.readOnly );
    EXPECT_EQ( ( state.dirty & VRHI_DIRTY_ATTACHMENTS ), VRHI_DIRTY_ATTACHMENTS );
}

UTEST( State, DebugFlags )
{
    vhState state;
    state.SetDebugFlags( VRHI_STATE_DEBUG_LOG_MISSING_BINDINGS );

    vhStateId id = 600;
    ASSERT_TRUE( vhSetState( id, state ) );
    vhFlush();

    vhState retrieved = {};
    ASSERT_TRUE( vhGetState( id, retrieved ) );
    EXPECT_EQ( retrieved.debugFlags, VRHI_STATE_DEBUG_LOG_MISSING_BINDINGS );
}

UTEST( Hashing, GraphicsPipeline )
{
    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    nvrhi::FramebufferInfo fbInfo;

    // 1. Stability
    uint64_t hash1 = vhHashGraphicsPipeline( desc, fbInfo );
    uint64_t hash2 = vhHashGraphicsPipeline( desc, fbInfo );
    EXPECT_EQ( hash1, hash2 );

    // 2. Sensitivity (RenderState - Blend)
    desc.renderState.blendState.targets[0].blendEnable = true;
    uint64_t hash3 = vhHashGraphicsPipeline( desc, fbInfo );
    EXPECT_NE( hash1, hash3 );

    // 3. Sensitivity (RenderState - Raster)
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Front;
    uint64_t hash4 = vhHashGraphicsPipeline( desc, fbInfo );
    EXPECT_NE( hash3, hash4 );

    // 4. Sensitivity (Framebuffer)
    fbInfo.sampleCount = 4;
    uint64_t hash5 = vhHashGraphicsPipeline( desc, fbInfo );
    EXPECT_NE( hash4, hash5 );
}

UTEST( Hashing, ComputePipeline )
{
    nvrhi::ComputePipelineDesc desc;

    // 1. Stability
    uint64_t h1 = vhHashComputePipeline( desc );
    uint64_t h2 = vhHashComputePipeline( desc );
    EXPECT_EQ( h1, h2 );
}

UTEST( Hashing, BindingLayout )
{
    nvrhi::BindingLayoutDesc desc1;
    desc1.visibility = nvrhi::ShaderType::AllGraphics;
    desc1.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    desc1.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 1 ) );

    nvrhi::BindingLayoutDesc desc2 = desc1; // Same

    nvrhi::BindingLayoutDesc desc3; // Diff
    desc3.visibility = nvrhi::ShaderType::Compute;
    desc3.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );

    uint64_t h1 = vhHashBindingLayout( desc1 );
    uint64_t h2 = vhHashBindingLayout( desc2 );
    uint64_t h3 = vhHashBindingLayout( desc3 );

    EXPECT_NE( h1, 0 );
    EXPECT_EQ( h1, h2 );
    EXPECT_NE( h1, h3 );

    // Test ordering sensitivity
    nvrhi::BindingLayoutDesc desc4;
    desc4.visibility = nvrhi::ShaderType::AllGraphics;
    desc4.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 1 ) ); // Swapped order
    desc4.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );

    uint64_t h4 = vhHashBindingLayout( desc4 );
    EXPECT_NE( h1, h4 );
}

UTEST( Hashing, InputLayout )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    nvrhi::VertexAttributeDesc attr1;
    attr1.name = "POSITION";
    attr1.format = nvrhi::Format::RGB32_FLOAT;
    attr1.bufferIndex = 0;
    attr1.elementStride = 12;

    nvrhi::VertexAttributeDesc attr2;
    attr2.name = "TEXCOORD";
    attr2.format = nvrhi::Format::RG32_FLOAT;
    attr2.bufferIndex = 1;
    attr2.elementStride = 8;

    std::vector<nvrhi::VertexAttributeDesc> attrs1 = { attr1, attr2 };
    nvrhi::InputLayoutHandle layout1;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout1 = g_vhDevice->createInputLayout( attrs1.data(), ( uint32_t ) attrs1.size(), nullptr );
    }

    std::vector<nvrhi::VertexAttributeDesc> attrs2 = { attr1, attr2 };
    nvrhi::InputLayoutHandle layout2;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout2 = g_vhDevice->createInputLayout( attrs2.data(), ( uint32_t ) attrs2.size(), nullptr );
    }

    std::vector<nvrhi::VertexAttributeDesc> attrs3 = { attr2, attr1 };
    nvrhi::InputLayoutHandle layout3;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout3 = g_vhDevice->createInputLayout( attrs3.data(), ( uint32_t ) attrs3.size(), nullptr );
    }

    ASSERT_NE( layout1, nullptr );
    ASSERT_NE( layout2, nullptr );
    ASSERT_NE( layout3, nullptr );

    uint64_t h1 = vhHashInputLayout( layout1 );
    uint64_t h2 = vhHashInputLayout( layout2 );
    uint64_t h3 = vhHashInputLayout( layout3 );

    EXPECT_NE( h1, 0 );
    EXPECT_EQ( h1, h2 );
    EXPECT_NE( h1, h3 );

    vhFinish();

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout1 = nullptr;
        layout2 = nullptr;
        layout3 = nullptr;
    }
}

UTEST( Hashing, BindingSet_Basics )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, nullptr ) );

    // Consistency check
    uint64_t h1 = vhHashBindingSet( desc, layout );
    uint64_t h2 = vhHashBindingSet( desc, layout );
    EXPECT_EQ( h1, h2 );

    // Layout dependency check
    nvrhi::BindingLayoutDesc layoutDesc2;
    layoutDesc2.visibility = nvrhi::ShaderType::All;
    layoutDesc2.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 1 ) );
    nvrhi::BindingLayoutHandle layout2;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout2 = g_vhDevice->createBindingLayout( layoutDesc2 );
    }
    uint64_t h3 = vhHashBindingSet( desc, layout2 );
    EXPECT_NE( h1, h3 );

    // Liveness dependency check
    nvrhi::BindingSetDesc desc2 = desc;
    desc2.trackLiveness = !desc.trackLiveness;
    uint64_t h4 = vhHashBindingSet( desc2, layout );
    EXPECT_NE( h1, h4 );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = nullptr;
        layout2 = nullptr;
    }
}

UTEST( Hashing, BindingSet_Differentiation )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::BindingSetDesc baseDesc;
    baseDesc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, nullptr ) );
    uint64_t baseHash = vhHashBindingSet( baseDesc, layout );

    // Check slot differentiation
    {
        nvrhi::BindingSetDesc desc = baseDesc;
        desc.bindings[0].slot = 1;
        EXPECT_NE( baseHash, vhHashBindingSet( desc, layout ) );
    }

    // Check type differentiation
    {
        nvrhi::BindingSetDesc desc = baseDesc;
        desc.bindings[0].type = nvrhi::ResourceType::Texture_UAV;
        EXPECT_NE( baseHash, vhHashBindingSet( desc, layout ) );
    }

    // Check resource handle differentiation
    {
        nvrhi::BindingSetDesc desc = baseDesc;
        desc.bindings[0].resourceHandle = ( nvrhi::ITexture* ) 0x12345678;
        EXPECT_NE( baseHash, vhHashBindingSet( desc, layout ) );
    }

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = nullptr;
    }
}

UTEST( Hashing, BindingSet_ViewParameters )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    // Handle is NULL: view parameters should NOT change the hash
    {
        nvrhi::BindingSetDesc desc;
        desc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, nullptr ) );
        uint64_t nullHandleHash = vhHashBindingSet( desc, layout );

        desc.bindings[0].format = nvrhi::Format::RGBA32_FLOAT;
        EXPECT_EQ( nullHandleHash, vhHashBindingSet( desc, layout ) );

        desc.bindings[0].dimension = nvrhi::TextureDimension::TextureCube;
        EXPECT_EQ( nullHandleHash, vhHashBindingSet( desc, layout ) );

        desc.bindings[0].subresources.baseMipLevel = 5;
        EXPECT_EQ( nullHandleHash, vhHashBindingSet( desc, layout ) );
    }

    // Handle is NON-NULL: view parameters SHOULD change the hash
    {
        nvrhi::BindingSetDesc desc;
        desc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, ( nvrhi::ITexture* ) 0xDEADBEEFull ) );
        uint64_t validHandleHash = vhHashBindingSet( desc, layout );

        // Check format impact
        desc.bindings[0].format = nvrhi::Format::RGBA32_FLOAT;
        uint64_t fmtHash = vhHashBindingSet( desc, layout );
        EXPECT_NE( validHandleHash, fmtHash );

        // Check dimension impact
        desc.bindings[0].dimension = nvrhi::TextureDimension::TextureCube;
        uint64_t dimHash = vhHashBindingSet( desc, layout );
        EXPECT_NE( fmtHash, dimHash );

        // Check subresource impact
        desc.bindings[0].subresources.baseMipLevel = 5;
        uint64_t mipHash = vhHashBindingSet( desc, layout );
        EXPECT_NE( dimHash, mipHash );

        desc.bindings[0].subresources.numMipLevels = 2;
        EXPECT_NE( mipHash, vhHashBindingSet( desc, layout ) );
    }

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = nullptr;
    }
}

UTEST( Hashing, BindingSet_ArrayElement )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, nullptr ) );

    desc.bindings[0].arrayElement = 0;
    uint64_t h0 = vhHashBindingSet( desc, layout );

    desc.bindings[0].arrayElement = 1;
    uint64_t h1 = vhHashBindingSet( desc, layout );

    EXPECT_NE( h0, h1 );

    vhFlush();

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = nullptr;
    }
}

UTEST( Hashing, BindingSet_RawData )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::Texture_SRV( 0, ( nvrhi::ITexture* ) 0x1 ) );

    // Verify rawData[0] impact
    desc.bindings[0].rawData[0] = 100;
    uint64_t h0 = vhHashBindingSet( desc, layout );

    desc.bindings[0].rawData[0] = 200;
    uint64_t h1 = vhHashBindingSet( desc, layout );
    EXPECT_NE( h0, h1 );

    // Verify rawData[1] impact
    desc.bindings[0].rawData[1] = 500;
    uint64_t h2 = vhHashBindingSet( desc, layout );
    EXPECT_NE( h1, h2 );

    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        layout = nullptr;
    }
}

UTEST( Debug, LayoutDiffCheck )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Create a simple buffer for binding
    nvrhi::BufferHandle buffer;
    {
        nvrhi::BufferDesc bufDesc;
        bufDesc.byteSize = 256;
        bufDesc.isConstantBuffer = true;
        bufDesc.debugName = "TestCB";
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        buffer = g_vhDevice->createBuffer( bufDesc );
    }
    ASSERT_NE( buffer, nullptr );

    // 1. Create Layout A (ConstantBuffer at Slot 0)
    nvrhi::BindingLayoutHandle layoutA;
    {
        nvrhi::BindingLayoutDesc desc;
        desc.visibility = nvrhi::ShaderType::All;
        desc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 0 ) );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layoutA = g_vhDevice->createBindingLayout( desc );
    }
    ASSERT_NE( layoutA, nullptr );

    // 2. Create Layout B (Texture_SRV at Slot 0)
    nvrhi::BindingLayoutHandle layoutB;
    {
        nvrhi::BindingLayoutDesc desc;
        desc.visibility = nvrhi::ShaderType::All;
        desc.addItem( nvrhi::BindingLayoutItem::Texture_SRV( 0 ) );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        layoutB = g_vhDevice->createBindingLayout( desc );
    }
    ASSERT_NE( layoutB, nullptr );

    // 3. Create Binding Set A (Matches Layout A)
    nvrhi::BindingSetHandle setA;
    {
        nvrhi::BindingSetDesc desc;
        desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, buffer ) );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        setA = g_vhDevice->createBindingSet( desc, layoutA );
    }
    ASSERT_NE( setA, nullptr );

    // Test 1: Perfect Match
    {
        nvrhi::BindingLayoutVector layouts;
        layouts.push_back( layoutA );
        nvrhi::BindingSetVector sets;
        sets.push_back( setA );
        EXPECT_TRUE( vhDebugLayoutDiffCheck( layouts, sets ) );
    }

    // Test 2: Count Mismatch
    {
        nvrhi::BindingLayoutVector layouts;
        layouts.push_back( layoutA );
        layouts.push_back( layoutB ); // 2 layouts
        nvrhi::BindingSetVector sets;
        sets.push_back( setA ); // 1 set
        EXPECT_FALSE( vhDebugLayoutDiffCheck( layouts, sets ) );
    }

    // Test 3: Type Mismatch (Layout B wants Texture, Set A has ConstantBuffer)
    {
        nvrhi::BindingLayoutVector layouts;
        layouts.push_back( layoutB );
        nvrhi::BindingSetVector sets;
        sets.push_back( setA );
        EXPECT_FALSE( vhDebugLayoutDiffCheck( layouts, sets ) );
    }

    // Test 4: Create Binding Set Null (Matches Layout A slots, but null resource)
    nvrhi::BindingSetHandle setNull;
    {
        nvrhi::BindingSetDesc desc;
        desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, nullptr ) );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        setNull = g_vhDevice->createBindingSet( desc, layoutA );
    }
    
    if ( setNull )
    {
        nvrhi::BindingLayoutVector layouts;
        layouts.push_back( layoutA );
        nvrhi::BindingSetVector sets;
        sets.push_back( setNull );
        EXPECT_FALSE( vhDebugLayoutDiffCheck( layouts, sets ) );
        
        {
             std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
             setNull = nullptr;
        }
    }

    // Clean up
    vhFlush();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        setA = nullptr;
        layoutA = nullptr;
        layoutB = nullptr;
        buffer = nullptr;
    }
}

UTEST( State, DirtyAll )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    const vhStateId sid = 1337;
    
    // Ensure we start from a clean slate for this ID
    vhState empty = {};
    vhSetState( sid, empty, VRHI_DIRTY_ALL );
    vhFlush();

    vhState localState = {};
    localState.SetViewRect( glm::vec4( 10, 20, 30, 40 ) );
    localState.dirty = 0x0ull; // Manually clear dirty bits

    // 1. Null test: passing 0x0 force mask with no dirty bits should NOT update the backend
    vhSetState( sid, localState, 0x0ull );
    vhFlush();

    vhState retrieved = {};
    ASSERT_TRUE( vhGetState( sid, retrieved ) );
    EXPECT_NE( retrieved.viewRect, localState.viewRect );
    EXPECT_EQ( retrieved.viewRect, empty.viewRect );

    // 2. Force test: passing VRHI_DIRTY_ALL should update everything regardless of local dirty bits
    vhSetState( sid, localState, VRHI_DIRTY_ALL );
    vhFlush();

    ASSERT_TRUE( vhGetState( sid, retrieved ) );
    EXPECT_EQ( retrieved.viewRect, localState.viewRect );

    // 3. Cleanup: reset the state ID to avoid leaving leftovers
    vhSetState( sid, empty, VRHI_DIRTY_ALL );
    vhFlush();
} 

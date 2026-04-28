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

#include "vrhi_internal.h"
#include "vrhi_utils.h"
#include "vrhi_backend.h"
#include "vdeps/OffsetAllocator/offsetAllocator.hpp"
#include <komihash/komihash.h>
#include <set>

vhCmdBackendState g_vhCmdBackendState;
void vhCmdListFlushAll_DeviceStateLocked();

robin_hood::unordered_flat_map< nvrhi::BindingLayoutHandle, vhBackendShader* > vhCmdBackendState::s_layoutToShader;
vhStateResolveCache vhCmdBackendState::s_resolveCache;
std::vector< vhShaderReflectionResource* > vhCmdBackendState::s_slotToReflection;
std::unordered_map< uint64_t, const vhVertexLayoutDef* > vhCmdBackendState::s_layoutLocationTable;
std::vector< nvrhi::VertexAttributeDesc > vhCmdBackendState::s_attributes;
std::vector< vhBackendShader* > vhCmdBackendState::s_shaders;
std::vector< vhBackendShader* > vhCmdBackendState::s_lastLayoutMapShaders;
nvrhi::GraphicsPipelineDesc vhCmdBackendState::s_submitPipelineDesc;
nvrhi::GraphicsState        vhCmdBackendState::s_submitGState;
nvrhi::ComputePipelineDesc  vhCmdBackendState::s_dispatchDesc;
nvrhi::ComputeState         vhCmdBackendState::s_dispatchCState;
nvrhi::IGraphicsPipeline*   vhCmdBackendState::s_lastGraphicsPSO = nullptr;
nvrhi::IComputePipeline*    vhCmdBackendState::s_lastComputePSO = nullptr;

static void vhResetGraphicsPipelineDesc( nvrhi::GraphicsPipelineDesc& desc )
{
    desc.VS = nullptr;
    desc.HS = nullptr;
    desc.DS = nullptr;
    desc.GS = nullptr;
    desc.PS = nullptr;
    desc.inputLayout = nullptr;
    desc.bindingLayouts.resize( 0 );
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.patchControlPoints = 0;
    desc.renderState = nvrhi::RenderState{};
    desc.shadingRateState = nvrhi::VariableRateShadingState{};
}

static void vhResetGraphicsState( nvrhi::GraphicsState& state )
{
    state.pipeline = nullptr;
    state.framebuffer = nullptr;
    state.viewport.viewports.resize( 0 );
    state.viewport.scissorRects.resize( 0 );
    state.shadingRateState = nvrhi::VariableRateShadingState{};
    state.blendConstantColor = nvrhi::Color{};
    state.dynamicStencilRefValue = 0;
    state.bindings.resize( 0 );
    state.vertexBuffers.resize( 0 );
    state.indexBuffer = nvrhi::IndexBufferBinding{};
    state.indirectParams = nullptr;
}

static void vhResetComputePipelineDesc( nvrhi::ComputePipelineDesc& desc )
{
    desc.CS = nullptr;
    desc.bindingLayouts.resize( 0 );
}

static void vhResetComputeState( nvrhi::ComputeState& state )
{
    state.pipeline = nullptr;
    state.bindings.resize( 0 );
    state.indirectParams = nullptr;
}

// --------------------------------------------------------------------------
// Backend :: Utils & Helpers
// --------------------------------------------------------------------------

static const char* vhResourceTypeToString( nvrhi::ResourceType type )
{
    switch ( type )
    {
        case nvrhi::ResourceType::None: return "None";
        case nvrhi::ResourceType::Texture_SRV: return "Texture_SRV";
        case nvrhi::ResourceType::Texture_UAV: return "Texture_UAV";
        case nvrhi::ResourceType::TypedBuffer_SRV: return "TypedBuffer_SRV";
        case nvrhi::ResourceType::TypedBuffer_UAV: return "TypedBuffer_UAV";
        case nvrhi::ResourceType::StructuredBuffer_SRV: return "StructuredBuffer_SRV";
        case nvrhi::ResourceType::StructuredBuffer_UAV: return "StructuredBuffer_UAV";
        case nvrhi::ResourceType::RawBuffer_SRV: return "RawBuffer_SRV";
        case nvrhi::ResourceType::RawBuffer_UAV: return "RawBuffer_UAV";
        case nvrhi::ResourceType::ConstantBuffer: return "ConstantBuffer";
        case nvrhi::ResourceType::VolatileConstantBuffer: return "VolatileConstantBuffer";
        case nvrhi::ResourceType::Sampler: return "Sampler";
        case nvrhi::ResourceType::RayTracingAccelStruct: return "RayTracingAccelStruct";
        case nvrhi::ResourceType::PushConstants: return "PushConstants";
        case nvrhi::ResourceType::SamplerFeedbackTexture_UAV: return "SamplerFeedbackTexture_UAV";
        default: return "Invalid";
    }
}

// Check if buffer format is compatible with shader format for vertex attributes
static bool vhAreVertexFormatsCompatible( nvrhi::Format bufferFmt, nvrhi::Format shaderFmt )
{
    if ( bufferFmt == shaderFmt ) return true;

    const nvrhi::FormatInfo& bufInfo = nvrhi::getFormatInfo( bufferFmt );
    const nvrhi::FormatInfo& shdInfo = nvrhi::getFormatInfo( shaderFmt );

    if ( bufInfo.kind == shdInfo.kind ) return true;
    if ( bufInfo.kind == nvrhi::FormatKind::Normalized && shdInfo.kind == nvrhi::FormatKind::Float ) return true;

    return false;
}

int32_t vhCmdBackendState::BE_Util_ResolveBindingSlot( const char* name, nvrhi::ResourceType type, vhBackendShader& shader, bool debugLog )
{
    if ( !name || !name[0] )
        return -1;

    const size_t nameLen = strlen( name );
    const uint64_t nameHash = komihash( name, nameLen, 0 );
    const bool hasHashCache = shader.reflectionNameHashes.size() == shader.reflection.size();

    for ( size_t i = 0; i < shader.reflection.size(); ++i )
    {
        auto& resource = shader.reflection[i];
        if ( hasHashCache && shader.reflectionNameHashes[i] != nameHash )
            continue;
        if ( resource.name == name ) 
        {
            if ( resource.type != type )
            {
                if ( debugLog ) VRHI_LOG( "vhSetState(): WARNING: '%s' name found BUT under different type. Shader wants %s but vhState binds %s\n", name, vhResourceTypeToString( resource.type ), vhResourceTypeToString( type ) );
                continue;
            }
            return resource.slot;
        }
    }
    return -1;
}

bool vhCmdBackendState::BE_Util_ShaderStageMatches( uint64_t flags, bool useCompute, bool useGraphics, bool useRT )
{
    uint64_t stage = flags & VRHI_SHADER_STAGE_MASK;
    if ( ( stage == VRHI_SHADER_STAGE_COMPUTE ) && useCompute ) return true;
    if ( ( stage == VRHI_SHADER_STAGE_VERTEX ||
        stage == VRHI_SHADER_STAGE_PIXEL ||
        stage == VRHI_SHADER_STAGE_HULL ||
        stage == VRHI_SHADER_STAGE_DOMAIN ||
        stage == VRHI_SHADER_STAGE_GEOMETRY ||
        stage == VRHI_SHADER_STAGE_MESH ||
        stage == VRHI_SHADER_STAGE_AMPLIFICATION ) && useGraphics ) return true;
    if ( ( stage == VRHI_SHADER_STAGE_RAYGEN ||
        stage == VRHI_SHADER_STAGE_MISS ||
        stage == VRHI_SHADER_STAGE_CLOSEST_HIT ||
        stage == VRHI_SHADER_STAGE_ANY_HIT ||
        stage == VRHI_SHADER_STAGE_INTERSECTION ||
        stage == VRHI_SHADER_STAGE_CALLABLE ) && useRT ) return true;
    return false;
}

nvrhi::BindingLayoutVector vhCmdBackendState::BE_Util_BuildStageIndexedLayouts( vhBackendShader* const* shaders, int shaderCount, bool useCompute, bool useGraphics, bool useRT )
{
    int maxSet = 0;
    for ( int i = 0; i < shaderCount; ++i )
    {
        if ( !BE_Util_ShaderStageMatches( shaders[i]->flags, useCompute, useGraphics, useRT ) )
            continue;
        maxSet = std::max( maxSet, ( int ) vhGetDescriptorSetForStage( shaders[i]->flags ) );
    }

    nvrhi::BindingLayoutVector out;
    for ( int set = 0; set <= maxSet; ++set )
    {
        nvrhi::BindingLayoutHandle pick = m_emptyLayout;
        for ( int i = 0; i < shaderCount; ++i )
        {
            if ( !BE_Util_ShaderStageMatches( shaders[i]->flags, useCompute, useGraphics, useRT ) )
                continue;
            if ( ( int ) vhGetDescriptorSetForStage( shaders[i]->flags ) != set )
                continue;
            if ( shaders[i]->layout ) { pick = shaders[i]->layout; break; }
        }
        out.push_back( pick );
    }
    return out;
}

// Template helper removed - logic moved to vhTransientBuffer::Write

int64_t vhCmdBackendState::BE_Util_WriteGlobalUniform( const vhState& state, vhTransientBuffer& tbuf, uint64_t& lastHash )
{
    vhGlobalUniform u;
    vhWriteStateToGlobalUniform( state, u );
    uint64_t hash = vhHashGlobalUniform( u );

    if ( hash == lastHash && tbuf.offset >= sizeof( vhGlobalUniform ) )
        return tbuf.offset - sizeof( vhGlobalUniform );

    lastHash = hash;
    return tbuf.Write( &u, sizeof( u ) );
}

int64_t vhCmdBackendState::BE_Util_WriteWorldUniform( const vhState& state, vhTransientBuffer& tbuf, uint64_t& lastHash )
{
    vhWorldUniform u;
    vhWriteStateToWorldUniform( state, u );
    uint64_t hash = vhHashWorldUniform( u );

    if ( hash == lastHash && tbuf.offset >= sizeof( vhWorldUniform ) )
        return tbuf.offset - sizeof( vhWorldUniform );

    lastHash = hash;
    return tbuf.Write( &u, sizeof( u ) );
}

// --------------------------------------------------------------------------
// Backend :: Complex BE Low Level NVRHI Device Functions
// --------------------------------------------------------------------------

void vhCmdBackendState::BE_UpdateTexture( vhBackendTexture& btex, const vhMem* data, glm::ivec4 arrayMipUpdateRange )
{
    VRHI_PROFILE_FUNCTION();
    if ( !btex.handle || !data || !data->size() ) return;
    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );

    // Clamp to texture mip / array boundaries.
    int32_t mipStart = arrayMipUpdateRange.x, mipEnd = arrayMipUpdateRange.y;
    int32_t layerStart = arrayMipUpdateRange.z, layerEnd = arrayMipUpdateRange.w;
    mipStart = glm::clamp( mipStart, 0, btex.info.mipLevels );
    mipEnd = glm::clamp( mipEnd, 0, ( int32_t ) btex.info.mipLevels );
    layerStart = glm::clamp( layerStart, 0, btex.info.arrayLayers );
    layerEnd = glm::clamp( layerEnd, 0, ( int32_t ) btex.info.arrayLayers );
    assert( mipStart <= mipEnd );

    vhProfile( "BE_UpdateTexture_Calc", true );
    // Calculate layer size.
    int64_t totalLayerSize = 0;
    for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
    {
        totalLayerSize += btex.mipInfo[mip].size;
    }
    vhProfile( "BE_UpdateTexture_Calc", false );

    uint64_t expectedSize = uint64_t( layerEnd - layerStart ) * uint64_t( totalLayerSize );
    if ( data->size() < expectedSize )
    {
        VRHI_ERR( "BE_UpdateTexture: texture '%s' data size %llu < expected %llu\n", btex.name.c_str(), data->size(), expectedSize );
        return;
    }

    // Update the texture.
    {
        vhProfile( "BE_UpdateTexture_Write", true );
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );

        for ( int32_t layer = layerStart; layer < layerEnd; ++layer )
        {
            const uint8_t* layerSrcPtr = data->data() + ( size_t ) ( layer - layerStart ) * totalLayerSize;
            const auto& mipStartData = btex.mipInfo[mipStart];
            for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
            {
                if ( mip >= ( int32_t ) btex.mipInfo.size() )
                    break;

                const auto& mipData = btex.mipInfo[mip];
                const uint8_t* srcMipPtr = layerSrcPtr + ( mipData.offset - mipStartData.offset );

                cmdlist->writeTexture( btex.handle, layer, mip, srcMipPtr, mipData.pitch, mipData.slice_size );
            }
        }
        vhProfile( "BE_UpdateTexture_Write", false );
    }
}

void vhCmdBackendState::BE_BlitTexture( vhBackendTexture& bdst, vhBackendTexture& bsrc, int dstMip, int srcMip, int dstLayer, int srcLayer, glm::ivec3 dstOffset, glm::ivec3 srcOffset, glm::ivec3 extent )
{
    VRHI_PROFILE_FUNCTION();
    if ( !bdst.handle || !bsrc.handle ) return;

    // Higher level layers should already handle the validation.
    assert( srcMip >= 0 && srcMip < bsrc.info.mipLevels );
    assert( dstMip >= 0 && dstMip < bdst.info.mipLevels );
    assert( srcLayer >= 0 && srcLayer < bsrc.info.arrayLayers );
    assert( dstLayer >= 0 && dstLayer < bdst.info.arrayLayers );

    vhProfile( "BE_BlitTexture_SliceSetup", true );
    nvrhi::TextureSlice srcSlice;
    srcSlice.mipLevel = srcMip;
    srcSlice.arraySlice = srcLayer;
    srcSlice.x = srcOffset.x; srcSlice.y = srcOffset.y; srcSlice.z = srcOffset.z;
    srcSlice.width = extent.x; srcSlice.height = extent.y; srcSlice.depth = extent.z;

    nvrhi::TextureSlice dstSlice;
    dstSlice.mipLevel = dstMip;
    dstSlice.arraySlice = dstLayer;
    dstSlice.x = dstOffset.x; dstSlice.y = dstOffset.y; dstSlice.z = dstOffset.z;
    dstSlice.width = extent.x; dstSlice.height = extent.y; dstSlice.depth = extent.z;
    vhProfile( "BE_BlitTexture_SliceSetup", false );

    // Acquire command list and execute copy
    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        vhProfile( "BE_BlitTexture_Execute", true );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->copyTexture( bdst.handle, dstSlice, bsrc.handle, srcSlice );
        vhProfile( "BE_BlitTexture_Execute", false );
    }
}

void vhCmdBackendState::BE_ReadTextureSlow( vhBackendTexture& btex, vhMem* outData, int mip, int layer )
{
    VRHI_PROFILE_FUNCTION();
    if ( !btex.handle || !outData ) return;
    assert( btex.info.target != nvrhi::TextureDimension::Texture3D );
    if ( mip < 0 || mip >= ( int ) btex.mipInfo.size() )
    {
        VRHI_ERR( "vhReadTextureSlow: mip %d out of range (0..%d)\n", mip, ( int ) btex.mipInfo.size() - 1 );
        return;
    }

    // Staging Texture
    auto desc = btex.handle->getDesc();
    desc.isVirtual = false;
    desc.isRenderTarget = false;
    desc.isUAV = false;
    desc.keepInitialState = true;
    desc.initialState = nvrhi::ResourceStates::CopyDest;
    desc.debugName = "BE_ReadTextureSlow Staging Texture";

    nvrhi::StagingTextureHandle stagingTex;
    {
        vhProfile( "BE_ReadTextureSlow_StagingCreate", true );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        stagingTex = g_vhDevice->createStagingTexture( desc, nvrhi::CpuAccessMode::Read );
        vhProfile( "BE_ReadTextureSlow_StagingCreate", false );
    }

    if ( !stagingTex ) return;

    nvrhi::TextureSlice slice;
    slice.mipLevel = mip;
    slice.arraySlice = layer;

    // For this slow-path operation, just use Graphics queue for everything
    // (avoids complexity with transfer queue barriers)
    {
        vhProfile( "BE_ReadTextureSlow_Copy", true );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );

        nvrhi::CommandListParameters params = { .queueType = nvrhi::CommandQueue::Graphics };
        auto cmdList = g_vhDevice->createCommandList( params );
        cmdList->open();
        cmdList->copyTexture( stagingTex, slice, btex.handle, slice );
        cmdList->close();
        g_vhDevice->executeCommandList( cmdList, nvrhi::CommandQueue::Graphics );
        g_vhDevice->waitForIdle();
        vhProfile( "BE_ReadTextureSlow_Copy", false );
    }

    // CPU copy
    void* pData = nullptr;
    size_t rowPitch = 0;

    {
        vhProfile( "BE_ReadTextureSlow_Map", true );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        pData = g_vhDevice->mapStagingTexture( stagingTex, slice, nvrhi::CpuAccessMode::Read, &rowPitch );
        vhProfile( "BE_ReadTextureSlow_Map", false );
    }

    if ( pData )
    {
        const auto& mipInfo = btex.mipInfo[mip];
        int height = mipInfo.dimensions.y;
        int expectedPitch = mipInfo.pitch;

        if ( outData->size() < ( size_t ) mipInfo.slice_size )
        {
            outData->resize( mipInfo.slice_size );
        }

        uint8_t* src = ( uint8_t* ) pData;
        uint8_t* dst = outData->data();

        vhProfile( "BE_ReadTextureSlow_CopyCPU", true );
        for ( int y = 0; y < height; ++y )
        {
            memcpy( dst + ( size_t ) y * expectedPitch, src + ( size_t ) y * rowPitch, expectedPitch );
        }
        vhProfile( "BE_ReadTextureSlow_CopyCPU", false );

        {
            vhProfile( "BE_ReadTextureSlow_Unmap", true );
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            g_vhDevice->unmapStagingTexture( stagingTex );
            vhProfile( "BE_ReadTextureSlow_Unmap", false );
        }
    }
}

void vhCmdBackendState::BE_ResizeBuffer( vhBackendBuffer& bbuf, uint64_t size )
{
    VRHI_PROFILE_FUNCTION();
    if ( !bbuf.handle ) return;

    auto oldHandle = bbuf.handle;
    auto oldSize = bbuf.desc.byteSize;

    bbuf.desc.setByteSize( size );
    {
        vhProfile( "BE_ResizeBuffer_Create", true );
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        bbuf.handle = g_vhDevice->createBuffer( bbuf.desc );
        vhProfile( "BE_ResizeBuffer_Create", false );
    }

    if ( !bbuf.handle )
    {
        VRHI_ERR( "vhCreateVertexBuffer() : Failed to create bhandle!\n" );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    vhProfile( "BE_ResizeBuffer_Copy", true );
    cmdlist->copyBuffer( bbuf.handle, 0, oldHandle, 0, glm::min( bbuf.desc.byteSize, oldSize ) );
    vhProfile( "BE_ResizeBuffer_Copy", false );
}

void vhCmdBackendState::BE_UpdateBuffer( vhBackendBuffer& bbuf, uint64_t offset, const vhMem* data )
{
    VRHI_PROFILE_FUNCTION();
    if ( !bbuf.handle || !data || !data->size() ) return;

    if ( offset + data->size() > bbuf.desc.byteSize )
    {
        vhProfile( "BE_UpdateBuffer_ResizeCheck", true );
        assert( bbuf.flags & VRHI_BUFFER_ALLOW_RESIZE );
        BE_ResizeBuffer( bbuf, offset + data->size() );
        vhProfile( "BE_UpdateBuffer_ResizeCheck", false );
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        vhProfile( "BE_UpdateBuffer_Write", true );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->writeBuffer( bbuf.handle, data->data(), data->size(), offset );
        vhProfile( "BE_UpdateBuffer_Write", false );
    }
}

nvrhi::FramebufferHandle vhCmdBackendState::BE_GetFrameBuffer( const std::vector< vhState::RenderTarget >& colourAttachment, const vhState::RenderTarget& depthAttachment, vhTexture shadingRateImage )
{
    // Reuse the descriptor storage across calls and only reset the mutable fields.
    static nvrhi::FramebufferDesc desc;
    desc.colorAttachments.resize( 0 );
    desc.depthAttachment = nvrhi::FramebufferAttachment{};
    desc.shadingRateAttachment = nvrhi::FramebufferAttachment{};

    for ( const auto& rt : colourAttachment )
    {
        if ( rt.texture == VRHI_INVALID_HANDLE ) continue;
        auto it = backendTextures.find( rt.texture );
        if ( it != backendTextures.end() && it->second->handle )
        {
            nvrhi::FramebufferAttachment att;
            att.setTexture( it->second->handle );
            att.setArraySlice( rt.arrayLayer );
            att.setMipLevel( rt.mipLevel );
            att.setFormat( rt.formatOverride );
            att.setReadOnly( rt.readOnly );
            desc.addColorAttachment( att );
        }
    }

    if ( depthAttachment.texture != VRHI_INVALID_HANDLE )
    {
        auto it = backendTextures.find( depthAttachment.texture );
        if ( it != backendTextures.end() && it->second->handle )
        {
            nvrhi::FramebufferAttachment att;
            att.setTexture( it->second->handle );
            att.setArraySlice( depthAttachment.arrayLayer );
            att.setMipLevel( depthAttachment.mipLevel );
            att.setFormat( depthAttachment.formatOverride );
            att.setReadOnly( depthAttachment.readOnly );
            desc.setDepthAttachment( att );
        }
    }

    if ( shadingRateImage != VRHI_INVALID_HANDLE )
    {
        auto it = backendTextures.find( shadingRateImage );
        if ( it != backendTextures.end() && it->second->handle )
            desc.setShadingRateAttachment( it->second->handle );
    }

    if ( desc.colorAttachments.empty() && !desc.depthAttachment.valid() )
    {
        VRHI_ERR( "vhSetState(): No attachments specified for frame buffer.\n" );
        return nullptr;
    }

    return vhFBOCacheGet( desc );
}

bool vhCmdBackendState::BE_PresubmitCommon_PipelineDesc(
    vhState& state,
    vhBackendShader* const* shaders,
    int shaderCount,
    nvrhi::ComputePipelineDesc* computePipelineDesc, // set to nullptr if not using compute.
    nvrhi::GraphicsPipelineDesc* graphicsPipelineDesc // set to nullptr if not using graphics.
)
{
    assert( shaders && shaderCount > 0 );
    const vhBackendShader* vertexShader = nullptr;

    const bool useCompute = computePipelineDesc != nullptr;
    const bool useGraphics = graphicsPipelineDesc != nullptr;

    auto layouts = BE_Util_BuildStageIndexedLayouts( shaders, shaderCount, useCompute, useGraphics, false );
    for ( auto& layout : layouts )
    {
        if ( computePipelineDesc ) computePipelineDesc->addBindingLayout( layout );
        if ( graphicsPipelineDesc ) graphicsPipelineDesc->addBindingLayout( layout );
    }

    vhProfile( "BE_PresubmitCommon_PipelineDesc_ShaderHandles", true );
    // Set Shader Handles
    for ( int shaderIdx = 0; shaderIdx < shaderCount; ++shaderIdx )
    {
        auto& shader = *shaders[shaderIdx];
        if ( !BE_Util_ShaderStageMatches( shader.flags, computePipelineDesc != nullptr, graphicsPipelineDesc != nullptr ) )
            continue;

        const uint32_t stage = ( uint32_t ) ( shader.flags & VRHI_SHADER_STAGE_MASK );
        if ( stage == VRHI_SHADER_STAGE_COMPUTE && computePipelineDesc )
        {
            computePipelineDesc->setComputeShader( shader.handle );
        }
        else if ( stage == VRHI_SHADER_STAGE_VERTEX && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setVertexShader( shader.handle );
            vertexShader = shaders[shaderIdx];
        }
        else if ( stage == VRHI_SHADER_STAGE_HULL && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setHullShader( shader.handle );
        }
        else if ( stage == VRHI_SHADER_STAGE_DOMAIN && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setDomainShader( shader.handle );
        }
        else if ( stage == VRHI_SHADER_STAGE_GEOMETRY && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setGeometryShader( shader.handle );
        }
        else if ( stage == VRHI_SHADER_STAGE_PIXEL && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setPixelShader( shader.handle );
        }
    }
    vhProfile( "BE_PresubmitCommon_PipelineDesc_ShaderHandles", false );

    if ( graphicsPipelineDesc )
    {
        vhProfile( "BE_PresubmitCommon_PipelineDesc_RenderState", true );
        graphicsPipelineDesc->setPrimType( vhTranslatePrimitiveType( state.stateFlags ) );
        graphicsPipelineDesc->renderState.blendState = vhTranslateBlendState( state.stateFlags );
        graphicsPipelineDesc->renderState.depthStencilState = vhTranslateDepthStencilState( state.stateFlags, state.stencilState );
        graphicsPipelineDesc->renderState.rasterState = vhTranslateRasterState( state.stateFlags );
        graphicsPipelineDesc->renderState.rasterState.scissorEnable = ( state.viewScissor.z >= 0.0f && state.viewScissor.w >= 0.0f );
        graphicsPipelineDesc->renderState.rasterState.depthBias = state.depthBias;
        graphicsPipelineDesc->renderState.rasterState.depthBiasClamp = state.depthBiasClamp;
        graphicsPipelineDesc->renderState.rasterState.slopeScaledDepthBias = state.slopeScaledDepthBias;
        vhProfile( "BE_PresubmitCommon_PipelineDesc_RenderState", false );

        // [TODO] The following fields are not currently populated from vhState:
        // - patchControlPoints: tessellation is only supported if we add it.

        vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", true );
        // Reuse pool for parsed vertex layouts. Inner vectors are cleared but not destroyed,
        // preserving their internal buffer capacity across calls.
        static std::vector< std::vector< vhVertexLayoutDef > > s_parsedLayouts;
        static size_t s_parsedLayoutsUsed = 0;

        // Flat table keyed by location index for fast O(1) collision detection and lookup.
        // Vertex attribute locations are small non-negative integers, capped at 32 in practice.
        constexpr int k_maxVertexLocations = 32;
        static const vhVertexLayoutDef* s_locationTable[ k_maxVertexLocations ];
        static int s_locationTableUsed[ k_maxVertexLocations ];
        static int s_locationTableUsedCount = 0;
        uint64_t attrHash = 0;

        // Reset only the slots that were used last frame, avoiding a full memset each call.
        for ( int _i = 0; _i < s_locationTableUsedCount; ++_i )
            s_locationTable[ s_locationTableUsed[_i] ] = nullptr;
        s_locationTableUsedCount = 0;

        s_attributes.clear();
        s_parsedLayoutsUsed = 0;

        for ( size_t i = 0; i < state.vertexBindings.size(); ++i )
        {
            const auto& binding = state.vertexBindings[i];
            if ( binding.buffer == VRHI_INVALID_HANDLE )
                continue;

            auto it = backendBuffers.find( binding.buffer );
            if ( it == backendBuffers.end() )
                continue;

            const auto& bbuf = *it->second;
            const std::vector< vhVertexLayoutDef >* pLayout = &bbuf.layout;
            uint32_t stride = bbuf.stride;

            if ( !binding.layoutOverride.empty() )
            {
                // Reuse pool pattern: grow only if needed, otherwise reuse existing slot.
                if ( s_parsedLayoutsUsed >= s_parsedLayouts.size() )
                    s_parsedLayouts.emplace_back();
                auto& slot = s_parsedLayouts[s_parsedLayoutsUsed++];
                slot.clear();
                if ( !vhParseVertexLayoutInternal( binding.layoutOverride, slot ) )
                {
                    VRHI_ERR( "Failed to parse vertex layout override: %s\n", binding.layoutOverride.c_str() );
                    vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
                    return false;
                }
                pLayout = &slot;
                stride = 0;
                for ( size_t defIdx = 0; defIdx < pLayout->size(); ++defIdx )
                    stride += vhVertexLayoutDefSize( ( *pLayout )[ defIdx ] );
            }

            for ( size_t defIdx = 0; defIdx < pLayout->size(); ++defIdx )
            {
                const auto& def = ( *pLayout )[ defIdx ];
                if ( def.location < 0 || def.location >= k_maxVertexLocations )
                {
                    VRHI_ERR( "Vertex Attribute Location %d out of supported range [0, %d).\n", def.location, k_maxVertexLocations );
                    vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
                    return false;
                }
                if ( s_locationTable[ def.location ] != nullptr )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH ) VRHI_ERR( "Vertex Attribute Collision: Location %d already bound by previous buffer\n", def.location );
                    vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
                    return false;
                }
                s_locationTable[ def.location ] = &def;
                s_locationTableUsed[ s_locationTableUsedCount++ ] = def.location;
                nvrhi::VertexAttributeDesc attr = vhTranslateVertexAttribute( def, ( uint32_t ) i );
                attr.elementStride = stride;

                // Validate instancing consistency between layout string and VertexBinding
                bool bindingInstanced = binding.isInstanced;
                if ( attr.isInstanced != bindingInstanced )
                {
                    VRHI_ERR( "Vertex Attribute Instancing Mismatch at Location %d: Layout %s, Binding %s. Must match.\n", def.location, attr.isInstanced ? "instanced" : "per-vertex", bindingInstanced ? "instanced" : "per-vertex" );
                    vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
                    return false;
                }

                s_attributes.push_back( attr );
                attrHash = vhHashVertexAttributeDesc( def.location, attr, attrHash );
            }
        }

        // nvrhi's createInputLayout assigns Vulkan locations sequentially (0,1,2,...) by array index.
        // If the bound attributes use sparse locations (e.g. 0-5, 8-11), expand s_attributes into a
        // dense array of size maxLocation+1 so that array index equals Vulkan location.
        {
            int maxLocation = 0;
            for ( int i = 0; i < s_locationTableUsedCount; ++i )
                maxLocation = std::max( maxLocation, s_locationTableUsed[i] );

            if ( !s_attributes.empty() && ( int ) s_attributes.size() < maxLocation + 1 )
            {
                static std::vector< nvrhi::VertexAttributeDesc > s_denseAttributes;
                s_denseAttributes.assign( maxLocation + 1, {} );

                for ( int i = 0; i < s_locationTableUsedCount; ++i )
                    s_denseAttributes[ s_locationTableUsed[i] ] = s_attributes[i];

                const uint32_t binding0Stride = s_attributes[0].elementStride;
                for ( int loc = 0; loc <= maxLocation; ++loc )
                {
                    if ( s_locationTable[loc] )
                        continue;
                    auto& pad         = s_denseAttributes[loc];
                    pad.name          = "PAD";
                    pad.format        = nvrhi::Format::R8_UINT;
                    pad.elementStride = binding0Stride;
                }

                s_attributes.swap( s_denseAttributes );

                attrHash = 0;
                for ( int loc = 0; loc <= maxLocation; ++loc )
                    attrHash = vhHashVertexAttributeDesc( loc, s_attributes[loc], attrHash );
            }
        }

        // Strict validation of shader inputs to ensure every attribute is satisfied by a bound buffer.
        if ( vertexShader )
        {
            for ( size_t attribIdx = 0; attribIdx < vertexShader->inputLayout.size(); ++attribIdx )
            {
                const auto& vsAttribDef = vertexShader->inputLayout[ attribIdx ];
                const vhVertexLayoutDef* bound = ( vsAttribDef.location >= 0 && vsAttribDef.location < k_maxVertexLocations )
                    ? s_locationTable[ vsAttribDef.location ] : nullptr;
                if ( !bound )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH ) VRHI_ERR( "Vertex Attribute Missing: Shader expects Location %d, but no bound buffer provides it.\n", vsAttribDef.location );
                    vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
                    return false;
                }
                if ( !vhAreVertexFormatsCompatible( bound->format, vsAttribDef.format ) )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH ) VRHI_ERR( "Vertex Attribute Format Incompatible at Location %d (Buffer: %d (%s), Shader: %d (%s))\n", vsAttribDef.location, ( int ) bound->format, nvrhi::getFormatInfo( bound->format ).name, ( int ) vsAttribDef.format, nvrhi::getFormatInfo( vsAttribDef.format ).name );
                    vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
                    return false;
                }
            }

            if ( !s_attributes.empty() )
            {
                // Hash the attribute array to dedup InputLayout objects. Creating a new InputLayout
                // on every draw call causes unnecessary driver allocations even when the layout is
                // identical to a previous frame's.
                static std::unordered_map< uint64_t, nvrhi::InputLayoutHandle > s_inputLayoutCache;
                auto cacheIt = s_inputLayoutCache.find( attrHash );
                if ( cacheIt != s_inputLayoutCache.end() )
                {
                    graphicsPipelineDesc->inputLayout = cacheIt->second;
                }
                else
                {
                    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
                    nvrhi::InputLayoutHandle layout = g_vhDevice->createInputLayout( s_attributes.data(), ( uint32_t ) s_attributes.size(), vertexShader->handle );
                    s_inputLayoutCache[ attrHash ] = layout;
                    graphicsPipelineDesc->inputLayout = layout;
                }
            }
        }
        vhProfile( "BE_PresubmitCommon_PipelineDesc_VertexLayout", false );
    }

    return true;
}

void vhCmdBackendState::BE_PreSubmitCommon_ResolveStateCache(
    const vhState& state,
    vhBackendShader* const* shaders,
    int shaderCount,
    vhStateResolveCache& scache
)
{
    assert( !scache.init );

    // Build the backend pointer caches.
    scache.btex.resize( state.textures.size(), nullptr );
    scache.bbuf.resize( state.buffers.size(), nullptr );

    // Resolve backend pointers.

    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_Textures", true );
    for ( size_t i = 0; i < state.textures.size(); i++ )
    {
        if ( state.textures[i].texture == VRHI_INVALID_HANDLE )
            continue;
        const auto& it = backendTextures.find( state.textures[i].texture );
        if ( it != backendTextures.end() )
            scache.btex[i] = it->second.get();
    }
    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_Textures", false );

    // Resolve shaders.
    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_Shaders", true );
    scache.bshaders.clear();
    if ( shaders && shaderCount > 0 )
    {
        for ( int i = 0; i < shaderCount; ++i )
            scache.bshaders.push_back( shaders[i] );
    }
    else if ( !state.program.empty() )
    {
        for ( vhShader h : state.program )
        {
            auto it = backendShaders.find( h );
            if ( it != backendShaders.end() )
                scache.bshaders.push_back( it->second.get() );
        }
    }
    int bshaderCount = ( int ) scache.bshaders.size();
    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_Shaders", false );

    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_Buffers", true );
    for ( size_t i = 0; i < state.buffers.size(); i++ )
    {
        if ( state.buffers[i].buffer == VRHI_INVALID_HANDLE )
            continue;
        const auto& it = backendBuffers.find( state.buffers[i].buffer );
        if ( it != backendBuffers.end() )
            scache.bbuf[i] = it->second.get();
    }
    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_Buffers", false );

    // Resolve acceleration structures.
    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_AccelStructs", true );
    scache.baccel.resize( state.accelStructs.size(), nullptr );
    for ( size_t i = 0; i < state.accelStructs.size(); i++ )
    {
        if ( state.accelStructs[i].as == VRHI_INVALID_HANDLE )
            continue;
        const auto& it = backendAccelStructs.find( state.accelStructs[i].as );
        if ( it != backendAccelStructs.end() )
            scache.baccel[i] = it->second.get();
    }
    vhProfile( "BE_PreSubmitCommon_ResolveStateCache_AccelStructs", false );

    // Build slot maps. Initialise stage binding storage.
    // Pre-reserve vectors on first-ever use (check capacity to avoid re-reserving every frame).
    for ( int i = 1; i <= VRHI_SHADER_STAGE_MAX; i++ )
    {
        if ( !scache.stageBindingActive[i] )
        {
            auto& slot = scache.stageBindingStorage[i];
            if ( slot.samplerTable.capacity() == 0 )
            {
                slot.samplerTable.reserve( vhStateResolveCache::ShaderStageBindingSlotState::MAX_SAMPLERS );
                slot.textureTable.reserve( vhStateResolveCache::ShaderStageBindingSlotState::MAX_TEXTURES );
                slot.bufferTable.reserve( vhStateResolveCache::ShaderStageBindingSlotState::MAX_BUFFERS );
                slot.uavTable.reserve( vhStateResolveCache::ShaderStageBindingSlotState::MAX_UAVS );
                slot.accelStructTable.reserve( 16 );
            }
        }
        scache.stageBindingActive[i] = true;
    }

    auto fnResolveSlot = [&]( const char* name, int32_t fallbackSlot, nvrhi::ResourceType type, vhBackendShader& shader ) -> int32_t
    {
        if ( name && name[0] )
        {
            // Reflected slots are already shifted by vhCompileShader / Slang.
            return BE_Util_ResolveBindingSlot( name, type, shader, !!( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) );
        }

        switch ( type )
        {
            case nvrhi::ResourceType::Sampler:
                return fallbackSlot + ( int32_t ) g_vhInit.shaderMake_sRegShift;
            case nvrhi::ResourceType::Texture_SRV:
            case nvrhi::ResourceType::TypedBuffer_SRV:
            case nvrhi::ResourceType::StructuredBuffer_SRV:
            case nvrhi::ResourceType::RawBuffer_SRV:
            case nvrhi::ResourceType::RayTracingAccelStruct:
                return fallbackSlot + ( int32_t ) g_vhInit.shaderMake_tRegShift;
            case nvrhi::ResourceType::ConstantBuffer:
            case nvrhi::ResourceType::VolatileConstantBuffer:
                return fallbackSlot + ( int32_t ) g_vhInit.shaderMake_bRegShift;
            case nvrhi::ResourceType::Texture_UAV:
            case nvrhi::ResourceType::TypedBuffer_UAV:
            case nvrhi::ResourceType::StructuredBuffer_UAV:
            case nvrhi::ResourceType::RawBuffer_UAV:
                return fallbackSlot + ( int32_t ) g_vhInit.shaderMake_uRegShift;
            default:
                return fallbackSlot;
        }
    };

    for ( size_t i = 0; i < state.samplers.size(); i++ )
    {
        const auto& s = state.samplers[i];
        for ( int j = 0; j < shaderCount; j++ )
        {
            const int32_t slot = fnResolveSlot( s.name, s.slot, nvrhi::ResourceType::Sampler, *shaders[j] );
            if ( slot < 0 )
                continue; // Having extra resources bound that the shader doesn't use is fair dinkum.

            const uint32_t stage = ( uint32_t ) ( shaders[j]->flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            assert( scache.stageBindingActive[stage] );
            auto& stageTable = scache.stageBindingStorage[stage];
            if ( slot < stageTable.samplerTable.size() && stageTable.samplerTable[slot] )
            {
                // Duplicate binding slots is not fair dinkum.
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Sampler Binding Slot Collision: Slot %d already bound by previous shader\n", slot );
                return;
            }

            auto shandle = vhGetSamplerHandle( s.flags );
            if ( !shandle )
            {
                VRHI_ERR( "vhSetState(): Failed to get sampler handle for sampler at index %zu\n", i );
                return;
            }
            if ( ( uint32_t ) slot < g_vhInit.shaderMake_sRegShift )
            {
                VRHI_ERR( "vhSetState(): Sampler slot %d is not shifted by sRegShift (%u) for '%s'\n", slot, g_vhInit.shaderMake_sRegShift, s.name ? s.name : "" );
                return;
            }
            if ( slot >= stageTable.samplerTable.size() )
                stageTable.samplerTable.resize( slot + 1, nullptr );
            stageTable.samplerTable[slot] = shandle.Get();
            //TEMP_PRINT
            //printf( "DEBUG: Store sampler slot=%u\n", slot );
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Sampler 0x%llx bound to slot %d '%s'\n", s.flags, slot, s.name ? s.name : "" );
        }
    }

    for ( size_t i = 0; i < state.textures.size(); i++ )
    {
        if ( !scache.btex[i] ) continue;
        auto& btex = *scache.btex[i];

        const auto& t = state.textures[i];
        for ( int j = 0; j < shaderCount; j++ )
        {
            const nvrhi::ResourceType bindingType = t.computeUAV ? nvrhi::ResourceType::Texture_UAV : nvrhi::ResourceType::Texture_SRV;
            const int32_t slot = fnResolveSlot( t.name, t.slot, bindingType, *shaders[j] );
            if ( slot < 0 )
                continue; // Having extra resources bound that the shader doesn't use is fair dinkum.

            const uint32_t stage = ( uint32_t ) ( shaders[j]->flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            auto& stageTable = scache.stageBindingStorage[stage];

            if ( t.computeUAV )
            {
                if ( ( uint32_t ) slot < g_vhInit.shaderMake_uRegShift )
                {
                    VRHI_ERR( "vhSetState(): Texture UAV slot %d is not shifted by uRegShift (%u) for '%s'\n", slot, g_vhInit.shaderMake_uRegShift, t.name ? t.name : "" );
                    return;
                }
                if ( slot >= stageTable.uavTable.size() )
                    stageTable.uavTable.resize( slot + 1 );
                auto& uavEntry = stageTable.uavTable[slot];
                if ( uavEntry.first.handle || uavEntry.second.handle )
                {
                    // If either part of the pair is already filled, that's a collision.
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Texture UAV Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                uavEntry.first = { btex.handle.Get(), &t };
                //TEMP_PRINT
                //printf( "DEBUG: Store texture UAV slot=%u\n", slot );
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Texture UAV '%s' bound to slot %d '%s'\n", btex.name.c_str(), slot, t.name ? t.name : "" );
            }
            else
            {
                if ( slot >= stageTable.textureTable.size() )
                    stageTable.textureTable.resize( slot + 1, { nullptr, nullptr } );
                if ( stageTable.textureTable[slot].handle || stageTable.textureTable[slot].binding )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Texture Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                stageTable.textureTable[slot] = { btex.handle.Get(), &t };
                //TEMP_PRINT
                //printf( "DEBUG: Store texture SRV slot=%u\n", slot );
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Texture SRV '%s' bound to slot %d '%s'\n", btex.name.c_str(), slot, t.name ? t.name : "" );
            }
        }
    }

    for ( size_t i = 0; i < state.buffers.size(); i++ )
    {
        if ( !scache.bbuf[i] ) continue;
        auto& bbuf = *scache.bbuf[i];

        const auto& b = state.buffers[i];
        for ( int j = 0; j < shaderCount; j++ )
        {
            nvrhi::ResourceType bindingType = nvrhi::ResourceType::None;
            if ( bbuf.desc.isConstantBuffer )
            {
                bindingType = nvrhi::ResourceType::ConstantBuffer;
            }
            else if ( bbuf.desc.format != nvrhi::Format::UNKNOWN )
            {
                bindingType = b.computeUAV ? nvrhi::ResourceType::TypedBuffer_UAV : nvrhi::ResourceType::TypedBuffer_SRV;
            }
            else if ( bbuf.desc.structStride > 0 )
            {
                bindingType = b.computeUAV ? nvrhi::ResourceType::StructuredBuffer_UAV : nvrhi::ResourceType::StructuredBuffer_SRV;
            }
            else
            {
                bindingType = b.computeUAV ? nvrhi::ResourceType::RawBuffer_UAV : nvrhi::ResourceType::RawBuffer_SRV;
            }
            const int32_t slot = fnResolveSlot( b.name, b.slot, bindingType, *shaders[j] );
            if ( slot < 0 )
                continue; // Having extra resources bound that the shader doesn't use is fair dinkum.

            const uint32_t stage = ( uint32_t ) ( shaders[j]->flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            auto& stageTable = scache.stageBindingStorage[stage];

            if ( b.computeUAV )
            {
                if ( ( uint32_t ) slot < g_vhInit.shaderMake_uRegShift )
                {
                    VRHI_ERR( "vhSetState(): Buffer UAV slot %d is not shifted by uRegShift (%u) for '%s'\n", slot, g_vhInit.shaderMake_uRegShift, b.name ? b.name : "" );
                    return;
                }
                if ( slot >= stageTable.uavTable.size() )
                    stageTable.uavTable.resize( slot + 1 );
                auto& uavEntry = stageTable.uavTable[slot];
                if ( uavEntry.first.handle || uavEntry.second.handle )
                {
                    // If either part of the pair is already filled, that's a collision.
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Buffer UAV Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                uavEntry.second = { bbuf.handle.Get(), &b };
                //TEMP_PRINT
                //printf( "DEBUG: Store buffer UAV slot=%u\n", slot );
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Buffer UAV '%s' bound to slot %d '%s'\n", bbuf.name.c_str(), slot, b.name ? b.name : "" );
            }
            else
            {
                if ( slot >= stageTable.bufferTable.size() )
                    stageTable.bufferTable.resize( slot + 1, { nullptr, nullptr } );
                if ( stageTable.bufferTable[slot].handle || stageTable.bufferTable[slot].binding )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Buffer Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                stageTable.bufferTable[slot] = { bbuf.handle.Get(), &b };
                //TEMP_PRINT
                //printf( "DEBUG: Store buf slot=%u isCB=%d\n", slot, bbuf.desc.isConstantBuffer );
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Buffer SRV '%s' bound to slot %d '%s'\n", bbuf.name.c_str(), slot, b.name ? b.name : ""  );
            }
        }
    }

    for ( size_t i = 0; i < state.accelStructs.size(); i++ )
    {
        if ( !scache.baccel[i] ) continue;

        const auto& a = state.accelStructs[i];
        for ( int j = 0; j < shaderCount; j++ )
        {
            const int32_t slot = fnResolveSlot( a.name, a.slot, nvrhi::ResourceType::RayTracingAccelStruct, *shaders[j] );
            if ( slot < 0 )
                continue;

            const uint32_t stage = ( uint32_t ) ( shaders[j]->flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            auto& stageTable = scache.stageBindingStorage[stage];

            if ( slot >= ( int32_t ) stageTable.accelStructTable.size() )
                stageTable.accelStructTable.resize( slot + 1, { nullptr, nullptr } );
            if ( stageTable.accelStructTable[slot].handle )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "AccelStruct Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                return;
            }
            stageTable.accelStructTable[slot] = { scache.baccel[i]->handle.Get(), &a };
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): AccelStruct bound to slot %d '%s'\n", slot, a.name ? a.name : "" );
        }
    }

    // Resolve User Global Uniforms

    for ( int j = 0; j < shaderCount; j++ )
    {
        const uint32_t stage = ( uint32_t ) ( shaders[j]->flags & VRHI_SHADER_STAGE_MASK );
        assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
        auto& stageTable = scache.stageBindingStorage[stage];

        if ( stageTable.userGlobalsSlot != UINT32_MAX )
        {
            VRHI_ERR( "ResolveCache: Duplicate shader stage %u detected in pipeline!\n", stage );
            assert( !"Duplicate shader stage detected in pipeline" );
            continue;
        }

        for ( size_t reflectionIdx = 0; reflectionIdx < shaders[j]->reflection.size(); ++reflectionIdx )
        {
            const auto& res = shaders[j]->reflection[ reflectionIdx ];
            if ( res.type == nvrhi::ResourceType::ConstantBuffer )
            {
                if ( res.name == "$Globals" || res.name == "_Globals" || res.name == "globalParams" )
                {
                    stageTable.userGlobalsSlot = res.slot;
                    stageTable.userGlobalsHash = res.membersHash;
                    stageTable.userGlobalsReflection = &res;
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS )
                    {
                        VRHI_LOG( "ResolveCache: Found User Globals '%s' at slot %u for stage %u. Hash: 0x%llx\n",
                            res.name.c_str(), res.slot, stage, res.membersHash );
                    }
                }
                else if ( res.name == "GlobalUniforms" )
                {
                    stageTable.globalUniformsSlot = res.slot;
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS )
                    {
                        VRHI_LOG( "ResolveCache: Found GlobalUniforms at slot %u for stage %u\n", res.slot, stage );
                    }
                }
                else if ( res.name == "WorldUniforms" )
                {
                    stageTable.worldUniformsSlot = res.slot;
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS )
                    {
                        VRHI_LOG( "ResolveCache: Found WorldUniforms at slot %u for stage %u\n", res.slot, stage );
                    }
                }
            }
        }
    }

    // Static buffer for packing user globals. Resizes only upward, preserving capacity across frames.
    static std::vector< uint8_t > s_userGlobalsPackBuffer;

    for ( int stageIdx = 1; stageIdx <= VRHI_SHADER_STAGE_MAX; stageIdx++ )
    {
        if ( !scache.stageBindingActive[stageIdx] )
            continue;

        const auto& stageTable = scache.stageBindingStorage[stageIdx];

        if ( stageTable.userGlobalsSlot == UINT32_MAX )
            continue;

        uint64_t hash = stageTable.userGlobalsHash;

        if ( scache.userGlobalUniformsBufferCache.find( hash ) != scache.userGlobalUniformsBufferCache.end() )
            continue;

        const vhShaderReflectionResource* res = stageTable.userGlobalsReflection;

        if ( !res || res->sizeInBytes == 0 )
            continue;

        uint32_t packSize = res->sizeInBytes;
        uint32_t alignedSize = ( uint32_t ) VRHI_ROUND_UP( packSize, VRHI_CBUF_ALIGN );
        if ( s_userGlobalsPackBuffer.size() < alignedSize )
            s_userGlobalsPackBuffer.resize( alignedSize, 0 );
        vhPackUserGlobals( state.uniforms, res->members, s_userGlobalsPackBuffer.data(), packSize );

        int64_t offset = m_userUniformBuffer.Write( s_userGlobalsPackBuffer.data(), alignedSize );
        if ( offset >= 0 )
        {
            vhStateResolveCache::UserGlobalUniformsBufferInfo info;
            info.buffer = m_userUniformBuffer.handle[m_userUniformBuffer.frameIdx];
            info.range = nvrhi::BufferRange( offset, alignedSize );
            scache.userGlobalUniformsBufferCache[hash] = info;

            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS )
                VRHI_LOG( "ResolveCache: Eagerly allocated User Globals buffer for hash 0x%llx\n", hash );
        }
        else
        {
            VRHI_ERR( "ResolveCache: Failed to write User Global Uniforms to transient buffer (Out of space)\n" );
        }
    }

    scache.init = true;
}

bool vhCmdBackendState::BE_PreSubmitCommon_FindResource(
    const vhState& state,
    const uint32_t stage,
    const vhStateResolveCache& scache,
    const nvrhi::BindingLayoutItem& item,
    nvrhi::BindingSetItem& outItem,
    const char* name
)
{
    assert( scache.init );
    assert( scache.btex.size() == state.textures.size() );
    assert( scache.bbuf.size() == state.buffers.size() );

    if ( stage == 0 || stage > VRHI_SHADER_STAGE_MAX || !scache.stageBindingActive[stage] )
    {
        if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Stage %d not found in cache.\n", stage );
        return false;
    }
    const auto& stageTable = scache.stageBindingStorage[stage];

    switch ( item.type )
    {
        case nvrhi::ResourceType::ConstantBuffer:
        case nvrhi::ResourceType::VolatileConstantBuffer:
        {
            if ( stageTable.globalUniformsSlot != UINT32_MAX && item.slot == stageTable.globalUniformsSlot )
            {
                int64_t offset = BE_Util_WriteGlobalUniform( state, m_globalUniformBuffer, m_globalUniformBufferLastHash );
                if ( offset < 0 )
                {
                     if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Failed to write GlobalUniforms\n" );
                     return false;
                }
                nvrhi::BufferRange range( offset, sizeof( vhGlobalUniform ) );
                outItem = nvrhi::BindingSetItem::ConstantBuffer( item.slot, m_globalUniformBuffer.handle[ m_globalUniformBuffer.frameIdx ], range );
                outItem.type = item.type;
                outItem.unused = 0;
                outItem.unused2 = 0;
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: GlobalUniforms bound to slot %d\n", item.slot );
                return true;
            }
            if ( stageTable.worldUniformsSlot != UINT32_MAX && item.slot == stageTable.worldUniformsSlot )
            {
                int64_t offset = BE_Util_WriteWorldUniform( state, m_worldUniformBuffer, m_worldUniformBufferLastHash );
                if ( offset < 0 )
                {
                     if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Failed to write WorldUniforms\n" );
                     return false;
                }
                nvrhi::BufferRange range( offset, sizeof( vhWorldUniform ) );
                outItem = nvrhi::BindingSetItem::ConstantBuffer( item.slot, m_worldUniformBuffer.handle[ m_worldUniformBuffer.frameIdx ], range );
                outItem.type = item.type;
                outItem.unused = 0;
                outItem.unused2 = 0;
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: WorldUniforms bound to slot %d\n", item.slot );
                return true;
            }

            // Check for User Globals ($Globals)
            if ( stageTable.userGlobalsSlot != UINT32_MAX && stageTable.userGlobalsSlot == item.slot )
            {
                uint64_t hash = stageTable.userGlobalsHash;

                // Lookup in eagerly-allocated cache
                auto cacheIt = scache.userGlobalUniformsBufferCache.find( hash );
                if ( cacheIt == scache.userGlobalUniformsBufferCache.end() )
                {
                    // Should never happen - buffer should have been eagerly allocated
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH )
                        VRHI_ERR( "FindResource: User Globals hash 0x%llx not found in cache (should be pre-allocated)\n", hash );
                    return false;
                }

                // Use pre-allocated buffer
                const auto& info = cacheIt->second;
                outItem = nvrhi::BindingSetItem::ConstantBuffer( item.slot, info.buffer, info.range );
                outItem.type = item.type;
                outItem.unused = 0;
                outItem.unused2 = 0;

                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS )
                    VRHI_LOG( "FindResource: Bound User Globals from cache (hash 0x%llx)\n", hash );

                return true;
            }

            if ( item.slot >= stageTable.bufferTable.size() || !stageTable.bufferTable[item.slot].handle )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: ConstantBuffer not found in cache at slot %d ('%s')\n", item.slot, name ? name : "Unknown" );
                return false;
            }
            const auto result = &stageTable.bufferTable[item.slot];
            assert( result );
            if ( !result->handle )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: ConstantBuffer found in cache at slot %d but null handle ('%s').\n", item.slot, name ? name : "Unknown" );
                return false;
            }
            if ( !result->handle->getDesc().isConstantBuffer )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: ConstantBuffer found in cache at slot %d but NOT a ConstantBuffer ('%s').\n", item.slot, name ? name : "Unknown" );
                return false;
            }

            uint64_t size = result->binding->byteSize ? result->binding->byteSize : result->handle->getDesc().byteSize;
            nvrhi::BufferRange range( result->binding->byteOffset, size );
            outItem = nvrhi::BindingSetItem::ConstantBuffer( item.slot, result->handle, range );
            if ( result->handle->getDesc().isVolatile && item.type != nvrhi::ResourceType::VolatileConstantBuffer )
            {
                 if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Volatile Buffer bound to Static ConstantBuffer slot %d. This may be unsafe! ('%s')\n", item.slot, name ? name : "Unknown" );
                 return false;
            }
            outItem.type = item.type;
            outItem.unused = 0;
            outItem.unused2 = 0;
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: ConstantBuffer found in cache at slot %d ('%s')\n", item.slot, name ? name : "Unknown" );
            return true;
        }

        case nvrhi::ResourceType::Texture_SRV:
        case nvrhi::ResourceType::Texture_UAV:
        {
            const bool isUAV = ( item.type == nvrhi::ResourceType::Texture_UAV );
            const vhStateResolveCache::ResolvedTexture* result = nullptr;

            if ( isUAV )
            {
                if ( item.slot >= stageTable.uavTable.size() || !stageTable.uavTable[item.slot].first.handle )
                    result = nullptr;
                else
                    result = &stageTable.uavTable[item.slot].first;
            }
            else
            {
                //TEMP_PRINT
                //printf( "DEBUG: Lookup texture slot=%u\n", item.slot );
                if ( item.slot >= stageTable.textureTable.size() || !stageTable.textureTable[item.slot].handle )
                    result = nullptr;
                else
                    result = &stageTable.textureTable[item.slot];
            }
            if ( !result )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Texture %s not found in cache at slot %d ('%s')\n", isUAV ? "UAV" : "SRV", item.slot, name ? name : "Unknown" );
                break;
            }
            if ( !result->handle || !result->binding )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Texture %s found in cache at slot %d but invalid configuration ('%s').\n", isUAV ? "UAV" : "SRV", item.slot, name ? name : "Unknown" );
                return false;
            }

            outItem = isUAV ? nvrhi::BindingSetItem::Texture_UAV( item.slot, result->handle ) : nvrhi::BindingSetItem::Texture_SRV( item.slot, result->handle );

            outItem.format = result->binding->formatOverride;
            outItem.subresources = result->binding->subresources;
            outItem.dimension = result->binding->dimensionOverride;
            outItem.unused = 0;
            outItem.unused2 = 0;
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: Texture %s found in cache at slot %d ('%s')\n", isUAV ? "UAV" : "SRV", item.slot, name ? name : "Unknown" );
            return true;
        }

        case nvrhi::ResourceType::TypedBuffer_SRV:
        case nvrhi::ResourceType::TypedBuffer_UAV:
        case nvrhi::ResourceType::StructuredBuffer_SRV:
        case nvrhi::ResourceType::StructuredBuffer_UAV:
        case nvrhi::ResourceType::RawBuffer_SRV:
        case nvrhi::ResourceType::RawBuffer_UAV:
        {
            const bool isUAV = ( item.type == nvrhi::ResourceType::RawBuffer_UAV || item.type == nvrhi::ResourceType::StructuredBuffer_UAV || item.type == nvrhi::ResourceType::TypedBuffer_UAV );
            const vhStateResolveCache::ResolvedBuffer* result = nullptr;

            if ( isUAV )
            {
                if ( item.slot >= stageTable.uavTable.size() || !stageTable.uavTable[item.slot].second.handle )
                    result = nullptr;
                else
                    result = &stageTable.uavTable[item.slot].second;
            }
            else
            {
                //TEMP_PRINT
                //printf( "DEBUG: Lookup SRV buf slot=%u\n", item.slot );
                if ( item.slot >= stageTable.bufferTable.size() || !stageTable.bufferTable[item.slot].handle )
                    result = nullptr;
                else
                    result = &stageTable.bufferTable[item.slot];
            }
            if ( !result )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Buffer %s not found in cache at slot %d ('%s')\n", isUAV ? "UAV" : "SRV", item.slot, name ? name : "Unknown" );
                break;
            }
            if ( !result->handle || !result->binding )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Buffer %s found in cache at slot %d but invalid configuration ('%s').\n", isUAV ? "UAV" : "SRV", item.slot, name ? name : "Unknown" );
                assert( !"Buffer found in cache at slot but invalid configuration. This is likely a Vrhi bug." );
                return false;
            }
            
            // RawBuffer SRV and UAV require 16 byte alignment.
            if ( result->binding->byteOffset % 16 != 0 || result->binding->byteSize % 16 != 0 )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Offset and size must be aligned to 16 bytes for RawBuffer %ss ('%s').\n", isUAV ? "UAV" : "SRV", name ? name : "Unknown" );
                return false;
            }
            
            uint64_t size = result->binding->byteSize ? result->binding->byteSize : result->handle->getDesc().byteSize;
            nvrhi::BufferRange range( result->binding->byteOffset, size );
            nvrhi::Format format = result->handle->getDesc().format;

            switch ( item.type )
            {
                case nvrhi::ResourceType::TypedBuffer_SRV:
                case nvrhi::ResourceType::TypedBuffer_UAV:
                {
                    if ( format == nvrhi::Format::UNKNOWN )
                    {
                        if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Unknown format %d (%s) for typed buffer ('%s').\n", ( int ) format, nvrhi::getFormatInfo( format ).name, name ? name : "Unknown" );
                        return false;
                    }
                    outItem = ( item.type == nvrhi::ResourceType::TypedBuffer_UAV )
                        ? nvrhi::BindingSetItem::TypedBuffer_UAV( item.slot, result->handle, format, range )
                        : nvrhi::BindingSetItem::TypedBuffer_SRV( item.slot, result->handle, format, range );
                    outItem.unused = 0;
                    outItem.unused2 = 0;
                    break;
                }
                case nvrhi::ResourceType::StructuredBuffer_SRV:
                case nvrhi::ResourceType::StructuredBuffer_UAV:
                {
                    outItem = ( item.type == nvrhi::ResourceType::StructuredBuffer_UAV )
                        ? nvrhi::BindingSetItem::StructuredBuffer_UAV( item.slot, result->handle, format, range )
                        : nvrhi::BindingSetItem::StructuredBuffer_SRV( item.slot, result->handle, format, range );
                    outItem.unused = 0;
                    outItem.unused2 = 0;
                    break;
                }
                case nvrhi::ResourceType::RawBuffer_SRV:
                case nvrhi::ResourceType::RawBuffer_UAV:
                {
                    outItem = ( item.type == nvrhi::ResourceType::RawBuffer_UAV )
                        ? nvrhi::BindingSetItem::RawBuffer_UAV( item.slot, result->handle, range )
                        : nvrhi::BindingSetItem::RawBuffer_SRV( item.slot, result->handle, range );
                    outItem.unused = 0;
                    outItem.unused2 = 0;
                    break;
                }
                default:
                    assert( !"Invalid resource type. This is likely a Vrhi bug." );
                    return false;
            }

            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: %s %s found in cache at slot %d ('%s')\n", vhResourceTypeToString( item.type ), isUAV ? "UAV" : "SRV", item.slot, name ? name : "Unknown" );
            return true;
        }
        case nvrhi::ResourceType::PushConstants:
        {
            outItem = nvrhi::BindingSetItem::PushConstants( item.slot, item.size );
            return true;
        }
        case nvrhi::ResourceType::Sampler:
        {
            //TEMP_PRINT
            //printf( "DEBUG: Lookup sampler slot=%u\n", item.slot );
            if ( item.slot >= stageTable.samplerTable.size() || !stageTable.samplerTable[item.slot] )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Sampler not found in cache at slot %d ('%s')\n", item.slot, name ? name : "Unknown" );
                break;
            }
            auto shandle = stageTable.samplerTable[item.slot];
            if ( !shandle )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Sampler found in cache at slot %d but handle is null ('%s')\n", item.slot, name ? name : "Unknown" );
                return false;
            }
            outItem = nvrhi::BindingSetItem::Sampler( item.slot, shandle );
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: Sampler found in cache at slot %d ('%s')\n", item.slot, name ? name : "Unknown" );
            return true;
        }
        case nvrhi::ResourceType::RayTracingAccelStruct:
        {
            if ( item.slot >= ( int32_t ) stageTable.accelStructTable.size() ||
                 !stageTable.accelStructTable[item.slot].handle )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: AccelStruct not found at slot %d ('%s')\n", item.slot, name ? name : "Unknown" );
                return false;
            }
            outItem = nvrhi::BindingSetItem::RayTracingAccelStruct( item.slot, stageTable.accelStructTable[item.slot].handle.Get() );
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: AccelStruct found at slot %d ('%s')\n", item.slot, name ? name : "Unknown" );
            return true;
        }
        default:
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Unknown or unsupported resource type %d (%s) at slot %d\n", ( int ) item.type, vhResourceTypeToString( item.type ), item.slot );
            break;
    }

    return false;
}

bool vhCmdBackendState::BE_PreSubmitCommon_State(
    nvrhi::CommandListHandle cmdList,
    vhState& state,
    vhBackendShader* const* shaders,
    int shaderCount,
    nvrhi::ComputeState* computeState,
    nvrhi::GraphicsState* graphicsState,
    nvrhi::rt::State* rtState,
    const nvrhi::BindingLayoutVector* layoutOverride,
    nvrhi::FramebufferHandle fb
)
{
    // Find the PSO layout to use.
    const nvrhi::BindingLayoutVector* psoLayouts = layoutOverride;
    if ( computeState && computeState->pipeline )
        psoLayouts = &computeState->pipeline->getDesc().bindingLayouts;
    if ( graphicsState && graphicsState->pipeline )
        psoLayouts = &graphicsState->pipeline->getDesc().bindingLayouts;
    if ( !psoLayouts )
    {
        VRHI_ERR( "vhSetState(): No PSO layout. This is likely a Vrhi bug.\n" );
        assert( !"No state or PSO layout" );
        return false;
    }
    const nvrhi::BindingLayoutVector& layouts = *psoLayouts;

    // Build layout-to-shader map only when PSO changes. This avoids repeated hashing and map rebuilds.
    nvrhi::IGraphicsPipeline* currentGraphicsPSO = graphicsState ? graphicsState->pipeline : nullptr;
    nvrhi::IComputePipeline* currentComputePSO = computeState ? computeState->pipeline : nullptr;
    const bool hasLayoutOverride = ( layoutOverride != nullptr );
    bool shaderSetChanged = ( s_lastLayoutMapShaders.size() != ( size_t ) shaderCount );
    if ( !shaderSetChanged )
    {
        for ( int shaderIdx = 0; shaderIdx < shaderCount; ++shaderIdx )
        {
            if ( s_lastLayoutMapShaders[shaderIdx] != shaders[shaderIdx] )
            {
                shaderSetChanged = true;
                break;
            }
        }
    }
    const bool psoChanged = hasLayoutOverride ||
        currentGraphicsPSO != s_lastGraphicsPSO ||
        currentComputePSO != s_lastComputePSO ||
        shaderSetChanged;

    static robin_hood::unordered_flat_map< uint64_t, const nvrhi::BindingLayoutHandle* > s_hashToPSOlayout;
    if ( psoChanged )
    {
        // Build map of hash --> psoLayouts. Static map uses clear() to preserve bucket array capacity.
        vhProfile( "BE_PreSubmitCommon_State_PSOLayoutHash", true );
        s_hashToPSOlayout.clear();
        for ( int i = 0; i < layouts.size(); i++ )
        {
            auto bdesc = layouts[i]->getDesc();
            if ( !bdesc ) continue;
            auto hash = vhHashBindingLayout( *bdesc );
            s_hashToPSOlayout[hash] = &layouts[i];
        }
        vhProfile( "BE_PreSubmitCommon_State_PSOLayoutHash", false );

        // Build map of layouts --> shader.
        vhProfile( "BE_PreSubmitCommon_State_ShaderLayoutMatch", true );

        s_layoutToShader.clear();
        for ( int shaderIdx = 0; shaderIdx < shaderCount; ++shaderIdx )
        {
            auto shader = shaders[shaderIdx];
            if ( !BE_Util_ShaderStageMatches( shader->flags, computeState != nullptr, graphicsState != nullptr, rtState != nullptr ) )
                continue;

            if ( !shader->layout )
            {
                VRHI_ERR( "vhSetState(): NULL shader layout. Stripped spirv-reflection?" );
                assert( !"NULL shader layout" );
                continue;
            }

            // Shaders with no reflected bindings have nothing to bind for; their descriptor set
            // is filled with the shared empty layout in the PSO layout list and binds m_emptySet.
            if ( shader->layout->getDesc() && shader->layout->getDesc()->bindings.empty() )
                continue;

            // Match shader.layout to equivalent state->pipeline->getDesc().bindingLayouts->layout.
            assert( shader->layout->getDesc() );
            auto hash = vhHashBindingLayout( *shader->layout->getDesc() );
            if ( s_hashToPSOlayout.find( hash ) == s_hashToPSOlayout.end() || !s_hashToPSOlayout[hash] )
            {
                VRHI_ERR( "vhSetState(): Mismatch between shader layout and PSO layout. This is likely a Vrhi bug." );
                assert( !"Mismatch between shader layout and PSO layout" );
                continue;
            }
            const auto& tempPSOLayout = *s_hashToPSOlayout[hash];
            assert( s_layoutToShader.find( tempPSOLayout ) == s_layoutToShader.end() ); // Duplicate layouts should be impossible.
            s_layoutToShader[ tempPSOLayout ] = shader;
        }
        s_hashToPSOlayout.clear();
        vhProfile( "BE_PreSubmitCommon_State_ShaderLayoutMatch", false );

        s_lastGraphicsPSO = currentGraphicsPSO;
        s_lastComputePSO = currentComputePSO;
        s_lastLayoutMapShaders.resize( shaderCount );
        for ( int shaderIdx = 0; shaderIdx < shaderCount; ++shaderIdx )
            s_lastLayoutMapShaders[shaderIdx] = shaders[shaderIdx];
    }

    // Resolve state resource cache
    s_resolveCache.Clear();
    vhProfile( "BE_PreSubmitCommon_State_ResolveCache", true );
    BE_PreSubmitCommon_ResolveStateCache( state, shaders, shaderCount, s_resolveCache );
    vhProfile( "BE_PreSubmitCommon_State_ResolveCache", false );
    if ( !s_resolveCache.init )
    {
        VRHI_ERR( "vhSetState(): Failed to resolve state resource cache.\n" );
        return false;
    }

    // Loop through the layouts and bind resources.

    vhProfile( "BE_PreSubmitCommon_State_BindingSetBuild", true );
    static nvrhi::BindingSetDesc bsetDesc;
    if ( bsetDesc.bindings.capacity() == 0 ) bsetDesc.bindings.reserve( 32 );

    for ( uint32_t layoutIdx = 0; layoutIdx < ( uint32_t ) layouts.size(); layoutIdx++ )
    {
        auto savedBindings = std::move( bsetDesc.bindings );
        bsetDesc = nvrhi::BindingSetDesc{};
        bsetDesc.bindings = std::move( savedBindings );
        bsetDesc.bindings.clear();

        auto layout = layouts[layoutIdx];
        assert( layout );
        auto layoutDesc = layout->getDesc();
        assert( layoutDesc );

        if ( layout == m_emptyLayout || ( layoutDesc && layoutDesc->bindings.empty() ) )
        {
            // Either our shared empty layout or any user-provided layout with no bindings
            // both use m_emptySet — two empty VkDescriptorSetLayouts with 0 bindings are
            // always compatible regardless of visibility flags.
            if ( computeState )  computeState->addBindingSet( m_emptySet );
            if ( graphicsState ) graphicsState->addBindingSet( m_emptySet );
            if ( rtState )       rtState->bindings.push_back( m_emptySet );
            continue;
        }

        // Build a flat vector of reflection slot --> reflection resource.
        // Reflection slots are small non-negative integers, so direct indexing is O(1).

        s_slotToReflection.clear();
        auto layoutItr = s_layoutToShader.find( layout );
        if ( layoutItr == s_layoutToShader.end() )
            continue;
        auto shader = layoutItr->second;
        if ( !shader )
            continue;
        if ( state.debugFlags == VRHI_STATE_DEBUG_ALL ) VRHI_LOG( "Binding resources for shader %s.\n", shader->name.c_str() );

        for ( uint32_t i = 0; i < ( uint32_t ) shader->reflection.size(); i++ )
        {
            auto& reflection = shader->reflection[i];
            if ( reflection.slot >= s_slotToReflection.size() )
                s_slotToReflection.resize( reflection.slot + 1, nullptr );
            assert( s_slotToReflection[reflection.slot] == nullptr );
            s_slotToReflection[reflection.slot] = &reflection;
        }
        uint32_t stage = ( uint32_t ) ( shader->flags & VRHI_SHADER_STAGE_MASK );
        assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );

        // Loop through the required bindings for this layout.

        for ( uint32_t bindingIdx = 0; bindingIdx < layoutDesc->bindings.size(); bindingIdx++ )
        {
            auto binding = layoutDesc->bindings[bindingIdx];
            //TEMP_PRINT
            //printf( "DEBUG: Layout binding type=%d slot=%u\n", (int)binding.type, binding.slot );

            if ( binding.type == nvrhi::ResourceType::PushConstants )
            {
                // Special early branch for push constants.
                nvrhi::BindingSetItem item;
                if ( BE_PreSubmitCommon_FindResource( state, stage, s_resolveCache, binding, item, "PushConstants" ) )
                {
                    bsetDesc.addItem( item );
                }
                continue;
            }

            // Find the corresponding reflection resource.
            const vhShaderReflectionResource* reflectionPtr = ( binding.slot < s_slotToReflection.size() ) ? s_slotToReflection[binding.slot] : nullptr;
            if ( !reflectionPtr )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Binding Slot %d not found in shader reflection. Submit / Dispatch aborted.\n", binding.slot );
                vhProfile( "BE_PreSubmitCommon_State_BindingSetBuild", false );
                return false;
            }
            const auto& reflection = *reflectionPtr;

            // Validate the reflection against the layout.
            if ( !vhShaderValidateBinding( reflection, binding, !!( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) ) )
            {
                vhProfile( "BE_PreSubmitCommon_State_BindingSetBuild", false );
                return false;
            }

            nvrhi::BindingSetItem item;
            if ( !BE_PreSubmitCommon_FindResource( state, stage, s_resolveCache, binding, item, reflection.name.c_str() ) )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Binding Slot %d not found in state. Dummy resource bound.\n", binding.slot );
                item = vhGetDummyBindingItem( binding, reflection.format, reflection.dim );
            }
            bsetDesc.addItem( item );
        }

        // Create Binding Set.
        nvrhi::BindingSetHandle bset = vhGetBindingSet( bsetDesc, layout );
        if ( !bset )
        {
            VRHI_ERR( "vhSetState() : Failed to create NVRHI binding set for shader %p!\n", shader->handle.Get() );
            vhProfile( "BE_PreSubmitCommon_State_BindingSetBuild", false );
            return false;
        }

        if ( computeState )  computeState->addBindingSet( bset );
        if ( graphicsState ) graphicsState->addBindingSet( bset );
        if ( rtState )       rtState->bindings.push_back( bset );
    }

    if ( graphicsState )
    {
        // Transfer some overlapping state from vhState to nvrhi::GraphicsState.
        vhProfile( "BE_PreSubmitCommon_State_GraphicsStateSetup", true );
        // Set blend constant color
        graphicsState->blendConstantColor = nvrhi::Color( state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a );

        // Set shading rate
        if ( state.shadingRateFlags != VRHI_VRS_1X1 || state.shadingRateImage != VRHI_INVALID_HANDLE )
        {
            auto& vrs = graphicsState->shadingRateState;
            vrs.enabled = true;
            vrs.shadingRate = vhTranslateShadingRate( state.shadingRateFlags & 0xF );
            vrs.imageCombiner = vhTranslateShadingRateCombiner( ( state.shadingRateFlags >> 4 ) & 0xF );
            // Image is bound in the framebuffer.
        }

        // Set viewport
        graphicsState->viewport.viewports.resize( 0 );
        graphicsState->viewport.viewports.push_back( nvrhi::Viewport(
            state.viewRect.x, state.viewRect.x + state.viewRect.z,
            state.viewRect.y, state.viewRect.y + state.viewRect.w,
            state.viewDepthRange.x, state.viewDepthRange.y
        ) );

        // Set scissor
        graphicsState->viewport.scissorRects.resize( 0 );
        if ( state.viewScissor.z >= 0.0f && state.viewScissor.w >= 0.0f )
        {
            graphicsState->viewport.scissorRects.push_back( nvrhi::Rect(
                ( int ) state.viewScissor.x, ( int ) ( state.viewScissor.x + state.viewScissor.z ),
                ( int ) state.viewScissor.y, ( int ) ( state.viewScissor.y + state.viewScissor.w )
            ) );
        }
        else
        {
            graphicsState->viewport.scissorRects.push_back( nvrhi::Rect(
                ( int ) state.viewRect.x, ( int ) ( state.viewRect.x + state.viewRect.z ),
                ( int ) state.viewRect.y, ( int ) ( state.viewRect.y + state.viewRect.w )
            ) );
        }

        // nvrhi::DepthStencilState::dynamicStencilRefValue is false, but we set this any way because it's fun.
        graphicsState->dynamicStencilRefValue = ( uint8_t ) ( ( state.stencilState & VRHI_STENCIL_FUNC_REF_MASK ) >> VRHI_STENCIL_FUNC_REF_SHIFT );

        // Bind Framebuffer
        graphicsState->framebuffer = fb ? fb : BE_GetFrameBuffer( state.colourAttachment, state.depthAttachment, state.shadingRateImage );

        // Bind Vertex Buffers
        for ( size_t vbIdx = 0; vbIdx < state.vertexBindings.size(); ++vbIdx )
        {
            const auto& vb = state.vertexBindings[ vbIdx ];
            if ( vb.buffer == VRHI_INVALID_HANDLE ) continue;
            auto it = backendBuffers.find( vb.buffer );
            if ( it != backendBuffers.end() && it->second->handle )
            {
                graphicsState->addVertexBuffer( nvrhi::VertexBufferBinding()
                    .setBuffer( it->second->handle )
                    .setSlot( vb.stream )
                    .setOffset( vb.byteOffset ) );
            }
        }

        // Bind Index Buffer
        if ( state.indexBinding.buffer != VRHI_INVALID_HANDLE )
        {
            auto it = backendBuffers.find( state.indexBinding.buffer );
            if ( it != backendBuffers.end() && it->second->handle )
            {
                bool is32Bit = !!( it->second->flags & VRHI_BUFFER_INDEX32 );
                assert( it->second->stride == ( is32Bit ? 4 : 2 ) );

                graphicsState->setIndexBuffer( nvrhi::IndexBufferBinding()
                    .setBuffer( it->second->handle )
                    .setFormat( is32Bit ? nvrhi::Format::R32_UINT : nvrhi::Format::R16_UINT )
                    .setOffset( ( uint32_t ) state.indexBinding.byteOffset ) );
            }
        }
    }

    // Bind Indirect Parameters if present
    if ( state.indirectParams.buffer != VRHI_INVALID_HANDLE )
    {
        auto it = backendBuffers.find( state.indirectParams.buffer );
        if ( it != backendBuffers.end() && it->second->handle )
        {
            if ( graphicsState ) graphicsState->setIndirectParams( it->second->handle );
            if ( computeState ) computeState->setIndirectParams( it->second->handle );
        }
    }

    if ( graphicsState )
    {
        vhProfile( "BE_PreSubmitCommon_State_GraphicsStateSetup", false );
    }

    // Final layout check and detailed diff print.

    const nvrhi::BindingSetVector* stateLayouts = nullptr;
    if ( computeState ) stateLayouts = &computeState->bindings;
    if ( graphicsState ) stateLayouts = &graphicsState->bindings;
    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH && stateLayouts )
    {
        assert( psoLayouts );
        if ( !vhDebugLayoutDiffCheck( *psoLayouts, *stateLayouts ) )
            return false;
    }

    if ( rtState )
    {
        if ( ( state.dirty & VRHI_DIRTY_PUSH_CONSTANTS ) || ( state.dirty & VRHI_DIRTY_WORLD ) )
        {
            for ( auto* shader : s_resolveCache.bshaders )
            {
                if ( !shader->pushConstants.empty() )
                {
                    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
                    vhSetPushConstant_DeviceStateLocked( cmdList, state );
                    break;
                }
            }
        }
    }
    vhProfile( "BE_PreSubmitCommon_State_BindingSetBuild", false );
    return true;
}

void vhCmdBackendState::BE_Dispatch( vhState& state, vhBackendShader& computeShader, glm::uvec3 workGroupCount )
{
    VRHI_PROFILE_FUNCTION();
    assert( computeShader.handle );

    vhBackendShader* shaderPtr = &computeShader;
    vhResetComputePipelineDesc( s_dispatchDesc );
    vhResetComputeState( s_dispatchCState );

    vhProfile( "BE_Dispatch_PipelineDesc", true );
    if ( !BE_PresubmitCommon_PipelineDesc( state, &shaderPtr, 1, &s_dispatchDesc, nullptr ) )
    {
        VRHI_ERR( "vhDispatch() : Failed to create nvrhi::ComputePipelineDesc for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        vhProfile( "BE_Dispatch_PipelineDesc", false );
        return;
    }
    vhProfile( "BE_Dispatch_PipelineDesc", false );

    nvrhi::ComputePipelineHandle pso = vhPSOCacheGet( s_dispatchDesc );
    vhProfile( "BE_Dispatch_PSOCache", true );
    if ( !pso )
    {
        VRHI_ERR( "vhDispatch() : Failed to create nvrhi::ComputePipelineHandle PSO for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        vhProfile( "BE_Dispatch_PSOCache", false );
        return;
    }
    vhProfile( "BE_Dispatch_PSOCache", false );

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    s_dispatchCState.setPipeline( pso.Get() );
    vhProfile( "BE_Dispatch_StateSetup", true );
    if ( !BE_PreSubmitCommon_State( cmdlist, state, &shaderPtr, 1, &s_dispatchCState, nullptr, nullptr, nullptr ) )
    {
        VRHI_ERR( "vhDispatch() : Failed to create nvrhi::ComputeState for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        vhProfile( "BE_Dispatch_StateSetup", false );
        return;
    }
    vhProfile( "BE_Dispatch_StateSetup", false );

    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->setComputeState( s_dispatchCState );
        
        if ( ( ( state.dirty & VRHI_DIRTY_PUSH_CONSTANTS ) || ( state.dirty & VRHI_DIRTY_WORLD ) ) && !computeShader.pushConstants.empty() )
        {
            vhProfile( "BE_Dispatch_PushConstants", true );
            vhSetPushConstant_DeviceStateLocked( cmdlist, state );
            vhProfile( "BE_Dispatch_PushConstants", false );
        }

        vhProfile( "BE_Dispatch_Execute", true );
        cmdlist->dispatch( workGroupCount.x, workGroupCount.y, workGroupCount.z );
        vhProfile( "BE_Dispatch_Execute", false );
    }
}

void vhCmdBackendState::BE_DispatchIndirect( vhState& state, vhBackendShader& computeShader, vhBackendBuffer& indirectBuffer, uint64_t byteOffset )
{
    VRHI_PROFILE_FUNCTION();
    assert( computeShader.handle );
    assert( indirectBuffer.handle );

    vhProfile( "BE_DispatchIndirect_Validation", true );
    if ( !( indirectBuffer.flags & VRHI_BUFFER_DRAW_INDIRECT ) )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Indirect buffer %s was not created with VRHI_BUFFER_DRAW_INDIRECT! SKIPPING COMPUTE DISPATCH.\n", indirectBuffer.name.c_str() );
        vhProfile( "BE_DispatchIndirect_Validation", false );
        return;
    }
    vhProfile( "BE_DispatchIndirect_Validation", false );

    vhBackendShader* shaderPtr = &computeShader;
    vhResetComputePipelineDesc( s_dispatchDesc );
    vhResetComputeState( s_dispatchCState );

    vhProfile( "BE_DispatchIndirect_PipelineDesc", true );
    if ( !BE_PresubmitCommon_PipelineDesc( state, &shaderPtr, 1, &s_dispatchDesc, nullptr ) )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Failed to create nvrhi::ComputePipelineDesc for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        vhProfile( "BE_DispatchIndirect_PipelineDesc", false );
        return;
    }
    vhProfile( "BE_DispatchIndirect_PipelineDesc", false );

    nvrhi::ComputePipelineHandle pso = vhPSOCacheGet( s_dispatchDesc );
    vhProfile( "BE_DispatchIndirect_PSOCache", true );
    if ( !pso )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Failed to create nvrhi::ComputePipelineHandle PSO for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        vhProfile( "BE_DispatchIndirect_PSOCache", false );
        return;
    }
    vhProfile( "BE_DispatchIndirect_PSOCache", false );

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );

    s_dispatchCState.setPipeline( pso.Get() );
    vhProfile( "BE_DispatchIndirect_StateSetup", true );
    if ( !BE_PreSubmitCommon_State( cmdlist, state, &shaderPtr, 1, &s_dispatchCState, nullptr, nullptr, nullptr ) )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Failed to create nvrhi::ComputeState for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        vhProfile( "BE_DispatchIndirect_StateSetup", false );
        return;
    }
    vhProfile( "BE_DispatchIndirect_StateSetup", false );

    vhProfile( "BE_DispatchIndirect_SetParams", true );
    s_dispatchCState.setIndirectParams( indirectBuffer.handle );
    vhProfile( "BE_DispatchIndirect_SetParams", false );

    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->setComputeState( s_dispatchCState );
        
        if ( ( ( state.dirty & VRHI_DIRTY_PUSH_CONSTANTS ) || ( state.dirty & VRHI_DIRTY_WORLD ) ) && !computeShader.pushConstants.empty() )
        {
            vhProfile( "BE_DispatchIndirect_PushConstants", true );
            vhSetPushConstant_DeviceStateLocked( cmdlist, state );
            vhProfile( "BE_DispatchIndirect_PushConstants", false );
        }

        vhProfile( "BE_DispatchIndirect_Execute", true );
        cmdlist->dispatchIndirect( ( uint32_t ) byteOffset );
        vhProfile( "BE_DispatchIndirect_Execute", false );
    }
}

void vhCmdBackendState::BE_Submit( vhState& state, vhBackendShader* const* shaders, int shaderCount, uint32_t flags, const nvrhi::DrawArguments& args, uint32_t drawCount )
{
    VRHI_PROFILE_FUNCTION();
    vhResetGraphicsPipelineDesc( s_submitPipelineDesc );
    vhResetGraphicsState( s_submitGState );

    vhProfile( "BE_Submit_PipelineDesc", true );
    if ( !BE_PresubmitCommon_PipelineDesc( state, shaders, shaderCount, nullptr, &s_submitPipelineDesc ) )
    {
        VRHI_ERR( "BE_Submit(): Failed to create pipeline descriptor!\n" );
        vhProfile( "BE_Submit_PipelineDesc", false );
        return;
    }
    vhProfile( "BE_Submit_PipelineDesc", false );

    vhProfile( "BE_Submit_GetFramebuffer", true );
    nvrhi::FramebufferHandle fb = BE_GetFrameBuffer( state.colourAttachment, state.depthAttachment, state.shadingRateImage );
    vhProfile( "BE_Submit_GetFramebuffer", false );
    if ( !fb )
    {
        VRHI_ERR( "BE_Submit(): Failed to get Framebuffer!\n" );
        return;
    }

    nvrhi::GraphicsPipelineHandle pso = vhPSOCacheGet( s_submitPipelineDesc, fb->getFramebufferInfo() );
    vhProfile( "BE_Submit_PSOCache", true );
    if ( !pso )
    {
        VRHI_ERR( "BE_Submit(): Failed to create PSO!\n" );
        vhProfile( "BE_Submit_PSOCache", false );
        return;
    }
    vhProfile( "BE_Submit_PSOCache", false );

    s_submitGState.setPipeline( pso.Get() );
    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    vhProfile( "BE_Submit_StateSetup", true );
    if ( !BE_PreSubmitCommon_State( cmdlist, state, shaders, shaderCount, nullptr, &s_submitGState, nullptr, nullptr, fb ) )
    {
        VRHI_ERR( "BE_Submit(): Failed to set graphics state!\n" );
        vhProfile( "BE_Submit_StateSetup", false );
        return;
    }
    vhProfile( "BE_Submit_StateSetup", false );

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdlist->setGraphicsState( s_submitGState );

        if ( ( state.dirty & VRHI_DIRTY_PUSH_CONSTANTS ) || ( state.dirty & VRHI_DIRTY_WORLD ) )
        {
            vhProfile( "BE_Submit_PushConstants", true );
            for ( int i = 0; i < shaderCount; ++i )
            {
                if ( !shaders[i]->pushConstants.empty() )
                {
                    vhSetPushConstant_DeviceStateLocked( cmdlist, state );
                    break;
                }
            }
            vhProfile( "BE_Submit_PushConstants", false );
        }

        vhProfile( "BE_Submit_Execute", true );
        if ( flags & VRHI_DRAW_INDIRECT )
        {
            uint32_t offset = ( uint32_t ) state.indirectParams.byteOffset;
            if ( flags & VRHI_DRAW_INDEXED )
                cmdlist->drawIndexedIndirect( offset, drawCount );
            else
                cmdlist->drawIndirect( offset, drawCount );
        }
        else
        {
            if ( flags & VRHI_DRAW_INDEXED )
            {
                cmdlist->drawIndexed( args );
            }
            else
            {
                cmdlist->draw( args );
            }
        }
        vhProfile( "BE_Submit_Execute", false );
    }
}

void vhCmdBackendState::BE_BlitBuffer( vhBackendBuffer& dst, vhBackendBuffer& src, uint64_t dstOffset, uint64_t srcOffset, uint64_t size )
{
    VRHI_PROFILE_FUNCTION();
    // Should already have been validated by handler.
    assert( dst.handle );
    assert( src.handle );
    assert( dstOffset + size <= dst.desc.byteSize );
    assert( srcOffset + size <= src.desc.byteSize );

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        vhProfile( "BE_BlitBuffer_Execute", true );
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->copyBuffer( dst.handle, dstOffset, src.handle, srcOffset, size );
        vhProfile( "BE_BlitBuffer_Execute", false );
    }
}

void vhCmdBackendState::BE_DispatchRays( vhState& state, vhBackendRTPipeline& pipeline, vhBackendShaderTable& shaderTable, const nvrhi::rt::DispatchRaysArguments& args )
{
    VRHI_PROFILE_FUNCTION();
    vhBackendShader* rtShaders[VRHI_SHADER_STAGE_MAX];
    int rtShaderCount = 0;
    vhProfile( "BE_DispatchRays_ShaderSetup", true );
    for ( auto h : state.program )
    {
        auto it = backendShaders.find( h );
        if ( it != backendShaders.end() && it->second )
        {
            assert( rtShaderCount < VRHI_SHADER_STAGE_MAX );
            rtShaders[rtShaderCount++] = it->second.get();
        }
    }
    vhProfile( "BE_DispatchRays_ShaderSetup", false );

    nvrhi::rt::State rtState;
    rtState.shaderTable = shaderTable.handle;

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );

    vhProfile( "BE_DispatchRays_StateSetup", true );
    if ( !BE_PreSubmitCommon_State(
        cmdlist, state, rtShaders, rtShaderCount,
        nullptr, nullptr,
        &rtState, &pipeline.desc.globalBindingLayouts ) )
    {
        VRHI_ERR( "BE_DispatchRays(): Failed to set RT state.\n" );
        vhProfile( "BE_DispatchRays_StateSetup", false );
        return;
    }
    vhProfile( "BE_DispatchRays_StateSetup", false );

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdlist->setRayTracingState( rtState );
        vhProfile( "BE_DispatchRays_Execute", true );
        cmdlist->dispatchRays( args );
        vhProfile( "BE_DispatchRays_Execute", false );
    }
}

void vhCmdBackendState::init()
{
    std::lock_guard< std::mutex > lock( backendMutex );

    if ( !m_globalUniformBuffer.handle[0] )
    {
        // Called from vhInit which already holds g_nvRHIStateMutex lock, and before RHI thread even starts.
        // So we don't need to lock g_nvRHIStateMutex here.

        nvrhi::BufferDesc desc;
        desc.setByteSize( g_vhInit.maxViewGlobals * sizeof( vhGlobalUniform ) );
        desc.setIsConstantBuffer( true );
        desc.setCpuAccess( nvrhi::CpuAccessMode::Write );
        desc.setDebugName( "GlobalUniforms" );
        m_globalUniformBuffer.Init_DeviceStateLocked( desc );
        
        nvrhi::BufferDesc descWorld;
        descWorld.setByteSize( g_vhInit.maxWorldMatrices * sizeof( vhWorldUniform ) ); 
        descWorld.setIsConstantBuffer( true );
        descWorld.setCpuAccess( nvrhi::CpuAccessMode::Write );
        descWorld.setDebugName( "WorldUniforms" );
        m_worldUniformBuffer.Init_DeviceStateLocked( descWorld );
        
        nvrhi::BufferDesc descUser;
        descUser.setByteSize( g_vhInit.maxUserGlobals ); 
        descUser.setIsConstantBuffer( true );
        descUser.setCpuAccess( nvrhi::CpuAccessMode::Write );
        descUser.setDebugName( "UserUniforms" );
        m_userUniformBuffer.Init_DeviceStateLocked( descUser );
        assert( g_vhInit.maxUserGlobals % VRHI_CBUF_ALIGN == 0 );

        nvrhi::BindingSetDesc bsdescEmpty;
        nvrhi::BindingLayoutDesc bldescEmpty = {};
        bldescEmpty.visibility = nvrhi::ShaderType::All;
        m_emptyLayout = g_vhDevice->createBindingLayout( bldescEmpty );
        m_emptySet = g_vhDevice->createBindingSet( bsdescEmpty, m_emptyLayout );
    }
}

void vhCmdBackendState::shutdown()
{
    std::lock_guard< std::mutex > lock( backendMutex );
    std::lock_guard< std::mutex > lock2( g_nvRHIStateMutex );

    for ( auto& heapPair : backendHeaps )
    {
        if ( heapPair.second && heapPair.second->allocator )
        {
            delete static_cast< OffsetAllocator::Allocator* >( heapPair.second->allocator );
            heapPair.second->allocator = nullptr;
            heapPair.second->allocations.clear();
        }
    }

    backendAccelStructs.clear();
    backendRTPipelines.clear();
    backendShaderTables.clear();

    m_globalUniformBuffer.Shutdown_DeviceStateLocked();
    m_worldUniformBuffer.Shutdown_DeviceStateLocked();
    m_userUniformBuffer.Shutdown_DeviceStateLocked();

    m_emptySet = nullptr;
    m_emptyLayout = nullptr;

    backendTextures.clear();
    backendHeaps.clear();
    backendBuffers.clear();
    backendShaders.clear();
    backendTimerQueries.clear();

    // Clear static caches and release all RefCountPtr members
    s_layoutToShader.clear();
    s_resolveCache.Clear();
    s_slotToReflection.clear();
    s_layoutLocationTable.clear();
    s_attributes.clear();
    s_shaders.clear();
    s_lastLayoutMapShaders.clear();
    s_lastGraphicsPSO = nullptr;
    s_lastComputePSO = nullptr;
    s_submitPipelineDesc = nvrhi::GraphicsPipelineDesc{};
    s_submitGState       = nvrhi::GraphicsState{};
    s_dispatchDesc       = nvrhi::ComputePipelineDesc{};
    s_dispatchCState     = nvrhi::ComputeState{};
}

void vhCmdBackendState::HandleLogFunction( const char* str )
{
    if ( g_vhInit.logBackendCmds ) VRHI_LOG( "BackendCmd: %s\n", str );
}

void vhCmdBackendState::RegisterInternalTexture( vhTexture id, const nvrhi::TextureHandle& handle, const nvrhi::TextureDesc& desc )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto btex = std::make_unique< vhBackendTexture >();
    btex->handle = handle;
    btex->name = desc.debugName;
    btex->info.target = desc.dimension;
    btex->info.dimensions = { desc.width, desc.height, desc.depth };
    btex->info.format = desc.format;
    btex->info.mipLevels = ( int32_t ) desc.mipLevels;
    btex->info.arrayLayers = ( int32_t ) desc.arraySize;
    btex->flags = VRHI_TEXTURE_NONE;
    vhTextureMiplevelInfo( btex->mipInfo, btex->pitchSize, btex->arraySize, btex->info );
    backendTextures[id] = std::move( btex );
}

// --------------------------------------------------------------------------
// Backend :: VIDL Command Handlers
// --------------------------------------------------------------------------


void vhCmdBackendState::Handle_vhResizeCleanup( VIDL_vhResizeCleanup* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    vhFBOCacheReset();
}

void vhCmdBackendState::Handle_vhBeginTimerQuery( VIDL_vhBeginTimerQuery* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    // Auto-create timer query if it doesn't exist
    auto it = backendTimerQueries.find( cmd->timerID );
    if ( it == backendTimerQueries.end() )
    {
        std::unique_ptr< vhBackendTimerQuery > timerQuery = std::make_unique< vhBackendTimerQuery >();
        timerQuery->initialised = true;

        // Create ring buffer of NVRHI timer query handles
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT; ++i )
        {
            timerQuery->handles[i] = g_vhDevice->createTimerQuery();
            if ( !timerQuery->handles[i] )
            {
                VRHI_ERR( "vhBeginTimerQuery(): Failed to create timer query handle for ID %llu\n", cmd->timerID );
                return;
            }
        }

        it = backendTimerQueries.emplace( cmd->timerID, std::move( timerQuery ) ).first;
    }

    auto& timerQuery = *it->second;
    int currentIdx = timerQuery.currentFrameIndex;

    // Begin timing on current frame's query handle
    auto cmdList = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdList->beginTimerQuery( timerQuery.handles[currentIdx] );
    }
}

void vhCmdBackendState::Handle_vhEndTimerQuery( VIDL_vhEndTimerQuery* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendTimerQueries.find( cmd->timerID );
    if ( it == backendTimerQueries.end() )
    {
        VRHI_ERR( "vhEndTimerQuery(): Timer ID %llu not found. Must call vhBeginTimerQuery first.\n", cmd->timerID );
        return;
    }

    auto& timerQuery = *it->second;
    int currentIdx = timerQuery.currentFrameIndex;

    // End timing on current frame's query handle
    auto cmdList = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdList->endTimerQuery( timerQuery.handles[currentIdx] );
    }

    // Read result from PREVIOUS frame (ring buffer offset)
    int readIdx = ( currentIdx + VRHI_MAX_FRAMES_INFLIGHT - 1 ) % VRHI_MAX_FRAMES_INFLIGHT;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        float timeSeconds = g_vhDevice->getTimerQueryTime( timerQuery.handles[readIdx] );
        if ( timeSeconds > 0.0f )
        {
            timerQuery.lastQueryTime = timeSeconds;
        }
    }

    // Advance ring buffer index for next frame
    timerQuery.currentFrameIndex = ( currentIdx + 1 ) % VRHI_MAX_FRAMES_INFLIGHT;
}

void vhCmdBackendState::Handle_vhBeginMarker( VIDL_vhBeginMarker* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto cmdList = vhCmdListGet();
    if ( cmdList )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdList->beginMarker( cmd->name.c_str() );
    }
}

void vhCmdBackendState::Handle_vhEndMarker( VIDL_vhEndMarker* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto cmdList = vhCmdListGet();
    if ( cmdList )
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdList->endMarker();
    }
}

void vhCmdBackendState::Handle_vhCaptureStart( VIDL_vhCaptureStart* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( g_vhRenderDoc ) g_vhRenderDoc->StartFrameCapture( NULL, NULL );
}

void vhCmdBackendState::Handle_vhCaptureEnd( VIDL_vhCaptureEnd* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    // Flush commands to GPU so they are submitted within the capture window.
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        vhCmdListFlushAll_DeviceStateLocked();
    }

    if ( g_vhRenderDoc ) g_vhRenderDoc->EndFrameCapture( NULL, NULL );
}

void vhCmdBackendState::Handle_vhResetTexture( VIDL_vhResetTexture* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->texture == VRHI_INVALID_HANDLE )
    {
        return;
    }

    // Ensure entry exists to make subsequent Destroy/Update safe
    if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
    {
        backendTextures[cmd->texture] = std::make_unique< vhBackendTexture >();
    }
}

void vhCmdBackendState::Handle_vhResetBuffer( VIDL_vhResetBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->buffer == VRHI_INVALID_HANDLE )
    {
        return;
    }

    // Ensure entry exists to make subsequent Destroy/Update safe
    if ( backendBuffers.find( cmd->buffer ) == backendBuffers.end() )
    {
        backendBuffers[cmd->buffer] = std::make_unique< vhBackendBuffer >();
    }
}

void vhCmdBackendState::Handle_vhDestroyTexture( VIDL_vhDestroyTexture* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->texture == VRHI_INVALID_HANDLE )
    {
        return;
    }

    if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
    {
        VRHI_ERR( "vhDestroyTexture() : Texture %d not found!\n", cmd->texture );
        return;
    }

    // Destroy texture by releasing our reference. NVRHI handles GPU destruction safety.
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backendTextures.erase( cmd->texture );
    }
}

void vhCmdBackendState::Handle_vhCreateTexture( VIDL_vhCreateTexture* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->texture == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateTexture() : Invalid texture handle!\n" );
        return;
    }
    if ( cmd->dimensions.x <= 0 || cmd->dimensions.y <= 0 || cmd->dimensions.z <= 0 ||
        cmd->numMips <= 0 || cmd->numLayers <= 0 || cmd->format == nvrhi::Format::UNKNOWN )
    {
        VRHI_ERR( "vhCreateTexture() : Invalid parameters! TexID %u %d x %d x %d mips %d layers %d format %d (%s)\n",
            cmd->texture, cmd->dimensions.x, cmd->dimensions.y, cmd->dimensions.z, cmd->numMips, cmd->numLayers, ( int ) cmd->format, nvrhi::getFormatInfo( cmd->format ).name );
        return;
    }

    // Create the NVRHI texture.
    if ( !cmd->name )
    {
        snprintf( temps, sizeof( temps ), "Texture %d", cmd->texture );
    }
    const char* debugName = cmd->name ? cmd->name : temps;
    auto textureDesc = nvrhi::TextureDesc()
        .setDimension( cmd->target )
        .setWidth( cmd->dimensions.x )
        .setHeight( cmd->dimensions.y )
        .setDepth( cmd->dimensions.z )
        .setFormat( cmd->format )
        .setMipLevels( cmd->numMips )
        .setArraySize( cmd->numLayers )
        .setIsRenderTarget( ( cmd->flag & VRHI_TEXTURE_RT ) != 0 )
        .setIsUAV( ( cmd->flag & VRHI_TEXTURE_COMPUTE_WRITE ) != 0 )
        .setIsVirtual( ( cmd->flag & VRHI_TEXTURE_VIRTUAL ) != 0 )
        .enableAutomaticStateTracking( nvrhi::ResourceStates::ShaderResource )
        .setDebugName( debugName );

    nvrhi::TextureHandle texture = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        texture = g_vhDevice->createTexture( textureDesc );
    }
    if ( !texture )
    {
        VRHI_ERR( "vhCreateTexture() : Failed to create texture!\n" );
        return;
    }

    // Calculate metadata for the texture.
    auto btex = std::make_unique< vhBackendTexture >();
    btex->handle = texture;
    btex->name = temps;
    btex->info.target = cmd->target;
    btex->info.dimensions = cmd->dimensions;
    btex->info.format = cmd->format;
    btex->info.mipLevels = cmd->numMips;
    btex->info.arrayLayers = cmd->numLayers;
    btex->flags = cmd->flag;
    vhTextureMiplevelInfo( btex->mipInfo, btex->pitchSize, btex->arraySize, btex->info );

    if ( cmd->data )
    {
        BE_UpdateTexture( *btex, cmd->data );
    }

    backendTextures[cmd->texture] = std::move( btex );
}

void vhCmdBackendState::Handle_vhUpdateTexture( VIDL_vhUpdateTexture* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->texture == VRHI_INVALID_HANDLE )
    {
        return;
    }

    auto it = backendTextures.find( cmd->texture );
    if ( it == backendTextures.end() )
    {
        VRHI_ERR( "vhUpdateTexture() : Texture %d not found!\n", cmd->texture );
        return;
    }
    auto& btex = *it->second;

    // Resolve VRHI_MIPMAP_COMPLETE to remaining mips count.
    int32_t resolvedNumMips = cmd->numMips;
    if ( cmd->numMips == VRHI_MIPMAP_COMPLETE )
    {
        resolvedNumMips = ( int32_t ) btex.info.mipLevels - cmd->startMips;
    }

    // Calculate expected data size for range.
    int32_t mipStart = cmd->startMips, mipEnd = cmd->startMips + resolvedNumMips;
    int32_t layerStart = cmd->startLayers, layerEnd = cmd->startLayers + cmd->numLayers;

    // Validation: range must be within texture limits.
    if ( mipStart < 0 || mipEnd >( int32_t ) btex.info.mipLevels ||
        layerStart < 0 || layerEnd >( int32_t ) btex.info.arrayLayers )
    {
        VRHI_ERR( "vhUpdateTexture(): Update range out of bounds.\n" );
        return;
    }

    // Calculate total size of mips in the range for one layer.
    int64_t totalLayerSize = 0;
    for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
    {
        totalLayerSize += btex.mipInfo[mip].size;
    }

    int64_t expectedSize = totalLayerSize * cmd->numLayers;
    if ( ( int64_t ) cmd->data->size() < expectedSize )
    {
        VRHI_ERR( "vhUpdateTexture(): Data size %llu is too small for update range, expected %llu\n", ( uint64_t ) cmd->data->size(), ( uint64_t ) expectedSize );
        return;
    }

    glm::ivec4 range = glm::ivec4( cmd->startMips, cmd->startMips + resolvedNumMips, cmd->startLayers, cmd->startLayers + cmd->numLayers );
    BE_UpdateTexture( btex, cmd->data, range );
}

void vhCmdBackendState::Handle_vhReadTextureSlow( VIDL_vhReadTextureSlow* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    // NO dataRAII here - outData is owned by the caller.

    if ( cmd->texture == VRHI_INVALID_HANDLE )
    {
        return;
    }

    if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
    {
        VRHI_ERR( "vhReadTextureSlow() : Texture %d not found!\n", cmd->texture );
        return;
    }

    auto& btex = *backendTextures[cmd->texture];
    if ( btex.info.target == nvrhi::TextureDimension::Texture3D )
    {
        VRHI_ERR( "vhReadTextureSlow() : 3D textures are not supported for readback yet!\n" );
        return;
    }

    BE_ReadTextureSlow( btex, cmd->outData, cmd->mip, cmd->layer );
}

void vhCmdBackendState::Handle_vhBlitTexture( VIDL_vhBlitTexture* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );

    if ( cmd->dst == VRHI_INVALID_HANDLE || cmd->src == VRHI_INVALID_HANDLE )
    {
        return;
    }

    auto itDst = backendTextures.find( cmd->dst );
    auto itSrc = backendTextures.find( cmd->src );
    if ( itDst == backendTextures.end() || itSrc == backendTextures.end() )
    {
        VRHI_ERR( "vhBlitTexture() : Texture handle(s) %d or %d not found!\n", cmd->dst, cmd->src );
        return;
    }
    auto& bdst = *itDst->second;
    auto& bsrc = *itSrc->second;

    glm::ivec3 extent = cmd->extent;
    if ( extent.x <= 0 || extent.y <= 0 )
    {
        if ( cmd->srcMip >= 0 && cmd->srcMip < ( int ) bsrc.mipInfo.size() )
            extent = bsrc.mipInfo[cmd->srcMip].dimensions;
    }

    if ( cmd->srcMip < 0 || cmd->srcMip >= ( int ) bsrc.mipInfo.size() )
    {
        VRHI_ERR( "vhBlitTexture: srcMip %d out of range (0..%d)\n", cmd->srcMip, ( int ) bsrc.mipInfo.size() - 1 );
        return;
    }
    if ( !vhVerifyRegionInTexture( vhGetFormat( bsrc.info.format ), bsrc.mipInfo[cmd->srcMip].dimensions, cmd->srcOffset, extent, "vhBlitTexture Source" ) )
    {
        return;
    }

    if ( cmd->dstMip < 0 || cmd->dstMip >= ( int ) bdst.mipInfo.size() )
    {
        VRHI_ERR( "vhBlitTexture: dstMip %d out of range (0..%d)\n", cmd->dstMip, ( int ) bdst.mipInfo.size() - 1 );
        return;
    }
    if ( !vhVerifyRegionInTexture( vhGetFormat( bdst.info.format ), bdst.mipInfo[cmd->dstMip].dimensions, cmd->dstOffset, extent, "vhBlitTexture Dest" ) )
    {
        return;
    }

    BE_BlitTexture( bdst, bsrc, cmd->dstMip, cmd->srcMip, cmd->dstLayer, cmd->srcLayer, cmd->dstOffset, cmd->srcOffset, extent );
}

vhBackendBuffer* vhCmdBackendState::Handle_vhCreateBufferCommon_Internal( const char* fn, vhBuffer buffer, nvrhi::BufferDesc& desc, const char* name, const char* autoname,
    const vhMem* data, uint64_t count, uint64_t stride, uint64_t flags )
{
    if ( buffer == VRHI_INVALID_HANDLE ) return nullptr;

    if ( backendBuffers.find( buffer ) != backendBuffers.end() && backendBuffers[buffer]->handle )
    {
        VRHI_ERR( "%s() : Buffer %d already exists!\n", fn, buffer );
        return nullptr;
    }

    uint64_t byteSize = 0;
    if ( data )
    {
        byteSize = data->size();
    }
    else
    {
        if ( count == 0 )
        {
            VRHI_ERR( "%s() : Memory bhandle is empty/null AND count is 0!\n", fn );
            return nullptr;
        }
        byteSize = count * stride;
    }

    // Create the NVRHI bhandle
    if ( !name || !name[0] ) snprintf( temps, sizeof( temps ), "%s %d", autoname, buffer );
    auto bufferDesc = desc
        .setByteSize( byteSize )
        .setCanHaveUAVs( !!( flags & VRHI_BUFFER_COMPUTE_WRITE ) )
        .setCanHaveTypedViews( !!( flags & VRHI_BUFFER_COMPUTE_READ ) )
        .setCanHaveRawViews( !!( flags & VRHI_BUFFER_COMPUTE_READ ) )
        .setIsDrawIndirectArgs( !!( flags & VRHI_BUFFER_DRAW_INDIRECT ) )
        .setIsVirtual( !!( flags & VRHI_BUFFER_VIRTUAL ) )
        .setIsAccelStructBuildInput( !!( flags & VRHI_BUFFER_ACCEL_INPUT ) )
        .setDebugName( ( name && name[0] ) ? name : temps );

    nvrhi::BufferHandle bhandle = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        bhandle = g_vhDevice->createBuffer( bufferDesc );
    }

    if ( !bhandle )
    {
        VRHI_ERR( "%s() : Failed to create bhandle!\n", fn );
        return nullptr;
    }

    auto bbuf = std::make_unique< vhBackendBuffer >();
    bbuf->handle = bhandle;
    bbuf->name = ( name && name[0] ) ? name : temps;
    bbuf->desc = bufferDesc;
    bbuf->stride = ( uint32_t ) stride;
    bbuf->flags = flags;

    if ( data )
    {
        BE_UpdateBuffer( *bbuf, 0, data );
    }

    backendBuffers[buffer] = std::move( bbuf );
    return backendBuffers[buffer].get();
}

void vhCmdBackendState::Handle_vhUpdateBufferCommon_Internal( const char* fn, vhBuffer buffer, uint64_t offsetElements, const vhMem* data, uint64_t count, bool isVertexBuffer )
{
    if ( buffer == VRHI_INVALID_HANDLE ) return;

    if ( backendBuffers.find( buffer ) == backendBuffers.end() )
    {
        VRHI_ERR( "%s() : Buffer %d not found!\n", fn, buffer );
        return;
    }
    auto& bbuf = backendBuffers[buffer];

    // Convert element offset to byte offset
    uint64_t byteOffset = 0;
    if ( isVertexBuffer )
    {
        byteOffset = offsetElements * bbuf->stride;
    }
    else
    {
        // Index buffer - determine index size from flags
        uint64_t indexSize = ( bbuf->flags & VRHI_BUFFER_INDEX32 ) ? sizeof( uint32_t ) : sizeof( uint16_t );
        byteOffset = offsetElements * indexSize;
    }

    if ( data )
    {
        if ( byteOffset + data->size() > bbuf->desc.byteSize && !( bbuf->flags & VRHI_BUFFER_ALLOW_RESIZE ) )
        {
            VRHI_ERR( "%s() : Update range [%llu, %llu] exceeds buffer size %llu!\n",
                fn, byteOffset, byteOffset + data->size(), bbuf->desc.byteSize );
            return;
        }
        BE_UpdateBuffer( *bbuf, byteOffset, data );
    }
    else if ( count > 0 )
    {
        if ( !( bbuf->flags & VRHI_BUFFER_ALLOW_RESIZE ) )
        {
            VRHI_ERR( "%s() : resize requested but buffer does not have ALLOW_RESIZE flag!\n", fn );
            return;
        }
        BE_ResizeBuffer( *bbuf, count * bbuf->stride );
    }
    else
    {
        VRHI_ERR( "%s() : Both data and count are null/zero.\n", fn );
    }
}

void vhCmdBackendState::Handle_vhCreateVertexBuffer( VIDL_vhCreateVertexBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->buffer == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateVertexBuffer() : Invalid bhandle handle!\n" );
        return;
    }

    std::vector< vhVertexLayoutDef > layoutDefs;
    if ( !vhParseVertexLayoutInternal( cmd->layout, layoutDefs ) )
    {
        VRHI_ERR( "vhCreateVertexBuffer() : Invalid vertex layout!\n" );
        return;
    }
    uint32_t stride = ( uint32_t ) vhVertexLayoutDefSize( layoutDefs );
    if ( stride == 0 )
    {
        VRHI_ERR( "vhCreateVertexBuffer() : Vertex layout has 0 size!\n" );
        return;
    }

    // Partially initialise nvrhi::BufferDesc
    nvrhi::BufferDesc desc;
    desc.setIsVertexBuffer( true );
    desc.enableAutomaticStateTracking( nvrhi::ResourceStates::VertexBuffer );

    auto bbuf = Handle_vhCreateBufferCommon_Internal( "vhCreateVertexBuffer", cmd->buffer, desc, cmd->name, "VertexBuffer", cmd->data, cmd->numVerts, stride, cmd->flags );
    if ( !bbuf ) return;
    bbuf->layout = layoutDefs;
}

void vhCmdBackendState::Handle_vhUpdateVertexBuffer( VIDL_vhUpdateVertexBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    Handle_vhUpdateBufferCommon_Internal( "vhUpdateVertexBuffer", cmd->buffer, cmd->offsetVerts, cmd->data, cmd->numVerts, true );
}

void vhCmdBackendState::Handle_vhCreateIndexBuffer( VIDL_vhCreateIndexBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->buffer == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateIndexBuffer() : Invalid bhandle handle!\n" );
        return;
    }

    // Partially initialise nvrhi::BufferDesc
    nvrhi::BufferDesc desc;
    desc.setIsIndexBuffer( true );
    desc.enableAutomaticStateTracking( nvrhi::ResourceStates::IndexBuffer );
    uint64_t stride = cmd->flags & VRHI_BUFFER_INDEX32 ? sizeof( uint32_t ) : sizeof( uint16_t );

    Handle_vhCreateBufferCommon_Internal( "vhCreateIndexBuffer", cmd->buffer, desc, cmd->name, "IndexBuffer", cmd->data, cmd->numIndices, stride, cmd->flags );
}

void vhCmdBackendState::Handle_vhUpdateIndexBuffer( VIDL_vhUpdateIndexBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    Handle_vhUpdateBufferCommon_Internal( "vhUpdateIndexBuffer", cmd->buffer, cmd->offsetIndices, cmd->data, cmd->numIndices, false );
}

void vhCmdBackendState::Handle_vhCreateUniformBuffer( VIDL_vhCreateUniformBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->buffer == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateUniformBuffer() : Invalid buffer handle!\n" );
        return;
    }

    // Partially initialise nvrhi::BufferDesc for uniform buffer
    nvrhi::BufferDesc desc;
    desc.setIsConstantBuffer( true );
    desc.enableAutomaticStateTracking( nvrhi::ResourceStates::ConstantBuffer );

    // Reuse common logic with stride = 1 (byte-oriented)
    Handle_vhCreateBufferCommon_Internal( "vhCreateUniformBuffer", cmd->buffer, desc, cmd->name, "UniformBuffer", cmd->data, cmd->size, 1, cmd->flags );
}

void vhCmdBackendState::Handle_vhUpdateUniformBuffer( VIDL_vhUpdateUniformBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    // Reuse common update logic (isVertexBuffer = true uses stride from creation)
    Handle_vhUpdateBufferCommon_Internal( "vhUpdateUniformBuffer", cmd->buffer, cmd->offset, cmd->data, cmd->size, true );
}

void vhCmdBackendState::Handle_vhCreateStorageBuffer( VIDL_vhCreateStorageBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->buffer == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateStorageBuffer() : Invalid buffer handle!\n" );
        return;
    }

    // Partially initialise nvrhi::BufferDesc for storage buffer
    nvrhi::BufferDesc desc;
    desc.setByteSize( cmd->size );
    desc.setCanHaveUAVs( true );
    desc.setCanHaveRawViews( true );
    desc.setStructStride( cmd->stride );
    desc.setFormat( cmd->format );
    desc.setCanHaveTypedViews( cmd->format != nvrhi::Format::UNKNOWN );

    desc.enableAutomaticStateTracking( nvrhi::ResourceStates::UnorderedAccess );

    // Reuse common logic with stride = 1 (byte-oriented)
    Handle_vhCreateBufferCommon_Internal( "vhCreateStorageBuffer", cmd->buffer, desc, cmd->name, "StorageBuffer", cmd->data, cmd->size, 1, cmd->flags );
}

void vhCmdBackendState::Handle_vhUpdateStorageBuffer( VIDL_vhUpdateStorageBuffer* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    // Reuse common update logic (isVertexBuffer = true uses stride from creation)
    Handle_vhUpdateBufferCommon_Internal( "vhUpdateStorageBuffer", cmd->buffer, cmd->offset, cmd->data, cmd->size, true );
}

void vhCmdBackendState::Handle_vhCreateHeap( VIDL_vhCreateHeap* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateHeap(): Invalid heap handle\n" );
        return;
    }

    // Check if heap already exists
    if ( backendHeaps.find( cmd->heap ) != backendHeaps.end() )
    {
        VRHI_ERR( "vhCreateHeap(): Heap %d already exists\n", cmd->heap );
        return;
    }

    if ( cmd->size > UINT32_MAX )
    {
        VRHI_ERR( "vhCreateHeap(): Size %llu exceeds maximum supported size %u\n", cmd->size, UINT32_MAX );
        return;
    }

    nvrhi::HeapDesc desc;
    desc.capacity = cmd->size;
    desc.type = nvrhi::HeapType::DeviceLocal;
    desc.debugName = ( cmd->name && cmd->name[0] ) ? cmd->name : nullptr;

    nvrhi::HeapHandle handle = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        handle = g_vhDevice->createHeap( desc );
    }
    if ( !handle )
    {
        VRHI_ERR( "vhCreateHeap(): Failed to create heap of size %llu\n", cmd->size );
        return;
    }

    auto heap = std::make_unique< vhBackendHeap >();
    heap->handle = handle;
    heap->desc = desc;
    heap->allocator = new OffsetAllocator::Allocator( static_cast< uint32_t >( desc.capacity ) );

    backendHeaps[cmd->heap] = std::move( heap );
}

void vhCmdBackendState::Handle_vhDestroyHeap( VIDL_vhDestroyHeap* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->heap == VRHI_INVALID_HANDLE )
    {
        return;
    }

    auto it = backendHeaps.find( cmd->heap );
    if ( it == backendHeaps.end() )
    {
        VRHI_ERR( "vhDestroyHeap(): Heap %d not found\n", cmd->heap );
        return;
    }

    if ( it->second->allocator )
    {
        delete static_cast< OffsetAllocator::Allocator* >( it->second->allocator );
        it->second->allocator = nullptr;
        it->second->allocations.clear();
    }

    backendHeaps.erase( it );
}

void vhCmdBackendState::Handle_vhBindTextureMemory( VIDL_vhBindTextureMemory* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->texture == VRHI_INVALID_HANDLE || cmd->heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhBindTextureMemory(): Invalid texture or heap handle\n" );
        return;
    }

    auto texIt = backendTextures.find( cmd->texture );
    if ( texIt == backendTextures.end() || !texIt->second->handle )
    {
        VRHI_ERR( "vhBindTextureMemory(): Texture %d not found or invalid\n", cmd->texture );
        return;
    }
    if ( ( texIt->second->flags & VRHI_TEXTURE_VIRTUAL ) == 0 )
    {
        VRHI_ERR( "vhBindTextureMemory(): Texture %d is not virtual\n", cmd->texture );
        return;
    }

    auto heapIt = backendHeaps.find( cmd->heap );
    if ( heapIt == backendHeaps.end() || !heapIt->second->handle )
    {
        VRHI_ERR( "vhBindTextureMemory(): Heap %d not found or invalid\n", cmd->heap );
        return;
    }

    if ( cmd->offset >= heapIt->second->desc.capacity )
    {
        VRHI_ERR( "vhBindTextureMemory(): Offset %llu exceeds heap %d capacity %llu\n", cmd->offset, cmd->heap, heapIt->second->desc.capacity );
        return;
    }

    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
    if ( !g_vhDevice->bindTextureMemory( texIt->second->handle, heapIt->second->handle, cmd->offset ) )
    {
        VRHI_ERR( "vhBindTextureMemory(): Failed to bind texture %d to heap %d at offset %llu\n", cmd->texture, cmd->heap, cmd->offset );
    }
}

void vhCmdBackendState::Handle_vhBindBufferMemory( VIDL_vhBindBufferMemory* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->buffer == VRHI_INVALID_HANDLE || cmd->heap == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhBindBufferMemory(): Invalid buffer or heap handle\n" );
        return;
    }

    auto bufIt = backendBuffers.find( cmd->buffer );
    if ( bufIt == backendBuffers.end() || !bufIt->second->handle )
    {
        VRHI_ERR( "vhBindBufferMemory(): Buffer %d not found or invalid\n", cmd->buffer );
        return;
    }
    if ( ( bufIt->second->flags & VRHI_BUFFER_VIRTUAL ) == 0 )
    {
        VRHI_ERR( "vhBindBufferMemory(): Buffer %d is not virtual\n", cmd->buffer );
        return;
    }

    auto heapIt = backendHeaps.find( cmd->heap );
    if ( heapIt == backendHeaps.end() || !heapIt->second->handle )
    {
        VRHI_ERR( "vhBindBufferMemory(): Heap %d not found or invalid\n", cmd->heap );
        return;
    }

    if ( cmd->offset >= heapIt->second->desc.capacity )
    {
        VRHI_ERR( "vhBindBufferMemory(): Offset %llu exceeds heap %d capacity %llu\n", cmd->offset, cmd->heap, heapIt->second->desc.capacity );
        return;
    }

    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
    if ( !g_vhDevice->bindBufferMemory( bufIt->second->handle, heapIt->second->handle, cmd->offset ) )
    {
        VRHI_ERR( "vhBindBufferMemory(): Failed to bind buffer %d to heap %d at offset %llu\n", cmd->buffer, cmd->heap, cmd->offset );
    }
}

void vhCmdBackendState::Handle_vhDestroyBuffer( VIDL_vhDestroyBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->buffer == VRHI_INVALID_HANDLE )
    {
        return;
    }

    if ( backendBuffers.find( cmd->buffer ) == backendBuffers.end() )
    {
        VRHI_ERR( "vhDestroyBuffer() : Buffer %d not found!\n", cmd->buffer );
        return;
    }

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backendBuffers.erase( cmd->buffer );
    }
}

void vhCmdBackendState::Handle_vhCreateShader( VIDL_vhCreateShader* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( cmd->shader == VRHI_INVALID_HANDLE ) return;

    nvrhi::ShaderType type = nvrhi::ShaderType::None;
    uint64_t stage = cmd->flags & VRHI_SHADER_STAGE_MASK;
    switch ( stage )
    {
        case VRHI_SHADER_STAGE_VERTEX:        type = nvrhi::ShaderType::Vertex; break;
        case VRHI_SHADER_STAGE_HULL:          type = nvrhi::ShaderType::Hull; break;
        case VRHI_SHADER_STAGE_DOMAIN:        type = nvrhi::ShaderType::Domain; break;
        case VRHI_SHADER_STAGE_GEOMETRY:      type = nvrhi::ShaderType::Geometry; break;
        case VRHI_SHADER_STAGE_PIXEL:         type = nvrhi::ShaderType::Pixel; break;
        case VRHI_SHADER_STAGE_COMPUTE:       type = nvrhi::ShaderType::Compute; break;
        case VRHI_SHADER_STAGE_RAYGEN:        type = nvrhi::ShaderType::RayGeneration; break;
        case VRHI_SHADER_STAGE_MISS:          type = nvrhi::ShaderType::Miss; break;
        case VRHI_SHADER_STAGE_CLOSEST_HIT:   type = nvrhi::ShaderType::ClosestHit; break;
        case VRHI_SHADER_STAGE_ANY_HIT:       type = nvrhi::ShaderType::AnyHit; break;
        case VRHI_SHADER_STAGE_INTERSECTION:  type = nvrhi::ShaderType::Intersection; break;
        case VRHI_SHADER_STAGE_CALLABLE:      type = nvrhi::ShaderType::Callable; break;
        case VRHI_SHADER_STAGE_MESH:          type = nvrhi::ShaderType::Mesh; break;
        case VRHI_SHADER_STAGE_AMPLIFICATION: type = nvrhi::ShaderType::Amplification; break;
    }

    if ( type == nvrhi::ShaderType::None )
    {
        VRHI_ERR( "vhCreateShader() : Invalid shader stage flags: %llu\n", cmd->flags );
        return;
    }

    // Reflection
    static_assert( sizeof( nvrhi::VulkanBindingOffsets ) == 4 * sizeof( uint32_t ) );
    nvrhi::BindingLayoutDesc layoutDesc = { .bindingOffsets = { 0, 0, 0, 0 } };
    std::vector< vhShaderReflectionResource > resources;
    glm::uvec3 groupSize = { 0,0,0 };
    std::vector< vhPushConstantRange > pushConstants;
    std::vector< vhVertexLayoutDef > inputLayout;

    // Perform reflection
    vhReflectSpirv( cmd->spirv, layoutDesc, resources, groupSize, pushConstants, &inputLayout );

    // Cache member hashes for resources that have members (avoids per-frame recomputation)
    for ( auto& res : resources )
    {
        if ( !res.members.empty() )
        {
            res.membersHash = vhHashReflectionMembers( res.members );
        }
    }

    // Set visibility based on shader stage.
    layoutDesc.visibility = type;

    // Create Shader via NVRHI. We actually unique hash into debug name, for which we will rely on for PSO hash later.
    nvrhi::ShaderDesc desc( type );
    desc.entryName = cmd->entry;
    desc.debugName = cmd->name;
    desc.debugName += " # " + std::to_string( vhHashShaderSPIRV( cmd->spirv ) );
    nvrhi::ShaderHandle handle = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        handle = g_vhDevice->createShader( desc, cmd->spirv.data(), cmd->spirv.size() * sizeof( uint32_t ) );
    }

    // Store in Backend Map
    if ( handle )
    {
        auto backendShader = std::make_unique< vhBackendShader >();
        backendShader->name = cmd->name;
        backendShader->handle = handle;
        backendShader->flags = cmd->flags;
        backendShader->entry = cmd->entry;
        backendShader->reflection = std::move( resources );
        backendShader->reflectionNameHashes.reserve( backendShader->reflection.size() );
        for ( size_t reflectionIdx = 0; reflectionIdx < backendShader->reflection.size(); ++reflectionIdx )
        {
            const auto& reflection = backendShader->reflection[ reflectionIdx ];
            backendShader->reflectionNameHashes.push_back( reflection.name.empty() ? 0 : komihash( reflection.name.data(), reflection.name.size(), 0 ) );
        }
        backendShader->inputLayout = std::move( inputLayout );
        backendShader->threadGroupSize = groupSize;
        backendShader->pushConstants = std::move( pushConstants );
        backendShader->layoutDesc = layoutDesc;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            backendShader->layout = g_vhDevice->createBindingLayout( layoutDesc );
            backendShaders[cmd->shader] = nullptr;
        }
        assert( backendShader->layout );
        backendShaders[cmd->shader] = std::move( backendShader );
    }
    else
    {
        VRHI_ERR( "Failed to create shader: %s\n", cmd->name );
    }
}

void vhCmdBackendState::Handle_vhDestroyShader( VIDL_vhDestroyShader* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->shader == VRHI_INVALID_HANDLE )
    {
        return;
    }

    if ( backendShaders.find( cmd->shader ) == backendShaders.end() )
    {
        VRHI_ERR( "vhDestroyShader() : Shader %d not found!\n", cmd->shader );
        return;
    }

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backendShaders.erase( cmd->shader );
    }
}

void vhCmdBackendState::Handle_vhCreateAS( VIDL_vhCreateAS* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( cmd->as == VRHI_INVALID_HANDLE ) return;

    if ( backendAccelStructs.find( cmd->as ) == backendAccelStructs.end() )
    {
        backendAccelStructs[ cmd->as ] = std::make_unique< vhBackendAccelStruct >();
    }

    auto backend = backendAccelStructs[ cmd->as ].get();
    backend->desc = cmd->desc;

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backend->handle = g_vhDevice->createAccelStruct( backend->desc );
        if ( !backend->handle )
        {
            VRHI_ERR( "vhCreateAS() : Failed to create acceleration structure!\n" );
        }
    }
}

void vhCmdBackendState::Handle_vhDestroyAS( VIDL_vhDestroyAS* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( backendAccelStructs.find( cmd->as ) == backendAccelStructs.end() )
    {
        VRHI_ERR( "vhDestroyAS() : AccelStruct %d not found!\n", cmd->as );
        return;
    }

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backendAccelStructs.erase( cmd->as );
    }
}

void vhCmdBackendState::Handle_vhBuildBLAS( VIDL_vhBuildBLAS* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendAccelStructs.find( cmd->blas );
    if ( it == backendAccelStructs.end() )
    {
        VRHI_ERR( "vhBuildBLAS() : BLAS %d not found!\n", cmd->blas );
        return;
    }

    auto backend = it->second.get();
    if ( !backend->handle )
    {
        VRHI_ERR( "vhBuildBLAS() : BLAS %d has no valid handle!\n", cmd->blas );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdlist->buildBottomLevelAccelStruct( backend->handle, cmd->geometries.data(), cmd->geometries.size(), backend->desc.buildFlags );
    }
}

void vhCmdBackendState::Handle_vhBuildTLAS( VIDL_vhBuildTLAS* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendAccelStructs.find( cmd->tlas );
    if ( it == backendAccelStructs.end() )
    {
        VRHI_ERR( "vhBuildTLAS() : TLAS %d not found!\n", cmd->tlas );
        return;
    }

    auto backend = it->second.get();
    if ( !backend->handle )
    {
        VRHI_ERR( "vhBuildTLAS() : TLAS %d has no valid handle!\n", cmd->tlas );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        nvrhi::rt::AccelStructBuildFlags flags = backend->desc.buildFlags | cmd->buildFlags;
        cmdlist->buildTopLevelAccelStruct( backend->handle, cmd->instances.data(), cmd->instances.size(), flags );
    }
}


void vhCmdBackendState::Handle_vhCompactBLAS( VIDL_vhCompactBLAS* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdlist->compactBottomLevelAccelStructs();
    }
}

void vhCmdBackendState::Handle_vhBuildTLASFromBuffer( VIDL_vhBuildTLASFromBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendAccelStructs.find( cmd->tlas );
    if ( it == backendAccelStructs.end() )
    {
        VRHI_ERR( "vhBuildTLASFromBuffer() : TLAS %d not found!\n", cmd->tlas );
        return;
    }

    auto backend = it->second.get();
    if ( !backend->handle )
    {
        VRHI_ERR( "vhBuildTLASFromBuffer() : TLAS %d has no valid handle!\n", cmd->tlas );
        return;
    }

    auto bufIt = backendBuffers.find( cmd->instanceBuffer );
    if ( bufIt == backendBuffers.end() )
    {
        VRHI_ERR( "vhBuildTLASFromBuffer() : Buffer %d not found!\n", cmd->instanceBuffer );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        cmdlist->buildTopLevelAccelStructFromBuffer( backend->handle, bufIt->second->handle.Get(), 0, cmd->numInstances, backend->desc.buildFlags );
    }
}

void vhCmdBackendState::Handle_vhCreateRTPipeline( VIDL_vhCreateRTPipeline* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( cmd->pipeline == VRHI_INVALID_HANDLE ) return;

    if ( backendRTPipelines.find( cmd->pipeline ) == backendRTPipelines.end() )
    {
        backendRTPipelines[ cmd->pipeline ] = std::make_unique< vhBackendRTPipeline >();
    }

    auto backend = backendRTPipelines[ cmd->pipeline ].get();
    backend->desc = cmd->desc;

    // Check for duplicate export names across shaders and hit groups.
    {
        std::set< std::string > seen;
        for ( const auto& s : backend->desc.shaders )
        {
            if ( !s.exportName.empty() && !seen.insert( s.exportName ).second )
            {
                VRHI_ERR( "vhCreateRTPipeline() : Duplicate export name '%s'\n", s.exportName.c_str() );
                return;
            }
        }
        for ( const auto& hg : backend->desc.hitGroups )
        {
            if ( !hg.exportName.empty() && !seen.insert( hg.exportName ).second )
            {
                VRHI_ERR( "vhCreateRTPipeline() : Duplicate export name '%s'\n", hg.exportName.c_str() );
                return;
            }
        }
    }

    // Auto-derive globalBindingLayouts from the RT shaders' reflected layouts if the caller did
    // not supply any. Holes between stage descriptor sets are filled with the shared empty layout
    // so that descriptor set compatibility holds for stages with no resources.
    if ( backend->desc.globalBindingLayouts.empty() )
    {
        auto fnFindShader = [this]( nvrhi::IShader* needle ) -> vhBackendShader*
        {
            if ( !needle ) return nullptr;
            for ( auto& kv : backendShaders )
            {
                if ( kv.second && kv.second->handle.Get() == needle ) return kv.second.get();
            }
            return nullptr;
        };

        std::vector< vhBackendShader* > rtShaders;
        for ( const auto& s : backend->desc.shaders )
        {
            if ( auto* bs = fnFindShader( s.shader.Get() ) ) rtShaders.push_back( bs );
        }
        for ( const auto& hg : backend->desc.hitGroups )
        {
            if ( auto* bs = fnFindShader( hg.closestHitShader.Get() ) ) rtShaders.push_back( bs );
            if ( auto* bs = fnFindShader( hg.anyHitShader.Get() ) ) rtShaders.push_back( bs );
            if ( auto* bs = fnFindShader( hg.intersectionShader.Get() ) ) rtShaders.push_back( bs );
        }

        backend->desc.globalBindingLayouts = BE_Util_BuildStageIndexedLayouts( rtShaders.data(), ( int ) rtShaders.size(), false, false, true );
    }

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backend->handle = g_vhDevice->createRayTracingPipeline( backend->desc );
        if ( !backend->handle )
        {
            VRHI_ERR( "vhCreateRTPipeline() : Failed to create raytracing pipeline!\n" );
        }
    }
}

void vhCmdBackendState::Handle_vhCreateRTPipelineSimple( VIDL_vhCreateRTPipelineSimple* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( cmd->pipeline == VRHI_INVALID_HANDLE ) return;
    if ( cmd->rayGen == VRHI_INVALID_HANDLE || cmd->miss == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateRTPipeline() : rayGen and miss shaders are required\n" );
        return;
    }

    auto fnResolve = [this]( vhShader h ) -> vhBackendShader*
    {
        if ( h == VRHI_INVALID_HANDLE ) return nullptr;
        auto it = backendShaders.find( h );
        return ( it != backendShaders.end() && it->second ) ? it->second.get() : nullptr;
    };

    vhBackendShader* bsRayGen = fnResolve( cmd->rayGen );
    vhBackendShader* bsMiss = fnResolve( cmd->miss );
    vhBackendShader* bsClosestHit = fnResolve( cmd->closestHit );
    vhBackendShader* bsAnyHit = fnResolve( cmd->anyHit );
    vhBackendShader* bsIntersection = fnResolve( cmd->intersection );

    if ( !bsRayGen || !bsRayGen->handle )
    {
        VRHI_ERR( "vhCreateRTPipeline() : rayGen shader not found or invalid\n" );
        return;
    }
    if ( !bsMiss || !bsMiss->handle )
    {
        VRHI_ERR( "vhCreateRTPipeline() : miss shader not found or invalid\n" );
        return;
    }

    if ( backendRTPipelines.find( cmd->pipeline ) == backendRTPipelines.end() )
    {
        backendRTPipelines[ cmd->pipeline ] = std::make_unique< vhBackendRTPipeline >();
    }
    auto backend = backendRTPipelines[ cmd->pipeline ].get();

    // Build the NVRHI pipeline descriptor with fixed export names.
    nvrhi::rt::PipelineDesc pipeDesc;
    {
        nvrhi::rt::PipelineShaderDesc rg;
        rg.shader = bsRayGen->handle;
        rg.exportName = "raygen";
        nvrhi::rt::PipelineShaderDesc ms;
        ms.shader = bsMiss->handle;
        ms.exportName = "miss";
        pipeDesc.shaders = { rg, ms };
    }
    if ( bsClosestHit && bsClosestHit->handle )
    {
        nvrhi::rt::PipelineHitGroupDesc hg;
        hg.exportName = "hg";
        hg.closestHitShader = bsClosestHit->handle;
        hg.anyHitShader = bsAnyHit ? bsAnyHit->handle : nullptr;
        hg.intersectionShader = bsIntersection ? bsIntersection->handle : nullptr;
        if ( bsIntersection ) hg.isProceduralPrimitive = true;
        pipeDesc.hitGroups = { hg };
    }
    pipeDesc.maxPayloadSize = cmd->maxPayloadSize;
    pipeDesc.maxAttributeSize = cmd->maxAttributeSize;
    pipeDesc.maxRecursionDepth = cmd->maxRecursionDepth;
    backend->desc = pipeDesc;

    // Store export names for shader table auto-population.
    backend->raygenExport = "raygen";
    backend->missExport = "miss";
    backend->hitGroupExport = ( bsClosestHit && bsClosestHit->handle ) ? "hg" : "";

    // Auto-derive global binding layouts.
    std::vector< vhBackendShader* > rtShaders;
    rtShaders.push_back( bsRayGen );
    rtShaders.push_back( bsMiss );
    if ( bsClosestHit ) rtShaders.push_back( bsClosestHit );
    if ( bsAnyHit ) rtShaders.push_back( bsAnyHit );
    if ( bsIntersection ) rtShaders.push_back( bsIntersection );
    backend->desc.globalBindingLayouts = BE_Util_BuildStageIndexedLayouts( rtShaders.data(), ( int ) rtShaders.size(), false, false, true );

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backend->handle = g_vhDevice->createRayTracingPipeline( backend->desc );
        if ( !backend->handle )
        {
            VRHI_ERR( "vhCreateRTPipeline() : Failed to create raytracing pipeline!\n" );
        }
    }
}

void vhCmdBackendState::Handle_vhDestroyRTPipeline( VIDL_vhDestroyRTPipeline* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( backendRTPipelines.find( cmd->pipeline ) == backendRTPipelines.end() )
    {
        VRHI_ERR( "vhDestroyRTPipeline() : RTPipeline %d not found!\n", cmd->pipeline );
        return;
    }

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backendRTPipelines.erase( cmd->pipeline );
    }
}

void vhCmdBackendState::Handle_vhCreateShaderTable( VIDL_vhCreateShaderTable* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( cmd->pipeline == VRHI_INVALID_HANDLE ) return;

    auto pipelineIt = backendRTPipelines.find( cmd->pipeline );
    if ( pipelineIt == backendRTPipelines.end() )
    {
        VRHI_ERR( "vhCreateShaderTable() : Parent pipeline %d not found!\n", cmd->pipeline );
        return;
    }

    vhShaderTable table = cmd->table;
    if ( table == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateShaderTable() : Invalid shader table handle!\n" );
        return;
    }

    auto tableBackend = std::make_unique< vhBackendShaderTable >();
    tableBackend->pipeline = cmd->pipeline;

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        tableBackend->handle = pipelineIt->second->handle->createShaderTable();
        if ( !tableBackend->handle )
        {
            VRHI_ERR( "vhCreateShaderTable() : Failed to create shader table!\n" );
            return;
        }

        // Auto-populate from stored export names when the pipeline was created via the simple overload.
        auto& pipeBackend = pipelineIt->second;
        if ( !pipeBackend->raygenExport.empty() )
            tableBackend->handle->setRayGenerationShader( pipeBackend->raygenExport.c_str() );
        if ( !pipeBackend->missExport.empty() )
            tableBackend->handle->addMissShader( pipeBackend->missExport.c_str() );
        if ( !pipeBackend->hitGroupExport.empty() )
            tableBackend->handle->addHitGroup( pipeBackend->hitGroupExport.c_str() );
    }

    backendShaderTables[table] = std::move( tableBackend );
}

void vhCmdBackendState::Handle_vhDestroyShaderTable( VIDL_vhDestroyShaderTable* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    if ( backendShaderTables.find( cmd->table ) == backendShaderTables.end() )
    {
        VRHI_ERR( "vhDestroyShaderTable() : ShaderTable %d not found!\n", cmd->table );
        return;
    }

    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        backendShaderTables.erase( cmd->table );
    }
}

void vhCmdBackendState::Handle_vhShaderTableSetRayGen( VIDL_vhShaderTableSetRayGen* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendShaderTables.find( cmd->table );
    if ( it == backendShaderTables.end() )
    {
        VRHI_ERR( "vhShaderTableSetRayGen() : ShaderTable %d not found!\n", cmd->table );
        return;
    }
    auto table = it->second.get();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        table->handle->setRayGenerationShader( cmd->exportName ? cmd->exportName : "", cmd->bindingSet );
    }
}

void vhCmdBackendState::Handle_vhShaderTableAddMiss( VIDL_vhShaderTableAddMiss* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendShaderTables.find( cmd->table );
    if ( it == backendShaderTables.end() )
    {
        VRHI_ERR( "vhShaderTableAddMiss() : ShaderTable %d not found!\n", cmd->table );
        return;
    }
    auto table = it->second.get();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        table->handle->addMissShader( cmd->exportName ? cmd->exportName : "", cmd->bindingSet );
    }
}

void vhCmdBackendState::Handle_vhShaderTableAddHitGroup( VIDL_vhShaderTableAddHitGroup* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendShaderTables.find( cmd->table );
    if ( it == backendShaderTables.end() )
    {
        VRHI_ERR( "vhShaderTableAddHitGroup() : ShaderTable %d not found!\n", cmd->table );
        return;
    }
    auto table = it->second.get();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        table->handle->addHitGroup( cmd->exportName ? cmd->exportName : "", cmd->bindingSet );
    }
}

void vhCmdBackendState::Handle_vhShaderTableAddCallable( VIDL_vhShaderTableAddCallable* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto it = backendShaderTables.find( cmd->table );
    if ( it == backendShaderTables.end() )
    {
        VRHI_ERR( "vhShaderTableAddCallable() : ShaderTable %d not found!\n", cmd->table );
        return;
    }
    auto table = it->second.get();
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        table->handle->addCallableShader( cmd->exportName ? cmd->exportName : "", cmd->bindingSet );
    }
}

// Refactored to use BE_DispatchRays
void vhCmdBackendState::Handle_vhDispatchRays( VIDL_vhDispatchRays* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto stateIt = backendStates.find( cmd->stateID );
    if ( stateIt == backendStates.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchRays() : State %llu not found!\n", cmd->stateID );
        return;
    }

    auto tableIt = backendShaderTables.find( cmd->table );
    if ( tableIt == backendShaderTables.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchRays() : ShaderTable %d not found!\n", cmd->table );
        return;
    }

    auto& state = stateIt->second;
    auto table = tableIt->second.get();
    auto pipelineIt = backendRTPipelines.find( table->pipeline );
    if ( pipelineIt == backendRTPipelines.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchRays() : Pipeline for ShaderTable %d not found!\n", cmd->table );
        return;
    }
    auto pipeline = pipelineIt->second.get();

    BE_DispatchRays( state, *pipeline, *table, cmd->args );
}


void vhCmdBackendState::Handle_vhCmdSetStateViewRect( VIDL_vhCmdSetStateViewRect* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].viewRect = cmd->rect;
}

void vhCmdBackendState::Handle_vhCmdSetStateViewScissor( VIDL_vhCmdSetStateViewScissor* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].viewScissor = cmd->scissor;
}

void vhCmdBackendState::Handle_vhCmdSetStateViewClear( VIDL_vhCmdSetStateViewClear* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    state.clearFlags = cmd->flags;
    state.clearColor = cmd->color;
    state.clearColorUInt = cmd->colorUInt;
    state.clearDepth = cmd->depth;
    state.clearStencil = cmd->stencil;
}

void vhCmdBackendState::Handle_vhCmdSetStateProgram( VIDL_vhCmdSetStateProgram* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].program = cmd->program;
}

void vhCmdBackendState::Handle_vhCmdSetStateViewTransform( VIDL_vhCmdSetStateViewTransform* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    state.viewMatrix = cmd->view;
    state.projMatrix = cmd->proj;
}

void vhCmdBackendState::Handle_vhCmdSetStateWorldTransform( VIDL_vhCmdSetStateWorldTransform* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].worldMatrix = cmd->matrices;
    backendStates[cmd->id].dirty |= VRHI_DIRTY_WORLD;
}

void vhCmdBackendState::Handle_vhCmdSetStateFlags( VIDL_vhCmdSetStateFlags* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].stateFlags = cmd->flags;
}

void vhCmdBackendState::Handle_vhCmdSetStateDebugFlags( VIDL_vhCmdSetStateDebugFlags* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].debugFlags = cmd->flags;
}

void vhCmdBackendState::Handle_vhCmdSetStateStencil( VIDL_vhCmdSetStateStencil* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    state.stencilState = cmd->stencilState;
}

void vhCmdBackendState::Handle_vhCmdSetStateDepthBias( VIDL_vhCmdSetStateDepthBias* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    state.depthBias = cmd->bias;
    state.depthBiasClamp = cmd->clamp;
    state.slopeScaledDepthBias = cmd->slopeScaled;
}

void vhCmdBackendState::Handle_vhCmdSetStateVertexBindings( VIDL_vhCmdSetStateVertexBindings* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].vertexBindings = cmd->bindings;
}

void vhCmdBackendState::Handle_vhCmdSetStateVertexBuffer( VIDL_vhCmdSetStateVertexBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    if ( cmd->stream >= state.vertexBindings.size() ) state.vertexBindings.resize( cmd->stream + 1 );
    state.vertexBindings[cmd->stream] = { cmd->buffer, cmd->stream, cmd->start, cmd->num, cmd->offset, cmd->layoutOverride };
    state.vertexBindings[cmd->stream].isInstanced = cmd->isInstanced;
}

void vhCmdBackendState::Handle_vhCmdSetStateIndexBuffer( VIDL_vhCmdSetStateIndexBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].indexBinding = { cmd->buffer, cmd->first, cmd->num, cmd->offset };
}

void vhCmdBackendState::Handle_vhCmdSetStateTextures( VIDL_vhCmdSetStateTextures* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].textures = cmd->textures;
}

void vhCmdBackendState::Handle_vhCmdSetStateSamplers( VIDL_vhCmdSetStateSamplers* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].samplers = cmd->samplers;
}

void vhCmdBackendState::Handle_vhCmdSetStateBuffers( VIDL_vhCmdSetStateBuffers* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].buffers = cmd->buffers;
}

void vhCmdBackendState::Handle_vhCmdSetStateConstants( VIDL_vhCmdSetStateConstants* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].constants = cmd->constants;
}

void vhCmdBackendState::Handle_vhCmdSetStatePushConstants( VIDL_vhCmdSetStatePushConstants* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].pushConstants = cmd->data;
    backendStates[cmd->id].dirty |= VRHI_DIRTY_PUSH_CONSTANTS;
}

void vhCmdBackendState::Handle_vhCmdSetStateUniforms( VIDL_vhCmdSetStateUniforms* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    backendStates[cmd->id].uniforms = cmd->uniforms;
}

void vhCmdBackendState::Handle_vhCmdSetStateAttachments( VIDL_vhCmdSetStateAttachments* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    state.colourAttachment = cmd->colours;
    state.depthAttachment = cmd->depth;
}


void vhCmdBackendState::Handle_vhCmdSetStateBlendConstants( VIDL_vhCmdSetStateBlendConstants* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto it = backendStates.find( cmd->id );
    if ( it != backendStates.end() )
    {
        it->second.blendConstantColor = cmd->blendConst;
    }
}

void vhCmdBackendState::Handle_vhCmdSetStateViewDepthRange( VIDL_vhCmdSetStateViewDepthRange* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto it = backendStates.find( cmd->id );
    if ( it != backendStates.end() )
    {
        it->second.viewDepthRange = glm::vec2( cmd->minZ, cmd->maxZ );
    }
}

void vhCmdBackendState::Handle_vhCmdSetStateShadingRate( VIDL_vhCmdSetStateShadingRate* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto it = backendStates.find( cmd->id );
    if ( it != backendStates.end() )
    {
        it->second.shadingRateFlags = cmd->flags;
        it->second.shadingRateImage = cmd->image;
    }
}

void vhCmdBackendState::Handle_vhCmdSetStateIndirectParams( VIDL_vhCmdSetStateIndirectParams* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto it = backendStates.find( cmd->id );
    if ( it != backendStates.end() )
    {
        it->second.indirectParams.buffer = cmd->buffer;
        it->second.indirectParams.byteOffset = cmd->offset;
    }
}

void vhCmdBackendState::Handle_vhCmdSetStateAccelStructs( VIDL_vhCmdSetStateAccelStructs* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto it = backendStates.find( cmd->id );
    if ( it != backendStates.end() )
    {
        it->second.accelStructs = cmd->accelStructs;
        it->second.dirty |= VRHI_DIRTY_ACCEL_STRUCT;
    }
}

void vhCmdBackendState::Handle_vhFlushInternal( VIDL_vhFlushInternal* cmd )
{
    VRHI_PROFILE_FUNCTION();
    BE_CmdRAII cmdRAII( cmd );
    std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );

    // Flush and step transient buffer maps here.
    // This needs to be done *before* we flush the command lists to GPU!!

    m_globalUniformBuffer.Unmap_DeviceStateLocked();
    m_globalUniformBuffer.Step();
    m_globalUniformBufferLastHash = 0;
    
    m_worldUniformBuffer.Unmap_DeviceStateLocked();
    m_worldUniformBuffer.Step();
    m_worldUniformBufferLastHash = 0;

    m_userUniformBuffer.Unmap_DeviceStateLocked();
    m_userUniformBuffer.Step();

    // Send it!!
    vhCmdListFlushAll_DeviceStateLocked();

    // Free all cmd memory allocations, because hitting this flush means all previous commands have been processed.
    {
        std::lock_guard< std::mutex > lock( g_vhMemListMutex );
        g_vhMemList.clear();
    }
    if ( cmd->waitForGPU )
    {
        vhProfile( "Handle_vhFlushInternal_WaitForGPU", true );
        g_vhDevice->waitForIdle();
        vhProfile( "Handle_vhFlushInternal_WaitForGPU", false );
    }
    g_vhDevice->runGarbageCollection();

    // Notify caller that we're done.
    // Safety warning : fence is probably from stack of caller
    if ( cmd->fence )
        cmd->fence->store( true );

    g_vhCmdArena.Rotate();
}

void vhCmdBackendState::Handle_vhDispatch( VIDL_vhDispatch* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->stateID == VRHI_INVALID_HANDLE || cmd->workGroupCount.x == 0 || cmd->workGroupCount.y == 0 || cmd->workGroupCount.z == 0 )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatch() skipped: Invalid state ID or zero work group count.\n" );
        return;
    }

    // Ensure state exists
    auto itState = backendStates.find( cmd->stateID );
    if ( itState == backendStates.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatch: State %llu not found!\n", cmd->stateID );
        return;
    }
    auto& state = itState->second;

    if ( state.program.empty() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatch: State %llu has no program set!\n", cmd->stateID );
        return;
    }

    auto itShader = backendShaders.find( state.program[0] );
    if ( itShader == backendShaders.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatch: Shader %llu not found for state %llu!\n", state.program[0], cmd->stateID );
        return;
    }

    BE_Dispatch( state, *itShader->second, cmd->workGroupCount );
}

void vhCmdBackendState::Handle_vhDispatchIndirect( VIDL_vhDispatchIndirect* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->stateID == VRHI_INVALID_HANDLE || cmd->indirectBuffer == VRHI_INVALID_HANDLE )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchIndirect() skipped: Invalid state ID or indirect buffer.\n" );
        return;
    }

    auto itBuf = backendBuffers.find( cmd->indirectBuffer );
    if ( itBuf == backendBuffers.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchIndirect: Indirect buffer %d not found!\n", cmd->indirectBuffer );
        return;
    }

    auto itState = backendStates.find( cmd->stateID );
    if ( itState == backendStates.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchIndirect: State %llu not found!\n", cmd->stateID );
        return;
    }
    auto& state = itState->second;

    if ( state.program.empty() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchIndirect: State %llu has no program set!\n", cmd->stateID );
        return;
    }

    auto itShader = backendShaders.find( state.program[0] );
    if ( itShader == backendShaders.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDispatchIndirect: Shader %llu not found for state %llu!\n", state.program[0], cmd->stateID );
        return;
    }

    BE_DispatchIndirect( state, *itShader->second, *itBuf->second, cmd->byteOffset );
}

void vhCmdBackendState::Handle_vhDrawCommonInternal( VIDL_vhDrawCommonInternal* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto itState = backendStates.find( cmd->state );
    if ( itState == backendStates.end() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "Draw with invalid state ID %llu\n", cmd->state );
        return;
    }
    vhState& state = itState->second;

    s_shaders.clear();

    for ( vhShader shaderHandle : state.program )
    {
        auto it = backendShaders.find( shaderHandle );
        if ( it != backendShaders.end() )
            s_shaders.push_back( it->second.get() );
    }

    if ( s_shaders.empty() )
    {
        if ( g_vhInit.errorOnSkippedDraw ) VRHI_ERR( "vhDraw(): No valid shaders in program!\n" );
        return;
    }

    // Clear indirect buffer if it's NOT an indirect draw to prevent BE_PreSubmitCommon_State from binding it
    if ( !( cmd->flags & VRHI_DRAW_INDIRECT ) )
        state.indirectParams.buffer = VRHI_INVALID_HANDLE;

    nvrhi::DrawArguments args;
    args.setVertexCount( cmd->vertexCount )
        .setInstanceCount( cmd->instanceCount )
        .setStartVertexLocation( cmd->startVertexLocation )
        .setStartIndexLocation( cmd->startIndexLocation )
        .setStartInstanceLocation( cmd->startInstanceLocation );

    BE_Submit( state, s_shaders.data(), ( int ) s_shaders.size(), cmd->flags, args, cmd->drawCount );
}

void vhCmdBackendState::Handle_vhBlitBuffer( VIDL_vhBlitBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->dst == VRHI_INVALID_HANDLE || cmd->src == VRHI_INVALID_HANDLE || cmd->size == 0 ) return;

    auto itDst = backendBuffers.find( cmd->dst );
    auto itSrc = backendBuffers.find( cmd->src );

    if ( itDst == backendBuffers.end() )
    {
        VRHI_ERR( "vhBlitBuffer: Destination buffer %d not found!\n", cmd->dst );
        return;
    }
    if ( itSrc == backendBuffers.end() )
    {
        VRHI_ERR( "vhBlitBuffer: Source buffer %d not found!\n", cmd->src );
        return;
    }

    // We can't clamp size if offset is out of bounds.
    if ( cmd->srcOffset > itSrc->second->desc.byteSize || cmd->dstOffset > itDst->second->desc.byteSize )
    {
        VRHI_ERR( "vhBlitBuffer: Source or destination buffer offset out of bounds!\n" );
        return;
    }

    // Clamp size to avoid buffer overruns.
    uint64_t clampedSizeBytes = cmd->size;
    if ( cmd->srcOffset + cmd->size > itSrc->second->desc.byteSize )
    {
        clampedSizeBytes = std::min( itSrc->second->desc.byteSize - cmd->srcOffset, cmd->size );
    }
    if ( cmd->dstOffset + cmd->size > itDst->second->desc.byteSize )
    {
        clampedSizeBytes = std::min( itDst->second->desc.byteSize - cmd->dstOffset, clampedSizeBytes );
    }

    BE_BlitBuffer( *itDst->second, *itSrc->second, cmd->dstOffset, cmd->srcOffset, clampedSizeBytes );
}

void vhCmdBackendState::Handle_vhClear( VIDL_vhClear* cmd )
{
    BE_CmdRAII cmdRAII( cmd );

    auto itState = backendStates.find( cmd->state );
    if ( itState == backendStates.end() )
    {
        VRHI_ERR( "vhClear(): State %llu not found!\n", cmd->state );
        return;
    }
    vhState& state = itState->second;

    // Validate that at least one attachment is bound
    if ( state.colourAttachment.empty() && state.depthAttachment.texture == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhClear(): No attachments bound in state!\n" );
        return;
    }

    // Clear Color Attachments
    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );

            if ( ( cmd->clearFlags & VRHI_CLEAR_COLOR ) && ( cmd->clearFlags & VRHI_CLEAR_UINT ) )
            {
                // Integer Clear
                // Pack glm::u8vec4 into uint32_t for NVRHI (which expects R8G8B8A8_UINT basically)
                uint32_t clearVal = ( uint32_t( state.clearColorUInt.a ) << 24 ) |
                                    ( uint32_t( state.clearColorUInt.b ) << 16 ) |
                                    ( uint32_t( state.clearColorUInt.g ) << 8 ) |
                                    ( uint32_t( state.clearColorUInt.r ) );

                for ( const auto& rt : state.colourAttachment )
                {
                    if ( rt.texture == VRHI_INVALID_HANDLE ) continue;
                    auto it = backendTextures.find( rt.texture );
                    if ( it != backendTextures.end() && it->second->handle )
                    {
                        nvrhi::TextureSubresourceSet subresources;
                        subresources.baseMipLevel = rt.mipLevel;
                        subresources.numMipLevels = 1;
                        subresources.baseArraySlice = rt.arrayLayer;
                        subresources.numArraySlices = 1;

                        cmdlist->clearTextureUInt( it->second->handle, subresources, clearVal );
                    }
                }
            }
            else if ( cmd->clearFlags & VRHI_CLEAR_COLOR )
            {
                // Float Clear
                nvrhi::Color clearVal( state.clearColor.r, state.clearColor.g, state.clearColor.b, state.clearColor.a );

                for ( const auto& rt : state.colourAttachment )
                {
                    if ( rt.texture == VRHI_INVALID_HANDLE ) continue;
                    auto it = backendTextures.find( rt.texture );
                    if ( it != backendTextures.end() && it->second->handle )
                    {
                        nvrhi::TextureSubresourceSet subresources;
                        subresources.baseMipLevel = rt.mipLevel;
                        subresources.numMipLevels = 1;
                        subresources.baseArraySlice = rt.arrayLayer;
                        subresources.numArraySlices = 1;

                        cmdlist->clearTextureFloat( it->second->handle, subresources, clearVal );
                    }
                }
            }

        // Clear Depth/Stencil Attachment
        if ( ( cmd->clearFlags & VRHI_CLEAR_DEPTH ) || ( cmd->clearFlags & VRHI_CLEAR_STENCIL ) )
        {
            if ( state.depthAttachment.texture != VRHI_INVALID_HANDLE )
            {
                auto it = backendTextures.find( state.depthAttachment.texture );
                if ( it != backendTextures.end() && it->second->handle )
                {
                    nvrhi::TextureSubresourceSet subresources;
                    subresources.baseMipLevel = state.depthAttachment.mipLevel;
                    subresources.numMipLevels = 1;
                    subresources.baseArraySlice = state.depthAttachment.arrayLayer;
                    subresources.numArraySlices = 1;

                    bool clearDepth = ( cmd->clearFlags & VRHI_CLEAR_DEPTH ) != 0;
                    bool clearStencil = ( cmd->clearFlags & VRHI_CLEAR_STENCIL ) != 0;

                    cmdlist->clearDepthStencilTexture( it->second->handle, subresources, clearDepth, state.clearDepth, clearStencil, state.clearStencil );
                }
            }
        }
    }
}

void vhCmdBackendState::RHIThreadEntry( std::function<void()> initCallback )
{
    VRHI_LOG( "    RHI Thread started.\n" );
    g_vhCmdThreadReady = true;
    if ( initCallback ) initCallback();

    while ( !g_vhCmdsQuit )
    {
        void* cmd = nullptr;
        if ( !g_vhCmds.try_dequeue( cmd ) )
        {
            // Block until there is a command to process
            if ( !g_vhCmds.wait_dequeue_timed( cmd, std::chrono::milliseconds( 8 ) ) )
                continue;
        }
        if ( cmd != nullptr )
        {
            std::lock_guard< std::mutex > lock( backendMutex );
            HandleCmd( cmd );
        }
    }

    VRHI_LOG( "    RHI Thread exiting.\n" );
}

// --------------------------------------------------------------------------
// Backend :: Query
// --------------------------------------------------------------------------

// The query functions are a fastpath for getting info about objects from the main-thread. They directly lock the backend mutex and access the backend maps, rather than 
// sending a command to the backend thread and then waiting for a response. Object info queries are usually extremely short and fast, so this is OK. Query functions should never do 
// significant work or take a long time to complete, because that would bubble the hell out of the command thread.

vhTexInfo vhCmdBackendState::QueryTextureInfo( vhTexture handle, std::vector< vhTextureMipInfo >* outMipInfo )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendTextures.find( handle );
    if ( it == backendTextures.end() || !it->second )
    {
        return vhTexInfo();
    }

    if ( outMipInfo )
    {
        *outMipInfo = it->second->mipInfo;
    }
    return it->second->info;
}

nvrhi::TextureHandle vhCmdBackendState::QueryTextureHandle( vhTexture handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendTextures.find( handle );
    if ( it == backendTextures.end() || !it->second )
    {
        return nullptr;
    }
    return it->second->handle;
}

uint64_t vhCmdBackendState::QueryBufferInfo( vhBuffer handle, uint32_t* outStride, uint64_t* outFlags )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendBuffers.find( handle );
    if ( it == backendBuffers.end() || !it->second )
    {
        return 0;
    }

    if ( outStride ) *outStride = it->second->stride;
    if ( outFlags ) *outFlags = it->second->flags;
    return it->second->desc.byteSize;
}

nvrhi::BufferHandle vhCmdBackendState::QueryBufferHandle( vhBuffer handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendBuffers.find( handle );
    if ( it == backendBuffers.end() || !it->second )
    {
        return nullptr;
    }
    return it->second->handle;
}

const std::vector< vhVertexLayoutDef >* vhCmdBackendState::QueryBufferLayout( vhBuffer handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendBuffers.find( handle );
    if ( it == backendBuffers.end() || !it->second )
    {
        return nullptr;
    }
    return &it->second->layout;
}

void vhCmdBackendState::QueryShaderInfo(
    vhShader handle,
    glm::uvec3* outGroupSize,
    std::vector< vhShaderReflectionResource >* outResources,
    std::vector< vhPushConstantRange >* outPushConstants,
    std::vector< vhSpecConstant >* outSpecConstants
)
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendShaders.find( handle );
    if ( it == backendShaders.end() || !it->second->handle )
    {
        if ( outGroupSize ) *outGroupSize = { 0, 0, 0 };
        if ( outResources ) outResources->clear();
        if ( outPushConstants ) outPushConstants->clear();
        if ( outSpecConstants ) outSpecConstants->clear();
        return;
    }

    const auto& bshader = *it->second;
    if ( outGroupSize ) *outGroupSize = bshader.threadGroupSize;
    if ( outResources ) *outResources = bshader.reflection;
    if ( outPushConstants ) *outPushConstants = bshader.pushConstants;
    if ( outSpecConstants ) *outSpecConstants = bshader.specConstants;
}

nvrhi::ShaderHandle vhCmdBackendState::QueryShaderHandle( vhShader handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendShaders.find( handle );
    if ( it == backendShaders.end() || !it->second )
    {
        return nullptr;
    }
    return it->second->handle;
}

bool vhCmdBackendState::QueryState( vhStateId id, vhState& outState )
{
    std::lock_guard<std::mutex> lock( backendMutex );
    auto it = backendStates.find( id );
    if ( it == backendStates.end() )
    {
        return false;
    }
    outState = it->second;
    return true;
}

float vhCmdBackendState::QueryTimer( vhTimerID timerID )
{
    std::lock_guard< std::mutex > lock( backendMutex );

    auto it = backendTimerQueries.find( timerID );
    if ( it == backendTimerQueries.end() || !it->second->initialised )
    {
        return 0.0f;
    }

    return it->second->lastQueryTime;
}

glm::u64vec2 vhCmdBackendState::QueryTextureMemoryRequirements( vhTexture texture )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto texIt = backendTextures.find( texture );
    if ( texIt == backendTextures.end() || !texIt->second->handle )
    {
        return glm::u64vec2( 0 );
    }

    uint64_t size = 0;
    uint64_t alignment = 0;
    {
        std::lock_guard< std::mutex > lockDevice( g_nvRHIStateMutex );
        nvrhi::MemoryRequirements req = g_vhDevice->getTextureMemoryRequirements( texIt->second->handle );
        size = req.size;
        alignment = req.alignment;
    }
    return glm::u64vec2( size, alignment );
}

glm::u64vec2 vhCmdBackendState::AllocTextureMemory( vhHeap heap, uint64_t size, uint64_t alignment )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    if ( alignment == 0 )
    {
        VRHI_ERR( "vhHeapAlloc(): Invalid alignment 0\n" );
        return glm::u64vec2( 0 );
    }

    auto heapIt = backendHeaps.find( heap );
    if ( heapIt == backendHeaps.end() || !heapIt->second->handle )
    {
        return glm::u64vec2( 0 );
    }
    if ( !heapIt->second->allocator )
    {
        VRHI_ERR( "vhHeapAlloc(): Heap %d allocator not initialised\n", heap );
        return glm::u64vec2( 0 );
    }

    if ( size > UINT32_MAX )
    {
        VRHI_ERR( "vhHeapAlloc(): Size %llu exceeds maximum supported size %u\n", size, UINT32_MAX );
        return glm::u64vec2( 0 );
    }
    if ( alignment > UINT32_MAX )
    {
        VRHI_ERR( "vhHeapAlloc(): Alignment %llu exceeds maximum supported size %u\n", alignment, UINT32_MAX );
        return glm::u64vec2( 0 );
    }
    if ( size > UINT32_MAX - ( alignment - 1 ) )
    {
        VRHI_ERR( "vhHeapAlloc(): Aligned size %llu exceeds maximum supported size %u\n", size + alignment - 1, UINT32_MAX );
        return glm::u64vec2( 0 );
    }

    uint64_t alignedSize = size + alignment - 1;

    OffsetAllocator::Allocation allocation = static_cast< OffsetAllocator::Allocator* >( heapIt->second->allocator )->allocate( static_cast< uint32_t >( alignedSize ) );
    if ( allocation.offset == OffsetAllocator::Allocation::NO_SPACE )
    {
        VRHI_ERR( "vhHeapAlloc(): Heap %d out of memory (need %llu, capacity %llu)\n", heap, alignedSize, heapIt->second->desc.capacity );
        return glm::u64vec2( 0 );
    }

    uint64_t alignedOffset = ( ( uint64_t ) allocation.offset + alignment - 1 ) / alignment * alignment;
    uint64_t allocationEnd = ( uint64_t ) allocation.offset + alignedSize;
    if ( alignedOffset + size > allocationEnd )
    {
        VRHI_ERR( "vhHeapAlloc(): Internal alignment overflow for heap %d\n", heap );
        static_cast< OffsetAllocator::Allocator* >( heapIt->second->allocator )->free( allocation );
        return glm::u64vec2( 0 );
    }

    heapIt->second->allocations[alignedOffset] = std::make_pair( allocation.offset, allocation.metadata );
    return glm::u64vec2( alignedOffset, size );
}

void vhCmdBackendState::FreeTextureMemory( vhHeap heap, uint64_t offset )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto heapIt = backendHeaps.find( heap );
    if ( heapIt == backendHeaps.end() || !heapIt->second->handle )
    {
        return;
    }
    if ( !heapIt->second->allocator )
    {
        VRHI_ERR( "vhHeapFree(): Heap %d allocator not initialised\n", heap );
        return;
    }

    auto allocIt = heapIt->second->allocations.find( offset );
    if ( allocIt == heapIt->second->allocations.end() )
    {
        VRHI_ERR( "vhHeapFree(): Unknown offset %llu in heap %d\n", offset, heap );
        return;
    }

    OffsetAllocator::Allocation allocation;
    allocation.offset = allocIt->second.first;
    allocation.metadata = allocIt->second.second;
    static_cast< OffsetAllocator::Allocator* >( heapIt->second->allocator )->free( allocation );
    heapIt->second->allocations.erase( allocIt );
}

glm::u64vec2 vhCmdBackendState::QueryBufferMemoryRequirements( vhBuffer buffer )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto bufIt = backendBuffers.find( buffer );
    if ( bufIt == backendBuffers.end() || !bufIt->second->handle )
    {
        return glm::u64vec2( 0 );
    }

    // Buffer alignment is typically 256 bytes for uniform/storage buffers
    // Use the device properties for proper alignment if available
    uint64_t size = bufIt->second->desc.byteSize;
    uint64_t alignment = 256; // Standard Vulkan buffer alignment

    return glm::u64vec2( size, alignment );
}

nvrhi::rt::AccelStructHandle vhCmdBackendState::QueryAccelStructHandle( vhAccelStruct as )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendAccelStructs.find( as );
    if ( it == backendAccelStructs.end() ) return nullptr;
    return it->second->handle;
}

nvrhi::rt::PipelineHandle vhCmdBackendState::QueryRTPipelineHandle( vhRTPipeline pipeline )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendRTPipelines.find( pipeline );
    if ( it == backendRTPipelines.end() ) return nullptr;
    return it->second->handle;
}

nvrhi::rt::ShaderTableHandle vhCmdBackendState::QueryShaderTableHandle( vhShaderTable table )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendShaderTables.find( table );
    if ( it == backendShaderTables.end() ) return nullptr;
    return it->second->handle;
}

// --------------------------------------------------------------------------
// Backend Bridge
// --------------------------------------------------------------------------

void vhBackendInit()
{
    g_vhCmdBackendState.init();
}

void vhBackendShutdown()
{
    g_vhCmdBackendState.shutdown();
}

void vhBackendThreadEntry( std::function<void()> initCallback )
{
    g_vhCmdBackendState.RHIThreadEntry( initCallback );
}

vhTexInfo vhBackendQueryTextureInfo( vhTexture texture, std::vector< vhTextureMipInfo >* outMipInfo )
{
    return g_vhCmdBackendState.QueryTextureInfo( texture, outMipInfo );
}

nvrhi::TextureHandle vhBackendQueryTextureHandle( vhTexture texture )
{
    return g_vhCmdBackendState.QueryTextureHandle( texture );
}

uint64_t vhBackendQueryBufferInfo( vhBuffer buffer, uint32_t* outStride, uint64_t* outFlags )
{
    return g_vhCmdBackendState.QueryBufferInfo( buffer, outStride, outFlags );
}

nvrhi::BufferHandle vhBackendQueryBufferHandle( vhBuffer buffer )
{
    return g_vhCmdBackendState.QueryBufferHandle( buffer );
}

const std::vector< vhVertexLayoutDef >* vhBackendQueryBufferLayout( vhBuffer buffer )
{
    return g_vhCmdBackendState.QueryBufferLayout( buffer );
}

void vhBackendQueryShaderInfo( vhShader shader, glm::uvec3* outGroupSize, std::vector< vhShaderReflectionResource >* outResources, std::vector< vhPushConstantRange >* outPushConstants, std::vector< vhSpecConstant >* outSpecConstants )
{
    g_vhCmdBackendState.QueryShaderInfo( shader, outGroupSize, outResources, outPushConstants, outSpecConstants );
}

nvrhi::ShaderHandle vhBackendQueryShaderHandle( vhShader shader )
{
    return g_vhCmdBackendState.QueryShaderHandle( shader );
}

bool vhBackendQueryState( vhStateId id, vhState& outState )
{
    return g_vhCmdBackendState.QueryState( id, outState );
}

float vhBackendQueryTimer( vhTimerID timerID )
{
    return g_vhCmdBackendState.QueryTimer( timerID );
}

glm::u64vec2 vhBackendQueryTextureMemoryRequirements( vhTexture texture )
{
    return g_vhCmdBackendState.QueryTextureMemoryRequirements( texture );
}

glm::u64vec2 vhBackendAllocTextureMemory( vhHeap heap, uint64_t size, uint64_t alignment )
{
    return g_vhCmdBackendState.AllocTextureMemory( heap, size, alignment );
}

void vhBackendFreeTextureMemory( vhHeap heap, uint64_t offset )
{
    g_vhCmdBackendState.FreeTextureMemory( heap, offset );
}

glm::u64vec2 vhBackendQueryBufferMemoryRequirements( vhBuffer buffer )
{
    return g_vhCmdBackendState.QueryBufferMemoryRequirements( buffer );
}

nvrhi::rt::AccelStructHandle vhBackendQueryAccelStructHandle( vhAccelStruct as )
{
    return g_vhCmdBackendState.QueryAccelStructHandle( as );
}

nvrhi::rt::PipelineHandle vhBackendQueryRTPipelineHandle( vhRTPipeline pipeline )
{
    return g_vhCmdBackendState.QueryRTPipelineHandle( pipeline );
}


nvrhi::rt::ShaderTableHandle vhBackendQueryShaderTableHandle( vhShaderTable table )
{
    return g_vhCmdBackendState.QueryShaderTableHandle( table );
}


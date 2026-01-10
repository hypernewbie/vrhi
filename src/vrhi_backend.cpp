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
#include <komihash/komihash.h>

vhCmdBackendState g_vhCmdBackendState;
void vhCmdListFlushAll_DeviceStateLocked();

std::unordered_map< nvrhi::BindingLayoutHandle, vhBackendShader* > vhCmdBackendState::s_layoutToShader;
vhStateResolveCache vhCmdBackendState::s_resolveCache;
std::unordered_map< uint32_t, vhShaderReflectionResource* > vhCmdBackendState::s_slotToReflection;
std::unordered_map< uint64_t, const vhVertexLayoutDef* > vhCmdBackendState::s_layoutLocationTable;
std::vector< nvrhi::VertexAttributeDesc > vhCmdBackendState::s_attributes;

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

int32_t vhCmdBackendState::BE_Util_ResolveBindingSlot( const char* name, nvrhi::ResourceType type, vhBackendShader& shader, bool debugLog )
{
    for ( auto& resource : shader.reflection )
    {
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

bool vhCmdBackendState::BE_Util_ShaderStageMatches( uint64_t flags, bool useCompute, bool useGraphics )
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
    return false;
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

    // Calculate layer size.
    int64_t totalLayerSize = 0;
    for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
    {
        totalLayerSize += btex.mipInfo[mip].size;
    }

    // Update the texture.
    {
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
    }
}

void vhCmdBackendState::BE_BlitTexture( vhBackendTexture& bdst, vhBackendTexture& bsrc, int dstMip, int srcMip, int dstLayer, int srcLayer, glm::ivec3 dstOffset, glm::ivec3 srcOffset, glm::ivec3 extent )
{
    if ( !bdst.handle || !bsrc.handle ) return;

    // Higher level layers should already handle the validation.
    assert( srcMip >= 0 && srcMip < bsrc.info.mipLevels );
    assert( dstMip >= 0 && dstMip < bdst.info.mipLevels );
    assert( srcLayer >= 0 && srcLayer < bsrc.info.arrayLayers );
    assert( dstLayer >= 0 && dstLayer < bdst.info.arrayLayers );

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

    // Acquire command list and execute copy
    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->copyTexture( bdst.handle, dstSlice, bsrc.handle, srcSlice );
    }
}

void vhCmdBackendState::BE_ReadTextureSlow( vhBackendTexture& btex, vhMem* outData, int mip, int layer )
{
    if ( !btex.handle || !outData ) return;
    assert( btex.info.target != nvrhi::TextureDimension::Texture3D );

    // Staging Texture
    auto desc = btex.handle->getDesc();
    desc.isVirtual = false;
    desc.isRenderTarget = false;
    desc.isUAV = false;
    desc.keepInitialState = true;
    desc.initialState = nvrhi::ResourceStates::CopyDest;

    nvrhi::StagingTextureHandle stagingTex;
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        stagingTex = g_vhDevice->createStagingTexture( desc, nvrhi::CpuAccessMode::Read );
    }

    if ( !stagingTex ) return;

    // For this slow-path operation, just use Graphics queue for everything
    // (avoids complexity with transfer queue barriers)
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );

        nvrhi::CommandListParameters params = { .queueType = nvrhi::CommandQueue::Graphics };
        auto cmdList = g_vhDevice->createCommandList( params );
        cmdList->open();

        nvrhi::TextureSlice slice;
        slice.mipLevel = mip;
        slice.arraySlice = layer;

        // Make sure source is in CopySource state
        cmdList->setTextureState( btex.handle, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource );
        cmdList->commitBarriers();

        cmdList->copyTexture( stagingTex, slice, btex.handle, slice );

        cmdList->close();
        g_vhDevice->executeCommandList( cmdList, nvrhi::CommandQueue::Graphics );
        g_vhDevice->waitForIdle();
    }

    // CPU copy
    void* pData = nullptr;
    size_t rowPitch = 0;

    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        nvrhi::TextureSlice slice;
        slice.mipLevel = mip;
        slice.arraySlice = layer;
        pData = g_vhDevice->mapStagingTexture( stagingTex, slice, nvrhi::CpuAccessMode::Read, &rowPitch );
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

        for ( int y = 0; y < height; ++y )
        {
            memcpy( dst + ( size_t ) y * expectedPitch, src + ( size_t ) y * rowPitch, expectedPitch );
        }

        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            g_vhDevice->unmapStagingTexture( stagingTex );
        }
    }
}

void vhCmdBackendState::BE_ResizeBuffer( vhBackendBuffer& bbuf, uint64_t size )
{
    if ( !bbuf.handle ) return;

    auto oldHandle = bbuf.handle;
    auto oldSize = bbuf.desc.byteSize;

    bbuf.desc.setByteSize( size );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        bbuf.handle = g_vhDevice->createBuffer( bbuf.desc );
    }

    if ( !bbuf.handle )
    {
        VRHI_ERR( "vhCreateVertexBuffer() : Failed to create bhandle!\n" );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    cmdlist->copyBuffer( bbuf.handle, 0, oldHandle, 0, glm::min( bbuf.desc.byteSize, oldSize ) );
}

void vhCmdBackendState::BE_UpdateBuffer( vhBackendBuffer& bbuf, uint64_t offset, const vhMem* data )
{
    if ( !bbuf.handle || !data || !data->size() ) return;

    if ( offset + data->size() > bbuf.desc.byteSize )
    {
        assert( bbuf.flags & VRHI_BUFFER_ALLOW_RESIZE );
        BE_ResizeBuffer( bbuf, offset + data->size() );
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->writeBuffer( bbuf.handle, data->data(), data->size(), offset );
    }
}

nvrhi::FramebufferHandle vhCmdBackendState::BE_GetFrameBuffer( const std::vector< vhState::RenderTarget >& colourAttachment, const vhState::RenderTarget& depthAttachment )
{
    nvrhi::FramebufferDesc desc;
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

    return vhFBOCacheGet( desc );
}

bool vhCmdBackendState::BE_PresubmitCommon_PipelineDesc(
    vhState& state,
    vhBackendShader* shaders,
    int shaderCount,
    nvrhi::ComputePipelineDesc* computePipelineDesc, // set to nullptr if not using compute.
    nvrhi::GraphicsPipelineDesc* graphicsPipelineDesc // set to nullptr if not using graphics.
)
{
    assert( shaders && shaderCount > 0 );
    const vhBackendShader* vertexShader = nullptr;

    for ( int shaderIdx = 0; shaderIdx < shaderCount; ++shaderIdx )
    {
        auto& shader = shaders[shaderIdx];
        nvrhi::BindingSetDesc bsetDesc = nvrhi::BindingSetDesc();
        if ( !BE_Util_ShaderStageMatches( shader.flags, computePipelineDesc != nullptr, graphicsPipelineDesc != nullptr ) )
            continue;

        assert( shader.layout );
        if ( computePipelineDesc ) computePipelineDesc->addBindingLayout( shader.layout );
        if ( graphicsPipelineDesc ) graphicsPipelineDesc->addBindingLayout( shader.layout );

        if ( shader.flags & VRHI_SHADER_STAGE_COMPUTE && computePipelineDesc )
        {
            computePipelineDesc->setComputeShader( shader.handle );
        }
        if ( shader.flags & VRHI_SHADER_STAGE_VERTEX && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setVertexShader( shader.handle );
            vertexShader = &shader;
        }
        if ( shader.flags & VRHI_SHADER_STAGE_HULL && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setHullShader( shader.handle );
        }
        if ( shader.flags & VRHI_SHADER_STAGE_DOMAIN && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setDomainShader( shader.handle );
        }
        if ( shader.flags & VRHI_SHADER_STAGE_GEOMETRY && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setGeometryShader( shader.handle );
        }
        if ( shader.flags & VRHI_SHADER_STAGE_PIXEL && graphicsPipelineDesc )
        {
            graphicsPipelineDesc->setPixelShader( shader.handle );
        }
    }

    if ( graphicsPipelineDesc )
    {
        graphicsPipelineDesc->setPrimType( vhTranslatePrimitiveType( state.stateFlags ) );
        graphicsPipelineDesc->renderState.blendState = vhTranslateBlendState( state.stateFlags );
        graphicsPipelineDesc->renderState.depthStencilState = vhTranslateDepthStencilState( state.stateFlags, state.frontStencil, state.backStencil );
        graphicsPipelineDesc->renderState.rasterState = vhTranslateRasterState( state.stateFlags );

        // [TODO] The following fields are not currently populated from vhState:
        // - patchControlPoints: tessellation is only supported if we add it.

        // Resolve Input Layout

        // Resolve Input Layout

        s_layoutLocationTable.clear();
        s_attributes.clear();

        for ( size_t i = 0; i < state.vertexBindings.size(); ++i )
        {
            const auto& binding = state.vertexBindings[i];
            if ( binding.buffer == VRHI_INVALID_HANDLE )
                continue;

            auto it = backendBuffers.find( binding.buffer );
            if ( it == backendBuffers.end() )
                continue;

            const auto& bbuf = *it->second;
            for ( const auto& def : bbuf.layout )
            {
                if ( s_layoutLocationTable.find( def.location ) != s_layoutLocationTable.end() )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH ) VRHI_ERR( "Vertex Attribute Collision: Location %d already bound by previous buffer\n", def.location );
                    return false;
                }
                s_layoutLocationTable[def.location] = &def;
                nvrhi::VertexAttributeDesc attr = vhTranslateVertexAttribute( def, ( uint32_t ) i );
                attr.elementStride = bbuf.stride;
                s_attributes.push_back( attr );
            }
        }

        // Strict validation of shader inputs to ensure every attribute is satisfied by a bound buffer.
        if ( vertexShader )
        {
            for ( const auto& vsAttribDef : vertexShader->inputLayout )
            {
                auto it = s_layoutLocationTable.find( vsAttribDef.location );
                if ( it == s_layoutLocationTable.end() )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH ) VRHI_ERR( "Vertex Attribute Missing: Shader expects Location %d, but no bound buffer provides it.\n", vsAttribDef.location );
                    return false;
                }
                if ( it->second->format != vsAttribDef.format )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_VATTRIB_MISMATCH ) VRHI_ERR( "Vertex Attribute Format Mismatch at Location %d (Buffer: %d, Shader: %d)\n", vsAttribDef.location, ( int ) it->second->format, ( int ) vsAttribDef.format );
                    return false;
                }
            }
            if ( !s_attributes.empty() )
            {
                std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
                graphicsPipelineDesc->inputLayout = g_vhDevice->createInputLayout( s_attributes.data(), ( uint32_t ) s_attributes.size(), vertexShader->handle );
            }
        }
    }

    return true;
}

void vhCmdBackendState::BE_PreSubmitCommon_ResolveStateCache(
    const vhState& state,
    vhBackendShader* shaders,
    int shaderCount,
    vhStateResolveCache& scache
)
{
    assert( !scache.init );

    // Build the backend pointer caches.
    scache.btex.resize( state.textures.size(), nullptr );
    scache.bbuf.resize( state.buffers.size(), nullptr );

    // Resolve backend pointers.

    for ( size_t i = 0; i < state.textures.size(); i++ )
    {
        if ( state.textures[i].texture == VRHI_INVALID_HANDLE )
            continue;
        const auto& it = backendTextures.find( state.textures[i].texture );
        if ( it != backendTextures.end() )
            scache.btex[i] = it->second.get();
    }

    for ( size_t i = 0; i < state.buffers.size(); i++ )
    {
        if ( state.buffers[i].buffer == VRHI_INVALID_HANDLE )
            continue;
        const auto& it = backendBuffers.find( state.buffers[i].buffer );
        if ( it != backendBuffers.end() )
            scache.bbuf[i] = it->second.get();
    }

    // Build slot maps.

    scache.stageBinding.clear();
    for ( int i = 1; i <= VRHI_SHADER_STAGE_MAX; i++ )
    {
        scache.stageBinding[i] = std::make_unique< vhStateResolveCache::ShaderStageBindingSlotState >();
    }

    auto fnResolveSlot = [&]( const char* name, int32_t fallbackSlot, nvrhi::ResourceType type, vhBackendShader& shader ) -> int32_t
    {
        return ( name && name[0] ) ? BE_Util_ResolveBindingSlot( name, type, shader, !!( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) ) : fallbackSlot;
    };

    for ( size_t i = 0; i < state.samplers.size(); i++ )
    {
        const auto& s = state.samplers[i];
        for ( int j = 0; j < shaderCount; j++ )
        {
            const int32_t slot = fnResolveSlot( s.name, s.slot, nvrhi::ResourceType::Sampler, shaders[j] );
            if ( slot < 0 )
                continue; // Having extra resources bound that the shader doesn't use is fair dinkum.

            const uint32_t stage = ( uint32_t ) ( shaders[j].flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            assert( scache.stageBinding[stage].get() );
            if ( scache.stageBinding[stage]->samplerTable.find( slot ) != scache.stageBinding[stage]->samplerTable.end() )
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
            scache.stageBinding[stage]->samplerTable[slot] = shandle;
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Sampler 0x%llx bound to slot %d '%s'\n", s.flags, slot, s.name ? s.name : "" );
        }
    }

    for ( size_t i = 0; i < state.textures.size(); i++ )
    {
        assert( scache.btex[i] );
        auto& btex = *scache.btex[i];

        const auto& t = state.textures[i];
        for ( int j = 0; j < shaderCount; j++ )
        {
            const nvrhi::ResourceType bindingType = t.computeUAV ? nvrhi::ResourceType::Texture_UAV : nvrhi::ResourceType::Texture_SRV;
            const int32_t slot = fnResolveSlot( t.name, t.slot, bindingType, shaders[j] );
            if ( slot < 0 )
                continue; // Having extra resources bound that the shader doesn't use is fair dinkum.

            const uint32_t stage = ( uint32_t ) ( shaders[j].flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            auto& stageTable = *scache.stageBinding[stage];

            if ( t.computeUAV )
            {
                auto& uavEntry = stageTable.uavTable[slot];
                if ( uavEntry.first.handle || uavEntry.second.handle )
                {
                    // If either part of the pair is already filled, that's a collision.
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Texture UAV Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                uavEntry.first = { btex.handle, &t };
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Texture UAV '%s' bound to slot %d '%s'\n", btex.name.c_str(), slot, t.name ? t.name : "" );
            }
            else
            { 
                if ( stageTable.textureTable.find( slot ) != stageTable.textureTable.end() )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Texture Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                stageTable.textureTable[slot] = { btex.handle, &t };
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Texture SRV '%s' bound to slot %d '%s'\n", btex.name.c_str(), slot, t.name ? t.name : "" );
            }
        }
    }

    for ( size_t i = 0; i < state.buffers.size(); i++ )
    {
        assert( scache.bbuf[i] );
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
            const int32_t slot = fnResolveSlot( b.name, b.slot, bindingType, shaders[j] );
            if ( slot < 0 )
                continue; // Having extra resources bound that the shader doesn't use is fair dinkum.

            const uint32_t stage = ( uint32_t ) ( shaders[j].flags & VRHI_SHADER_STAGE_MASK );
            assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );
            auto& stageTable = *scache.stageBinding[stage];

            if ( b.computeUAV )
            {
                auto& uavEntry = stageTable.uavTable[slot];
                if ( uavEntry.first.handle || uavEntry.second.handle )
                {
                    // If either part of the pair is already filled, that's a collision.
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Buffer UAV Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                uavEntry.second = { bbuf.handle, &b };
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Buffer UAV '%s' bound to slot %d '%s'\n", bbuf.name.c_str(), slot, b.name ? b.name : "" );
            }
            else
            {
                if ( stageTable.bufferTable.find( slot ) != stageTable.bufferTable.end() )
                {
                    if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Buffer Binding Slot Collision: Slot %d already bound by previous resource\n", slot );
                    return;
                }
                stageTable.bufferTable[slot] = { bbuf.handle, &b };
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "vhSetState(): Buffer SRV '%s' bound to slot %d '%s'\n", bbuf.name.c_str(), slot, b.name ? b.name : ""  );
            }
        }
    }

    scache.init = true;
}

bool vhCmdBackendState::BE_PreSubmitCommon_FindResource(
    const vhState& state,
    const uint32_t stage,
    const vhStateResolveCache& scache,
    const nvrhi::BindingLayoutItem& item,
    nvrhi::BindingSetItem& outItem
)
{
    assert( scache.init );
    assert( scache.btex.size() == state.textures.size() );
    assert( scache.bbuf.size() == state.buffers.size() );

    if ( scache.stageBinding.find( stage ) == scache.stageBinding.end() )
    {
        if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Stage %d not found in cache.\n", stage );
        return false;
    }
    const auto& stageTable = *scache.stageBinding.at( stage );

    switch ( item.type )
    {
        case nvrhi::ResourceType::ConstantBuffer:
        case nvrhi::ResourceType::VolatileConstantBuffer:
        {
            if ( item.slot == g_vhInit.shaderMake_bRegShift + 0 )
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
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: GlobalUniforms bound to slot %d\n", item.slot );
                return true;
            }
            if ( item.slot == g_vhInit.shaderMake_bRegShift + 1 )
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
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: WorldUniforms bound to slot %d\n", item.slot );
                return true;
            }

            auto it = stageTable.bufferTable.find( item.slot );
            if ( it == stageTable.bufferTable.end() )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: ConstantBuffer not found in cache at slot %d\n", item.slot );
                return false;
            }
            const auto result = &it->second;
            assert( result );
            if ( !result->handle )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: ConstantBuffer found in cache at slot %d but null handle.\n", item.slot );
                return false;
            }
            if ( !result->handle->getDesc().isConstantBuffer )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: ConstantBuffer found in cache at slot %d but NOT a ConstantBuffer.\n", item.slot );
                return false;
            }

            uint64_t size = result->binding->byteSize ? result->binding->byteSize : result->handle->getDesc().byteSize;
            nvrhi::BufferRange range( result->binding->byteOffset, size );
            outItem = nvrhi::BindingSetItem::ConstantBuffer( item.slot, result->handle, range );
            if ( result->handle->getDesc().isVolatile && item.type != nvrhi::ResourceType::VolatileConstantBuffer )
            {
                 if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Volatile Buffer bound to Static ConstantBuffer slot %d. This may be unsafe!\n", item.slot );
                return false;
            }
            outItem.type = item.type;
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: ConstantBuffer found in cache at slot %d\n", item.slot );
            return true;
        }

        case nvrhi::ResourceType::Texture_SRV:
        case nvrhi::ResourceType::Texture_UAV:
        {
            const bool isUAV = ( item.type == nvrhi::ResourceType::Texture_UAV );
            const vhStateResolveCache::ResolvedTexture* result = nullptr;

            if ( isUAV )
            {
                auto it = stageTable.uavTable.find( item.slot );
                if ( it != stageTable.uavTable.end() ) result = &it->second.first;
            }
            else
            {
                auto it = stageTable.textureTable.find( item.slot );
                if ( it != stageTable.textureTable.end() ) result = &it->second;
            }
            if ( !result )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Texture %s not found in cache at slot %d\n", isUAV ? "UAV" : "SRV", item.slot );
                break;
            }
            if ( !result->handle || !result->binding )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Texture %s found in cache at slot %d but invalid configuration.\n", isUAV ? "UAV" : "SRV", item.slot );
                return false;
            }

            outItem = isUAV ? nvrhi::BindingSetItem::Texture_UAV( item.slot, result->handle ) : nvrhi::BindingSetItem::Texture_SRV( item.slot, result->handle );

            outItem.format = result->binding->formatOverride;
            outItem.subresources = result->binding->subresources;
            outItem.dimension = result->binding->dimensionOverride;
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: Texture %s found in cache at slot %d\n", isUAV ? "UAV" : "SRV", item.slot );
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
                 auto it = stageTable.uavTable.find( item.slot );
                 if ( it != stageTable.uavTable.end() ) result = &it->second.second;
            }
            else
            {
                 auto it = stageTable.bufferTable.find( item.slot );
                 if ( it != stageTable.bufferTable.end() ) result = &it->second;
            }
            if ( !result )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Buffer %s not found in cache at slot %d\n", isUAV ? "UAV" : "SRV", item.slot );
                break;
            }
            if ( !result->handle || !result->binding )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Buffer %s found in cache at slot %d but invalid configuration.\n", isUAV ? "UAV" : "SRV", item.slot );
                assert( !"Buffer found in cache at slot but invalid configuration. This is likely a Vrhi bug." );
                return false;
            }
            
            // RawBuffer SRV and UAV require 16 byte alignment.
            if ( result->binding->byteOffset % 16 != 0 || result->binding->byteSize % 16 != 0 )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Offset and size must be aligned to 16 bytes for RawBuffer %ss.\n", isUAV ? "UAV" : "SRV" );
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
                        if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Unknown format for typed buffer." );
                        return false;
                    }
                    outItem = ( item.type == nvrhi::ResourceType::TypedBuffer_UAV ) 
                        ? nvrhi::BindingSetItem::TypedBuffer_UAV( item.slot, result->handle, format, range )
                        : nvrhi::BindingSetItem::TypedBuffer_SRV( item.slot, result->handle, format, range );
                    break;
                }
                case nvrhi::ResourceType::StructuredBuffer_SRV:
                case nvrhi::ResourceType::StructuredBuffer_UAV:
                {
                    if ( format == nvrhi::Format::UNKNOWN )
                    {
                        if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Unknown format for structured buffer." );
                        return false;
                    }
                    outItem = ( item.type == nvrhi::ResourceType::StructuredBuffer_UAV )
                        ? nvrhi::BindingSetItem::StructuredBuffer_UAV( item.slot, result->handle, format, range )
                        : nvrhi::BindingSetItem::StructuredBuffer_SRV( item.slot, result->handle, format, range );
                    break;
                }
                case nvrhi::ResourceType::RawBuffer_SRV:
                case nvrhi::ResourceType::RawBuffer_UAV:
                {
                    outItem = ( item.type == nvrhi::ResourceType::RawBuffer_UAV )
                        ? nvrhi::BindingSetItem::RawBuffer_UAV( item.slot, result->handle, range )
                        : nvrhi::BindingSetItem::RawBuffer_SRV( item.slot, result->handle, range );
                    break;
                }
                default: 
                    assert( !"Invalid resource type. This is likely a Vrhi bug." );
                    return false;
            }

            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: %s %s found in cache at slot %d\n", vhResourceTypeToString( item.type ), isUAV ? "UAV" : "SRV", item.slot );
            return true;
        }
        case nvrhi::ResourceType::Sampler:
        {
            auto it = stageTable.samplerTable.find( item.slot );
            if ( it == stageTable.samplerTable.end() )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Sampler not found in cache at slot %d\n", item.slot );
                break;
            }
            
            if ( !it->second )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Sampler found in cache at slot %d but handle is null\n", item.slot );
                return false;
            }
            outItem = nvrhi::BindingSetItem::Sampler( item.slot, it->second );
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_ALL_BINDINGS ) VRHI_LOG( "FindResource: Sampler found in cache at slot %d\n", item.slot );
            return true;
        }
        default:
            if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "FindResource: Unknown or unsupported resource type %d at slot %d\n", ( int ) item.type, item.slot );
            break;
    }

    return false;
}

bool vhCmdBackendState::BE_PreSubmitCommon_State(
    vhState& state,
    vhBackendShader* shaders,
    int shaderCount,
    nvrhi::ComputeState* computeState, // set to nullptr if not using compute.
    nvrhi::GraphicsState* graphicsState // set to nullptr if not using graphics.
)
{
    const nvrhi::BindingLayoutVector* psoLayouts = nullptr;
    if ( computeState ) psoLayouts = &computeState->pipeline->getDesc().bindingLayouts;
    if ( graphicsState ) psoLayouts = &graphicsState->pipeline->getDesc().bindingLayouts;
    if ( !psoLayouts )
    {
        VRHI_ERR( "vhSetState(): No PSO layout. This is likely a Vrhi bug.\n" );
        assert( !"No state or PSO layout" );
        return false;
    }
    const nvrhi::BindingLayoutVector& layouts = *psoLayouts;

    // Build map of hash --> psoLayouts.
    static std::unordered_map< uint64_t, const nvrhi::BindingLayoutHandle* > s_hashToPSOlayout;
    s_hashToPSOlayout.clear();
    for ( int i = 0; i < layouts.size(); i++ )
    {
        auto bdesc = layouts[i]->getDesc();
        if ( !bdesc ) continue;
        auto hash = vhHashBindingLayout( *bdesc );
        s_hashToPSOlayout[hash] = &layouts[i];
    }

    // Build map of layouts --> shader.

    s_layoutToShader.clear();
    for ( int shaderIdx = 0; shaderIdx < shaderCount; ++shaderIdx )
    {
        auto& shader = shaders[shaderIdx];
        if ( !BE_Util_ShaderStageMatches( shader.flags, computeState != nullptr, graphicsState != nullptr ) )
            continue;

        if ( !shader.layout )
        {
            VRHI_ERR( "vhSetState(): NULL shader layout. Stripped spirv-reflection?" );
            assert( !"NULL shader layout" );
            continue;
        }

        // Match shader.layout to equivalent state->pipeline->getDesc().bindingLayouts->layout.
        assert( shader.layout->getDesc() );
        auto hash = vhHashBindingLayout( *shader.layout->getDesc() );
        if ( s_hashToPSOlayout.find( hash ) == s_hashToPSOlayout.end() || !s_hashToPSOlayout[hash] )
        {
            VRHI_ERR( "vhSetState(): Mismatch between shader layout and PSO layout. This is likely a Vrhi bug." );
            assert( !"Mismatch between shader layout and PSO layout" );
            continue;
        }
        const auto& tempPSOLayout = *s_hashToPSOlayout[hash];
        assert( s_layoutToShader.find( tempPSOLayout ) == s_layoutToShader.end() ); // Duplicate layouts should be impossible.
        s_layoutToShader[ tempPSOLayout ] = &shader;
    }
    s_hashToPSOlayout.clear();

    // Loop through the layouts and bind resources.

    s_resolveCache.Clear();
    BE_PreSubmitCommon_ResolveStateCache( state, shaders, shaderCount, s_resolveCache );
    if ( !s_resolveCache.init )
    {
        VRHI_ERR( "vhSetState(): Failed to resolve state resource cache.\n" );
        return false;
    }

    for ( uint32_t layoutIdx = 0; layoutIdx < ( uint32_t ) layouts.size(); layoutIdx++ )
    {
        nvrhi::BindingSetDesc bsetDesc;

        auto layout = layouts[layoutIdx];
        assert( layout );
        auto layoutDesc = layout->getDesc();
        assert( layoutDesc );

        // Build a map of reflection slots --> reflection resources.

        s_slotToReflection.clear();
        assert( s_layoutToShader.find( layout ) != s_layoutToShader.end() );
        auto shader = s_layoutToShader[layout];
        assert( shader );
        for ( uint32_t i = 0; i < ( uint32_t ) shader->reflection.size(); i++ )
        {
            auto& reflection = shader->reflection[i];
            assert( s_slotToReflection.find( reflection.slot ) == s_slotToReflection.end() );
            s_slotToReflection[reflection.slot] = &reflection;
        }
        uint32_t stage = ( uint32_t ) ( shader->flags & VRHI_SHADER_STAGE_MASK );
        assert( stage > 0 && stage <= VRHI_SHADER_STAGE_MAX );

        // Loop through the required bindings for this layout.

        for ( uint32_t bindingIdx = 0; bindingIdx < layoutDesc->bindings.size(); bindingIdx++ )
        {
            auto binding = layoutDesc->bindings[bindingIdx];

            // Find the corresponding reflection resource.
            auto reflectionItr = s_slotToReflection.find( binding.slot );
            if ( reflectionItr == s_slotToReflection.end() )
            {
                if ( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) VRHI_ERR( "Binding Slot %d not found in shader reflection. Submit / Dispatch aborted.\n", binding.slot );
                return false;
            }
            assert( reflectionItr->second );
            const auto& reflection = *reflectionItr->second;

            // Validate the reflection against the layout.
            if ( !vhShaderValidateBinding( reflection, binding, !!( state.debugFlags & VRHI_STATE_DEBUG_LOG_BINDING_MISMATCH ) ) )
                return false;

            nvrhi::BindingSetItem item;
            if ( !BE_PreSubmitCommon_FindResource( state, stage, s_resolveCache, binding, item ) )
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
            return false;
        }

        if ( computeState )  computeState->addBindingSet( bset );
        if ( graphicsState ) graphicsState->addBindingSet( bset );
    }

    if ( graphicsState )
    {
        // Transfer some overlapping state from vhState to nvrhi::GraphicsState.

        graphicsState->viewport.viewports.resize( 0 );
        graphicsState->viewport.viewports.push_back( nvrhi::Viewport(
            state.viewRect.x, state.viewRect.x + state.viewRect.z,
            state.viewRect.y, state.viewRect.y + state.viewRect.w,
            0.0f, 1.0f
        ) );

        graphicsState->viewport.scissorRects.resize( 0 );
        graphicsState->viewport.scissorRects.push_back( nvrhi::Rect(
            ( int ) state.viewScissor.x, ( int ) ( state.viewScissor.x + state.viewScissor.z ),
            ( int ) state.viewScissor.y, ( int ) ( state.viewScissor.y + state.viewScissor.w )
        ) );

        // nvrhi::DepthStencilState::dynamicStencilRefValue is false, but we set this any way because it's fun.
        graphicsState->dynamicStencilRefValue = ( uint8_t ) ( ( state.frontStencil & VRHI_STENCIL_FUNC_REF_MASK ) >> VRHI_STENCIL_FUNC_REF_SHIFT );

        // Bind Framebuffer
        graphicsState->framebuffer = BE_GetFrameBuffer( state.colourAttachment, state.depthAttachment );
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
    return true;
}

void vhCmdBackendState::BE_Dispatch( vhState& state, vhBackendShader& computeShader, glm::uvec3 workGroupCount )
{
    // Suggested Implementation:
    // Get Compute Queue command list: auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Compute );
    // Create/Get Compute Pipeline (using nvrhi::ComputePipelineDesc with computeShader.handle).
    // Bind Compute Pipeline to cmdlist.
    // Bind Resources (descriptors, push constants, uniforms) to cmdlist (using state).
    // IMPORTANT: Skip Viewport/Scissor as they are not for compute.
    // cmdlist->dispatch( workGroupCount.x, workGroupCount.y, workGroupCount.z );

    assert( computeShader.handle );

    nvrhi::ComputePipelineDesc desc;
    if ( !BE_PresubmitCommon_PipelineDesc( state, &computeShader, 1, &desc, nullptr ) )
    {
        VRHI_ERR( "vhDispatch() : Failed to create nvrhi::ComputePipelineDesc for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        return;
    }

    nvrhi::ComputePipelineHandle pso = vhPSOCacheGet( desc );
    if ( !pso )
    {
        VRHI_ERR( "vhDispatch() : Failed to create nvrhi::ComputePipelineHandle PSO for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        return;
    }

    nvrhi::ComputeState cstate;
    cstate.setPipeline( pso.Get() );
    if ( !BE_PreSubmitCommon_State( state, &computeShader, 1, &cstate, nullptr ) )
    {
        VRHI_ERR( "vhDispatch() : Failed to create nvrhi::ComputeState for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->setComputeState( cstate );
        cmdlist->dispatch( workGroupCount.x, workGroupCount.y, workGroupCount.z );
    }
}

void vhCmdBackendState::BE_DispatchIndirect( vhState& state, vhBackendShader& computeShader, vhBackendBuffer& indirectBuffer, uint64_t byteOffset )
{
    assert( computeShader.handle );
    assert( indirectBuffer.handle );

    if ( !( indirectBuffer.flags & VRHI_BUFFER_DRAW_INDIRECT ) )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Indirect buffer %s was not created with VRHI_BUFFER_DRAW_INDIRECT! SKIPPING COMPUTE DISPATCH.\n", indirectBuffer.name.c_str() );
        return;
    }

    nvrhi::ComputePipelineDesc desc;
    if ( !BE_PresubmitCommon_PipelineDesc( state, &computeShader, 1, &desc, nullptr ) )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Failed to create nvrhi::ComputePipelineDesc for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        return;
    }

    nvrhi::ComputePipelineHandle pso = vhPSOCacheGet( desc );
    if ( !pso )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Failed to create nvrhi::ComputePipelineHandle PSO for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        return;
    }

    nvrhi::ComputeState cstate;
    cstate.setPipeline( pso.Get() );
    if ( !BE_PreSubmitCommon_State( state, &computeShader, 1, &cstate, nullptr ) )
    {
        VRHI_ERR( "BE_DispatchIndirect() : Failed to create nvrhi::ComputeState for shader %p! SKIPPING COMPUTE DISPATCH.\n", computeShader.handle.Get() );
        return;
    }

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->setComputeState( cstate );
        cmdlist->setIndirectParams( indirectBuffer.handle );
        cmdlist->dispatchIndirect( ( uint32_t ) byteOffset );
    }
}

void vhCmdBackendState::BE_BlitBuffer( vhBackendBuffer& dst, vhBackendBuffer& src, uint64_t dstOffset, uint64_t srcOffset, uint64_t size )
{
    // Should already have been validated by handler.
    assert( dst.handle );
    assert( src.handle );
    assert( dstOffset + size <= dst.desc.byteSize );
    assert( srcOffset + size <= src.desc.byteSize );

    auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        cmdlist->copyBuffer( dst.handle, dstOffset, src.handle, srcOffset, size );
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
    }
}

void vhCmdBackendState::shutdown()
{
    std::lock_guard< std::mutex > lock( backendMutex );
    std::lock_guard< std::mutex > lock2( g_nvRHIStateMutex );

    m_globalUniformBuffer.Shutdown_DeviceStateLocked();
    m_worldUniformBuffer.Shutdown_DeviceStateLocked();

    backendTextures.clear();
    backendBuffers.clear();
    backendShaders.clear();

    // Clear static caches
    s_layoutToShader.clear();
    s_resolveCache.Clear();
    s_slotToReflection.clear();
    s_layoutLocationTable.clear();
    s_attributes.clear();
}

void vhCmdBackendState::HandleLogFunction( const char* str )
{
    if ( g_vhInit.logBackendCmds ) VRHI_LOG( "BackendCmd: %s\n", str );
}

// --------------------------------------------------------------------------
// Backend :: VIDL Command Handlers
// --------------------------------------------------------------------------

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
        backendTextures[cmd->texture] = std::make_unique<vhBackendTexture>();
    }
}

void vhCmdBackendState::Handle_vhResizeCleanup( VIDL_vhResizeCleanup* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    vhFBOCacheReset();
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
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    if ( cmd->texture == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhCreateTexture() : Invalid texture handle!\n" );
        return;
    }
    if ( cmd->dimensions.x <= 0 || cmd->dimensions.y <= 0 || cmd->dimensions.z <= 0 ||
        cmd->numMips == 0 || cmd->numLayers <= 0 || cmd->format == nvrhi::Format::UNKNOWN )
    {
        VRHI_ERR( "vhCreateTexture() : Invalid parameters! TexID %u %d x %d x %d mips %d layers %d format %d\n",
            cmd->texture, cmd->dimensions.x, cmd->dimensions.y, cmd->dimensions.z, cmd->numMips, cmd->numLayers, cmd->format );
        return;
    }

    // Create the NVRHI texture.
    snprintf( temps, sizeof( temps ), "Texture %d", cmd->texture );
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
        .enableAutomaticStateTracking( nvrhi::ResourceStates::ShaderResource )
        .setDebugName( temps );

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

    // Calculate expected data size for the range.
    int32_t mipStart = cmd->startMips, mipEnd = cmd->startMips + cmd->numMips;
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

    glm::ivec4 range = glm::ivec4( cmd->startMips, cmd->startMips + cmd->numMips, cmd->startLayers, cmd->startLayers + cmd->numLayers );
    BE_UpdateTexture( btex, cmd->data, range );
}

void vhCmdBackendState::Handle_vhReadTextureSlow( VIDL_vhReadTextureSlow* cmd )
{
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
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    Handle_vhUpdateBufferCommon_Internal( "vhUpdateVertexBuffer", cmd->buffer, cmd->offsetVerts, cmd->data, cmd->numVerts, true );
}

void vhCmdBackendState::Handle_vhCreateIndexBuffer( VIDL_vhCreateIndexBuffer* cmd )
{
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
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    Handle_vhUpdateBufferCommon_Internal( "vhUpdateIndexBuffer", cmd->buffer, cmd->offsetIndices, cmd->data, cmd->numIndices, false );
}

void vhCmdBackendState::Handle_vhCreateUniformBuffer( VIDL_vhCreateUniformBuffer* cmd )
{
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
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    // Reuse common update logic (isVertexBuffer = true uses stride from creation)
    Handle_vhUpdateBufferCommon_Internal( "vhUpdateUniformBuffer", cmd->buffer, cmd->offset, cmd->data, cmd->size, true );
}

void vhCmdBackendState::Handle_vhCreateStorageBuffer( VIDL_vhCreateStorageBuffer* cmd )
{
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
    BE_CmdRAII cmdRAII( cmd );
    auto dataRAII = BE_MemRAII( cmd->data );

    // Reuse common update logic (isVertexBuffer = true uses stride from creation)
    Handle_vhUpdateBufferCommon_Internal( "vhUpdateStorageBuffer", cmd->buffer, cmd->offset, cmd->data, cmd->size, true );
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
    state.clearRgba = cmd->rgba;
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
    state.frontStencil = cmd->front;
    state.backStencil = cmd->back;
}

void vhCmdBackendState::Handle_vhCmdSetStateVertexBuffer( VIDL_vhCmdSetStateVertexBuffer* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    auto& state = backendStates[cmd->id];
    if ( cmd->stream >= state.vertexBindings.size() ) state.vertexBindings.resize( cmd->stream + 1 );
    state.vertexBindings[cmd->stream] = { cmd->buffer, cmd->stream, cmd->start, cmd->num, cmd->offset };
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

void vhCmdBackendState::Handle_vhFlushInternal( VIDL_vhFlushInternal* cmd )
{
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

    // Send it!!
    vhCmdListFlushAll_DeviceStateLocked();

    // Free all cmd memory allocations, because hitting this flush means all previous commands have been processed.
    {
        std::lock_guard< std::mutex > lock( g_vhMemListMutex );
        g_vhMemList.clear();
    }
    if ( cmd->waitForGPU )
    {
        g_vhDevice->waitForIdle();
    }
    g_vhDevice->runGarbageCollection();

    // Notify caller that we're done.
    // Safety warning : fence is probably from stack of caller
    if ( cmd->fence )
        cmd->fence->store( true );
}

void vhCmdBackendState::Handle_vhDispatch( VIDL_vhDispatch* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->stateID == VRHI_INVALID_HANDLE || cmd->workGroupCount.x == 0 || cmd->workGroupCount.y == 0 || cmd->workGroupCount.z == 0 ) return;

    // Ensure state exists
    auto itState = backendStates.find( cmd->stateID );
    if ( itState == backendStates.end() )
    {
        VRHI_ERR( "vhDispatch: State %llu not found!\n", cmd->stateID );
        return;
    }
    auto& state = itState->second;

    if ( state.program.empty() )
    {
        VRHI_ERR( "vhDispatch: State %llu has no program set!\n", cmd->stateID );
        return;
    }

    auto itShader = backendShaders.find( state.program[0] );
    if ( itShader == backendShaders.end() )
    {
        VRHI_ERR( "vhDispatch: Shader %llu not found for state %llu!\n", state.program[0], cmd->stateID );
        return;
    }

    BE_Dispatch( state, *itShader->second, cmd->workGroupCount );
}

void vhCmdBackendState::Handle_vhDispatchIndirect( VIDL_vhDispatchIndirect* cmd )
{
    BE_CmdRAII cmdRAII( cmd );
    if ( cmd->stateID == VRHI_INVALID_HANDLE || cmd->indirectBuffer == VRHI_INVALID_HANDLE ) return;

    auto itBuf = backendBuffers.find( cmd->indirectBuffer );
    if ( itBuf == backendBuffers.end() )
    {
        VRHI_ERR( "vhDispatchIndirect: Indirect buffer %d not found!\n", cmd->indirectBuffer );
        return;
    }

    auto itState = backendStates.find( cmd->stateID );
    if ( itState == backendStates.end() )
    {
        VRHI_ERR( "vhDispatchIndirect: State %llu not found!\n", cmd->stateID );
        return;
    }
    auto& state = itState->second;

    if ( state.program.empty() )
    {
        VRHI_ERR( "vhDispatchIndirect: State %llu has no program set!\n", cmd->stateID );
        return;
    }

    auto itShader = backendShaders.find( state.program[0] );
    if ( itShader == backendShaders.end() )
    {
        VRHI_ERR( "vhDispatchIndirect: Shader %llu not found for state %llu!\n", state.program[0], cmd->stateID );
        return;
    }

    BE_DispatchIndirect( state, *itShader->second, *itBuf->second, cmd->byteOffset );
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

void* vhCmdBackendState::QueryTextureHandle( vhTexture handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendTextures.find( handle );
    if ( it == backendTextures.end() || !it->second )
    {
        return nullptr;
    }
    return it->second->handle.Get();
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

void* vhCmdBackendState::QueryBufferHandle( vhBuffer handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendBuffers.find( handle );
    if ( it == backendBuffers.end() || !it->second )
    {
        return nullptr;
    }
    return it->second->handle.Get();
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

void* vhCmdBackendState::QueryShaderHandle( vhShader handle )
{
    std::lock_guard< std::mutex > lock( backendMutex );
    auto it = backendShaders.find( handle );
    if ( it == backendShaders.end() || !it->second )
    {
        return nullptr;
    }
    return it->second->handle.Get();
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

void* vhBackendQueryTextureHandle( vhTexture texture )
{
    return g_vhCmdBackendState.QueryTextureHandle( texture );
}

uint64_t vhBackendQueryBufferInfo( vhBuffer buffer, uint32_t* outStride, uint64_t* outFlags )
{
    return g_vhCmdBackendState.QueryBufferInfo( buffer, outStride, outFlags );
}

void* vhBackendQueryBufferHandle( vhBuffer buffer )
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

void* vhBackendQueryShaderHandle( vhShader shader )
{
    return g_vhCmdBackendState.QueryShaderHandle( shader );
}

bool vhBackendQueryState( vhStateId id, vhState& outState )
{
    return g_vhCmdBackendState.QueryState( id, outState );
}
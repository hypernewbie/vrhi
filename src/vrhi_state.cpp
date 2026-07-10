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

// ------------ State Implementation ------------

// Query state from backend (fast-path direct access)
bool vhGetState( vhStateId id, vhState& outState )
{
    return vhBackendQueryState( id, outState );
}

void vhCmdSetStateViewRect( vhStateId id, glm::vec4 rect )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateViewRect >( id, rect ) );
}

void vhCmdSetStateViewScissor( vhStateId id, glm::vec4 scissor )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateViewScissor >( id, scissor ) );
}

void vhCmdSetStateViewClear( vhStateId id, uint16_t flags, glm::vec4 color, glm::u8vec4 colorUInt, float depth, uint8_t stencil )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateViewClear >( id, flags, color, colorUInt, depth, stencil ) );
}

void vhCmdSetStateProgram( vhStateId id, vhProgram program )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateProgram >( id, program ) );
}

void vhCmdSetStateViewTransform( vhStateId id, glm::mat4 view, glm::mat4 proj )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateViewTransform >( id, view, proj ) );
}

void vhCmdSetStateWorldTransform( vhStateId id, const std::vector< glm::mat4 >& matrices )
{
    vhCmdEnqueue( vhCmdAllocMat4Span< VIDL_vhCmdSetStateWorldTransform >( id, matrices ) );
}

void vhCmdSetStateFlags( vhStateId id, uint64_t flags )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateFlags >( id, flags ) );
}

void vhCmdSetStateDebugFlags( vhStateId id, uint64_t flags )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateDebugFlags >( id, flags ) );
}

void vhCmdSetStateStencil( vhStateId id, uint64_t stencilState )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateStencil >( id, stencilState ) );
}

void vhCmdSetStateDepthBias( vhStateId id, int bias, float clamp, float slopeScaled )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateDepthBias >( id, bias, clamp, slopeScaled ) );
}

void vhCmdSetStateVertexBindings( vhStateId id, const std::vector< vhState::VertexBinding >& bindings )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateVertexBindings >( id, bindings ) );
}

void vhCmdSetStateVertexBuffer( vhStateId id, uint8_t stream, vhBuffer buffer, uint64_t offset, uint32_t start, uint32_t num, const vhVertexLayout& layoutOverride, bool isInstanced )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateVertexBuffer >( id, stream, buffer, offset, start, num, layoutOverride, isInstanced ) );
}

void vhCmdSetStateIndexBuffer( vhStateId id, vhBuffer buffer, uint64_t offset, uint32_t first, uint32_t num )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateIndexBuffer >( id, buffer, offset, first, num ) );
}

void vhCmdSetStateTextures( vhStateId id, const std::vector< vhState::TextureBinding >& textures )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateTextures >( id, vhArenaCopySpan( textures ) ) );
}

void vhCmdSetStateSamplers( vhStateId id, const std::vector< vhState::SamplerDefinition >& samplers )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateSamplers >( id, vhArenaCopySpan( samplers ) ) );
}

void vhCmdSetStateBuffers( vhStateId id, const std::vector< vhState::BufferBinding >& buffers )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateBuffers >( id, vhArenaCopySpan( buffers ) ) );
}

void vhCmdSetStateAccelStructs( vhStateId id, const std::vector< vhState::AccelStructBinding >& accelStructs )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateAccelStructs >( id, vhArenaCopySpan( accelStructs ) ) );
}

void vhCmdSetStateDescriptorTables( vhStateId id, const std::vector< vhState::DescriptorTableBinding >& tables )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateDescriptorTables >( id, vhArenaCopySpan( tables ) ) );
}

void vhCmdSetStateConstants( vhStateId id, const std::vector< vhState::ConstantBufferValue >& constants )
{
    vhCmdEnqueue( vhCmdAllocNamedVec4Span< VIDL_vhCmdSetStateConstants, vhArenaConstantValue >( id, constants ) );
}

void vhCmdSetStatePushConstants( vhStateId id, glm::vec4 data )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStatePushConstants >( id, data ) );
}

void vhCmdSetStateUniforms( vhStateId id, const std::vector< vhState::UniformBufferValue >& uniforms )
{
    vhCmdEnqueue( vhCmdAllocNamedVec4Span< VIDL_vhCmdSetStateUniforms, vhArenaUniformValue >( id, uniforms ) );
}

void vhCmdSetStateAttachments( vhStateId id, const std::vector< vhState::RenderTarget >& colors, vhState::RenderTarget depth )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateAttachments >( id, vhArenaCopySpan( colors ), depth ) );
}

void vhCmdSetStateBlendConstants( vhStateId id, glm::vec4 blendConst )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateBlendConstants >( id, blendConst ) );
}

void vhCmdSetStateViewDepthRange( vhStateId id, float minZ, float maxZ )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateViewDepthRange >( id, minZ, maxZ ) );
}

void vhCmdSetStateShadingRate( vhStateId id, uint32_t flags, vhTexture image )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateShadingRate >( id, flags, image ) );
}

void vhCmdSetStateIndirectParams( vhStateId id, vhBuffer buffer, uint64_t offset )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateIndirectParams >( id, buffer, offset ) );
}

void vhCmdSetStateIndirectCountBuffer( vhStateId id, vhBuffer buffer, uint64_t offset )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhCmdSetStateIndirectCountBuffer >( id, buffer, offset ) );
}

bool vhSetState( vhStateId id, vhState& state, uint64_t dirtyForceMask )
{
    uint64_t dirty = state.dirty | dirtyForceMask;
    if ( !dirty ) return true;

    if ( dirty & VRHI_DIRTY_VIEWPORT )
    {
        vhCmdSetStateViewRect( id, state.viewRect );
        vhCmdSetStateViewScissor( id, state.viewScissor );
        vhCmdSetStateViewClear( id, state.clearFlags, state.clearColor, state.clearColorUInt, state.clearDepth, state.clearStencil );
    }

    if ( dirty & VRHI_DIRTY_ATTACHMENTS )
    {
        vhCmdSetStateAttachments( id, state.colourAttachment, state.depthAttachment );
    }

    if ( dirty & VRHI_DIRTY_CAMERA )
    {
        vhCmdSetStateViewTransform( id, state.viewMatrix, state.projMatrix );
    }

    if ( dirty & VRHI_DIRTY_WORLD )
    {
        vhCmdSetStateWorldTransform( id, state.worldMatrix );
    }

    if ( dirty & VRHI_DIRTY_PIPELINE )
    {
        vhCmdSetStateFlags( id, state.stateFlags );
        vhCmdSetStateDebugFlags( id, state.debugFlags );
        vhCmdSetStateStencil( id, state.stencilState );
    }

    if ( dirty & VRHI_DIRTY_DEPTH_BIAS )
    {
        vhCmdSetStateDepthBias( id, state.depthBias, state.depthBiasClamp, state.slopeScaledDepthBias );
    }

    if ( dirty & VRHI_DIRTY_VERTEX_INDEX )
    {
        vhCmdSetStateVertexBindings( id, state.vertexBindings );
        vhCmdSetStateIndexBuffer( id, state.indexBinding.buffer, state.indexBinding.byteOffset, state.indexBinding.firstIndex, state.indexBinding.numIndices );
    }

    if ( dirty & VRHI_DIRTY_TEXTURE_SAMPLERS )
    {
        vhCmdSetStateTextures( id, state.textures );
        vhCmdSetStateSamplers( id, state.samplers );
    }

    if ( dirty & VRHI_DIRTY_BUFFERS )
    {
        vhCmdSetStateBuffers( id, state.buffers );
    }

    if ( dirty & VRHI_DIRTY_ACCEL_STRUCT )
    {
        vhCmdSetStateAccelStructs( id, state.accelStructs );
    }

    if ( dirty & VRHI_DIRTY_DESCRIPTOR_TABLES )
    {
        vhCmdSetStateDescriptorTables( id, state.descriptorTables );
    }

    if ( dirty & VRHI_DIRTY_CONSTANTS )
    {
        vhCmdSetStateConstants( id, state.constants );
    }

    if ( dirty & VRHI_DIRTY_PUSH_CONSTANTS )
    {
        vhCmdSetStatePushConstants( id, state.pushConstants );
    }

    if ( dirty & VRHI_DIRTY_PROGRAM )
    {
        vhCmdSetStateProgram( id, state.program );
    }

    if ( dirty & VRHI_DIRTY_UNIFORMS )
    {
    vhCmdSetStateUniforms( id, state.uniforms );
    }

    if ( dirty & VRHI_DIRTY_PIPELINE )
    {
        vhCmdSetStateBlendConstants( id, state.blendConstantColor );
    }

    if ( dirty & VRHI_DIRTY_VIEWPORT )
    {
        vhCmdSetStateViewDepthRange( id, state.viewDepthRange.x, state.viewDepthRange.y );
    }

    if ( dirty & VRHI_DIRTY_VRS )
    {
        vhCmdSetStateShadingRate( id, state.shadingRateFlags, state.shadingRateImage );
    }

    if ( dirty & VRHI_DIRTY_INDIRECT )
    {
        vhCmdSetStateIndirectParams( id, state.indirectParams.buffer, state.indirectParams.byteOffset );
    }
    if ( dirty & VRHI_DIRTY_INDIRECT_COUNT )
    {
        vhCmdSetStateIndirectCountBuffer( id, state.indirectCountBuffer.buffer, state.indirectCountBuffer.byteOffset );
    }

    state.dirty = 0x0ull;
    return true;
}

nvrhi::VariableShadingRate vhTranslateShadingRate( uint32_t rate )
{
    switch ( rate )
    {
        case VRHI_VRS_1X1: return nvrhi::VariableShadingRate::e1x1;
        case VRHI_VRS_1X2: return nvrhi::VariableShadingRate::e1x2;
        case VRHI_VRS_2X1: return nvrhi::VariableShadingRate::e2x1;
        case VRHI_VRS_2X2: return nvrhi::VariableShadingRate::e2x2;
        case VRHI_VRS_2X4: return nvrhi::VariableShadingRate::e2x4;
        case VRHI_VRS_4X2: return nvrhi::VariableShadingRate::e4x2;
        case VRHI_VRS_4X4: return nvrhi::VariableShadingRate::e4x4;
        default: return nvrhi::VariableShadingRate::e1x1;
    }
}

nvrhi::ShadingRateCombiner vhTranslateShadingRateCombiner( uint32_t combiner )
{
    switch ( combiner )
    {
        case VRHI_VRS_COMBINER_PASSTHROUGH: return nvrhi::ShadingRateCombiner::Passthrough;
        case VRHI_VRS_COMBINER_OVERRIDE:    return nvrhi::ShadingRateCombiner::Override;
        case VRHI_VRS_COMBINER_MIN:         return nvrhi::ShadingRateCombiner::Min;
        case VRHI_VRS_COMBINER_MAX:         return nvrhi::ShadingRateCombiner::Max;
        case VRHI_VRS_COMBINER_SUM:         return nvrhi::ShadingRateCombiner::ApplyRelative;
        default: return nvrhi::ShadingRateCombiner::Passthrough;
    }
}

nvrhi::PrimitiveType vhTranslatePrimitiveType( uint64_t stateFlags )
{
    uint32_t pt = ( uint32_t ) ( ( stateFlags & VRHI_STATE_PT_MASK ) >> VRHI_STATE_PT_SHIFT );
    switch ( pt )
    {
        case 0:  return nvrhi::PrimitiveType::TriangleList;
        case 1:  return nvrhi::PrimitiveType::TriangleStrip;
        case 2:  return nvrhi::PrimitiveType::LineList;
        case 3:  return nvrhi::PrimitiveType::LineStrip;
        case 4:  return nvrhi::PrimitiveType::PointList;
        default: return nvrhi::PrimitiveType::TriangleList;
    }
}

void vhSetPushConstant_DeviceStateLocked( nvrhi::CommandListHandle cmdList, const vhState& state, size_t pipelineSizeBytes )
{
    struct PushConstantData
    {
        glm::vec4 userData;
        glm::mat4 modelViewProj;
    } pushData;

    pushData.userData = state.pushConstants;
    if ( !state.worldMatrix.empty() )
        pushData.modelViewProj = state.projMatrix * state.viewMatrix * state.worldMatrix[0];
    else
        pushData.modelViewProj = state.projMatrix * state.viewMatrix;

    size_t writeSize = sizeof( pushData );
    if ( pipelineSizeBytes != 0 && pipelineSizeBytes < writeSize ) writeSize = pipelineSizeBytes;
    cmdList->setPushConstants( &pushData, writeSize );
}

size_t vhPushConstantSize( const std::vector< vhPushConstantRange >& ranges )
{
    size_t s = 0;
    for ( const auto& r : ranges ) s = std::max< size_t >( s, ( size_t ) r.offset + r.size );
    return s;
}

nvrhi::BlendState vhTranslateBlendState( uint64_t stateFlags )
{
    nvrhi::BlendState blendState;

    auto fnConvertBlendFactor = []( uint32_t factor ) -> nvrhi::BlendFactor
    {
        switch ( factor )
        {
            case 1: return nvrhi::BlendFactor::Zero;
            case 2: return nvrhi::BlendFactor::One;
            case 3: return nvrhi::BlendFactor::SrcColor;
            case 4: return nvrhi::BlendFactor::InvSrcColor;
            case 5: return nvrhi::BlendFactor::SrcAlpha;
            case 6: return nvrhi::BlendFactor::InvSrcAlpha;
            case 7: return nvrhi::BlendFactor::DstAlpha;
            case 8: return nvrhi::BlendFactor::InvDstAlpha;
            case 9: return nvrhi::BlendFactor::DstColor;
            case 10: return nvrhi::BlendFactor::InvDstColor;
            case 11: return nvrhi::BlendFactor::SrcAlphaSaturate;
            case 12: return nvrhi::BlendFactor::ConstantColor;
            case 13: return nvrhi::BlendFactor::InvConstantColor;
            default: return nvrhi::BlendFactor::One;
        }
    };

    auto fnConvertBlendOp = []( uint32_t op ) -> nvrhi::BlendOp
    {
        switch ( op )
        {
            case 0: return nvrhi::BlendOp::Add;
            case 1: return nvrhi::BlendOp::Subtract;
            case 2: return nvrhi::BlendOp::ReverseSubtract;
            case 3: return nvrhi::BlendOp::Min;
            case 4: return nvrhi::BlendOp::Max;
            default: return nvrhi::BlendOp::Add;
        }
    };

    // Write Masks
    nvrhi::ColorMask mask = ( nvrhi::ColorMask ) 0;
    if ( stateFlags & VRHI_STATE_WRITE_R ) mask = mask | nvrhi::ColorMask::Red;
    if ( stateFlags & VRHI_STATE_WRITE_G ) mask = mask | nvrhi::ColorMask::Green;
    if ( stateFlags & VRHI_STATE_WRITE_B ) mask = mask | nvrhi::ColorMask::Blue;
    if ( stateFlags & VRHI_STATE_WRITE_A ) mask = mask | nvrhi::ColorMask::Alpha;
    blendState.targets[0].colorWriteMask = mask;

    // Blend Factors
    uint32_t blendBits = ( uint32_t ) ( ( stateFlags & VRHI_STATE_BLEND_MASK ) >> VRHI_STATE_BLEND_SHIFT );
    if ( blendBits != 0 )
    {
        blendState.targets[0].blendEnable = true;
        blendState.targets[0].srcBlend = fnConvertBlendFactor( blendBits & 0xF );
        blendState.targets[0].destBlend = fnConvertBlendFactor( ( blendBits >> 4 ) & 0xF );
        blendState.targets[0].srcBlendAlpha = fnConvertBlendFactor( ( blendBits >> 8 ) & 0xF );
        blendState.targets[0].destBlendAlpha = fnConvertBlendFactor( ( blendBits >> 12 ) & 0xF );
    }

    // Blend Equation
    uint32_t blendEq = ( uint32_t ) ( ( stateFlags & VRHI_STATE_BLEND_EQUATION_MASK ) >> VRHI_STATE_BLEND_EQUATION_SHIFT );
    if ( blendEq != 0 )
    {
        blendState.targets[0].blendOp = fnConvertBlendOp( blendEq & 0x7 );
        blendState.targets[0].blendOpAlpha = fnConvertBlendOp( ( blendEq >> 3 ) & 0x7 );
    }

    // Propagate duplicate settings to all other targets
    for ( int i = 1; i < nvrhi::c_MaxRenderTargets; ++i )
    {
        blendState.targets[i] = blendState.targets[0];
    }

    // Independent per-RT blend: 11 bits per slot in stateFlags (4 src factor, 4 dst factor, 3 equation).
    if ( stateFlags & VRHI_STATE_BLEND_INDEPENDENT )
    {
        for ( int i = 0; i < 3; i++ )
        {
            uint32_t bits = ( uint32_t ) ( ( stateFlags >> ( i * 11 ) ) & 0x7FFu );
            if ( bits == 0 ) continue;
            uint32_t srcF = ( bits >> 0 ) & 0xF;
            uint32_t dstF = ( bits >> 4 ) & 0xF;
            uint32_t equ  = ( bits >> 8 ) & 0x7;
            auto& t = blendState.targets[i];
            t.blendEnable    = true;
            t.srcBlend       = fnConvertBlendFactor( srcF );
            t.destBlend      = fnConvertBlendFactor( dstF );
            t.srcBlendAlpha  = fnConvertBlendFactor( srcF );
            t.destBlendAlpha = fnConvertBlendFactor( dstF );
            if ( equ != 0 )
            {
                t.blendOp      = fnConvertBlendOp( equ );
                t.blendOpAlpha = fnConvertBlendOp( equ );
            }
        }
    }

    blendState.alphaToCoverageEnable = ( stateFlags & VRHI_STATE_BLEND_ALPHA_TO_COVERAGE ) != 0;

    return blendState;
}

nvrhi::DepthStencilState vhTranslateDepthStencilState( uint64_t stateFlags, uint64_t stencilState )
{
    nvrhi::DepthStencilState dsState;

    auto fnConvertComparisonFunc = []( uint64_t func ) -> nvrhi::ComparisonFunc
    {
        switch ( func )
        {
            case 1: return nvrhi::ComparisonFunc::Less;
            case 2: return nvrhi::ComparisonFunc::LessOrEqual;
            case 3: return nvrhi::ComparisonFunc::Equal;
            case 4: return nvrhi::ComparisonFunc::GreaterOrEqual;
            case 5: return nvrhi::ComparisonFunc::Greater;
            case 6: return nvrhi::ComparisonFunc::NotEqual;
            case 7: return nvrhi::ComparisonFunc::Never;
            case 8: return nvrhi::ComparisonFunc::Always;
            default: return nvrhi::ComparisonFunc::Less;
        }
    };

    dsState.depthWriteEnable = ( stateFlags & VRHI_STATE_WRITE_Z ) != 0;

    uint64_t depthFunc = ( uint64_t ) ( ( stateFlags & VRHI_STATE_DEPTH_TEST_MASK ) >> VRHI_STATE_DEPTH_TEST_SHIFT );
    if ( depthFunc != 0 )
    {
        dsState.depthTestEnable = true;
        dsState.depthFunc = fnConvertComparisonFunc( depthFunc );
    }
    else if ( stateFlags & VRHI_STATE_DEPTH_TEST_ENABLE )
    {
        dsState.depthTestEnable = true;
        dsState.depthFunc = nvrhi::ComparisonFunc::Less; // Default comparison when only enable flag is set
    }
    else
    {
        dsState.depthTestEnable = false;
        dsState.depthFunc = nvrhi::ComparisonFunc::Less;
    }

    auto fnConvertStencilOp = []( uint64_t op ) -> nvrhi::StencilOp
    {
        switch ( op )
        {
            case 0: return nvrhi::StencilOp::Zero;
            case 1: return nvrhi::StencilOp::Keep;
            case 2: return nvrhi::StencilOp::Replace;
            case 3: return nvrhi::StencilOp::IncrementAndWrap;
            case 4: return nvrhi::StencilOp::IncrementAndClamp;
            case 5: return nvrhi::StencilOp::DecrementAndWrap;
            case 6: return nvrhi::StencilOp::DecrementAndClamp;
            case 7: return nvrhi::StencilOp::Invert;
            default: return nvrhi::StencilOp::Keep;
        }
    };

    auto fnUnpackStencilFront = [&]( uint64_t packed, nvrhi::DepthStencilState::StencilOpDesc& desc )
    {
        uint64_t failOp = ( packed & VRHI_STENCIL_OP_FAIL_S_MASK ) >> VRHI_STENCIL_OP_FAIL_S_SHIFT;
        uint64_t depthFailOp = ( packed & VRHI_STENCIL_OP_FAIL_Z_MASK ) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT;
        uint64_t passOp = ( packed & VRHI_STENCIL_OP_PASS_Z_MASK ) >> VRHI_STENCIL_OP_PASS_Z_SHIFT;
        uint64_t func = ( packed & VRHI_STENCIL_TEST_MASK ) >> VRHI_STENCIL_TEST_SHIFT;

        desc.failOp = fnConvertStencilOp( failOp );
        desc.depthFailOp = fnConvertStencilOp( depthFailOp );
        desc.passOp = fnConvertStencilOp( passOp );
        desc.stencilFunc = fnConvertComparisonFunc( func );
    };

    auto fnUnpackStencilBack = [&]( uint64_t packed, nvrhi::DepthStencilState::StencilOpDesc& desc )
    {
        uint64_t failOp = ( packed & VRHI_STENCIL_BACK_OP_FAIL_S_MASK ) >> VRHI_STENCIL_BACK_OP_FAIL_S_SHIFT;
        uint64_t depthFailOp = ( packed & VRHI_STENCIL_BACK_OP_FAIL_Z_MASK ) >> VRHI_STENCIL_BACK_OP_FAIL_Z_SHIFT;
        uint64_t passOp = ( packed & VRHI_STENCIL_BACK_OP_PASS_Z_MASK ) >> VRHI_STENCIL_BACK_OP_PASS_Z_SHIFT;
        uint64_t func = ( packed & VRHI_STENCIL_BACK_TEST_MASK ) >> VRHI_STENCIL_BACK_TEST_SHIFT;

        desc.failOp = fnConvertStencilOp( failOp );
        desc.depthFailOp = fnConvertStencilOp( depthFailOp );
        desc.passOp = fnConvertStencilOp( passOp );
        desc.stencilFunc = fnConvertComparisonFunc( func );
    };

    if ( stencilState != VRHI_STENCIL_NONE )
    {
        dsState.stencilEnable = true;
        fnUnpackStencilFront( stencilState, dsState.frontFaceStencil );

        bool backFaceSet = false;

        if ( ( stencilState & VRHI_STENCIL_BACK_MASK ) != 0 )
        {
            fnUnpackStencilBack( stencilState, dsState.backFaceStencil );
            backFaceSet = true;
        }

        if ( !backFaceSet )
        {
            dsState.backFaceStencil = dsState.frontFaceStencil;
        }

        dsState.stencilRefValue = ( uint8_t ) ( ( stencilState & VRHI_STENCIL_FUNC_REF_MASK ) >> VRHI_STENCIL_FUNC_REF_SHIFT );
        dsState.stencilReadMask = ( uint8_t ) ( ( stencilState & VRHI_STENCIL_FUNC_RMASK_MASK ) >> VRHI_STENCIL_FUNC_RMASK_SHIFT );
        
        uint8_t wmask = ( uint8_t ) ( ( stencilState & VRHI_STENCIL_FUNC_WMASK_MASK ) >> VRHI_STENCIL_FUNC_WMASK_SHIFT );
        if ( wmask != 0 || ( stencilState & VRHI_STENCIL_FUNC_WMASK_MASK ) != 0 )
        {
            dsState.stencilWriteMask = wmask;
        }
        else
        {
            dsState.stencilWriteMask = dsState.stencilReadMask;
        }

        dsState.dynamicStencilRef = false;
    }

    return dsState;
}

nvrhi::RasterState vhTranslateRasterState( uint64_t stateFlags )
{
    nvrhi::RasterState rasterState;
    memset( &rasterState, 0, sizeof( rasterState ) ); // zero padding: hashed by raw bytes

    // Extract cull mode
    uint32_t cullMode = ( uint32_t ) ( ( stateFlags & VRHI_STATE_CULL_MASK ) >> VRHI_STATE_CULL_SHIFT );
    
    // Map to NVRHI: 0 = None, 1 = Back, 2 = Front
    rasterState.cullMode = (cullMode == 1) ? nvrhi::RasterCullMode::Back :
                           (cullMode == 2) ? nvrhi::RasterCullMode::Front :
                           nvrhi::RasterCullMode::None;

    // Front face winding (default CCW = front, override to CW if flag set)
    rasterState.frontCounterClockwise = !( stateFlags & VRHI_STATE_FRONT_CW );
    
    rasterState.multisampleEnable = ( stateFlags & VRHI_STATE_MSAA ) != 0;
    rasterState.antialiasedLineEnable = ( stateFlags & VRHI_STATE_LINEAA ) != 0;
    rasterState.conservativeRasterEnable = ( stateFlags & VRHI_STATE_CONSERVATIVE_RASTER ) != 0;
    rasterState.depthClipEnable = ( stateFlags & VRHI_STATE_DEPTH_CLIP ) != 0;

    return rasterState;
}

nvrhi::VertexAttributeDesc vhTranslateVertexAttribute( const vhVertexLayoutDef& def, uint32_t bufferIndex )
{
    static const char* const s_attrNames[] =
    {
        "ATTR0",  "ATTR1",  "ATTR2",  "ATTR3",  "ATTR4",  "ATTR5",  "ATTR6",  "ATTR7",
        "ATTR8",  "ATTR9",  "ATTR10", "ATTR11", "ATTR12", "ATTR13", "ATTR14", "ATTR15",
        "ATTR16", "ATTR17", "ATTR18", "ATTR19", "ATTR20", "ATTR21", "ATTR22", "ATTR23",
        "ATTR24", "ATTR25", "ATTR26", "ATTR27", "ATTR28", "ATTR29", "ATTR30", "ATTR31"
    };

    nvrhi::VertexAttributeDesc attr;
    if ( def.location >= 0 && def.location < ( int32_t ) ( sizeof( s_attrNames ) / sizeof( s_attrNames[0] ) ) )
        attr.name = s_attrNames[ def.location ];
    else
        attr.name = "ATTR" + std::to_string( def.location );
    attr.format = def.format;
    attr.arraySize = 1;
    attr.bufferIndex = bufferIndex;
    attr.offset = def.offset;
    attr.elementStride = 0; // Set by caller
    attr.isInstanced = def.isInstanced;
    return attr;
}

bool vhDebugLayoutDiffCheck( const nvrhi::BindingLayoutVector& layouts, const nvrhi::BindingSetVector& bindings )
{
    if ( layouts.size() != bindings.size() )
    {
        VRHI_ERR( "vhDebugLayoutDiffCheck: Number of binding sets provided (%llu) does not match the number of binding layouts in the pipeline (%llu)\n", ( uint64_t ) bindings.size(), ( uint64_t ) layouts.size() );
        return false;
    }

    bool anyErrors = false;

    for ( int i = 0; i < ( int ) layouts.size(); ++i )
    {
        if ( !bindings[i] )
        {
            VRHI_ERR( "vhDebugLayoutDiffCheck: Binding set in slot %d is NULL\n", i );
            anyErrors = true;
            continue;
        }

        const auto* actualSetDesc = bindings[i]->getDesc();
        if ( actualSetDesc )
        {
            for ( const auto& item : actualSetDesc->bindings )
            {
                if ( !item.resourceHandle )
                {
                    VRHI_ERR( "vhDebugLayoutDiffCheck: Binding set in slot %d has NULL resource at binding slot %u\n", i, item.slot );
                    anyErrors = true;
                }
            }
        }

        nvrhi::IBindingLayout* setLayout = bindings[i]->getLayout();
        nvrhi::IBindingLayout* expectedLayout = layouts[i];
        
        bool setIsBindless = ( bindings[i]->getDesc() == nullptr );
        bool expectedBindless = ( expectedLayout->getBindlessDesc() != nullptr );

        if ( !expectedBindless && setLayout != expectedLayout )
        {
            VRHI_ERR( "vhDebugLayoutDiffCheck: Binding set in slot %d does not match the layout in pipeline slot %d. setLayout %p != expectedLayout %p.\n", i, i, setLayout, expectedLayout );

            const auto* setDesc = setLayout->getDesc();
            const auto* expDesc = expectedLayout->getDesc();

            if ( !setDesc || !expDesc )
            {
                VRHI_ERR( "    Invalid descriptors. SetDesc: %p, ExpectedDesc: %p (Is one bindless? Set: %d, Exp: %d)\n", setDesc, expDesc, setIsBindless, expectedBindless );
            }
            else
            {
                if ( setDesc->visibility != expDesc->visibility )
                    VRHI_ERR( "    Visibility mismatch. Set: %d, Expected: %d\n", ( int ) setDesc->visibility, ( int ) expDesc->visibility );

                std::map< uint32_t, const nvrhi::BindingLayoutItem* > setBindings;
                for ( const auto& b : setDesc->bindings ) setBindings[b.slot] = &b;

                std::map< uint32_t, const nvrhi::BindingLayoutItem* > expBindings;
                for ( const auto& b : expDesc->bindings ) expBindings[b.slot] = &b;

                for ( const auto& pair : expBindings )
                {
                    uint32_t slot = pair.first;
                    const auto* expItem = pair.second;
                    auto it = setBindings.find( slot );
                    if ( it == setBindings.end() )
                    {
                        VRHI_ERR( "    Missing binding at Slot %u. Expected Type: %d\n", slot, ( int ) expItem->type );
                    }
                    else
                    {
                        const auto* setItem = it->second;
                        if ( setItem->type != expItem->type )
                            VRHI_ERR( "    Type mismatch at Slot %u. Set: %d, Expected: %d\n", slot, ( int ) setItem->type, ( int ) expItem->type );
                        if ( setItem->size != expItem->size )
                            VRHI_ERR( "    Size/Array mismatch at Slot %u. Set: %d, Expected: %d\n", slot, ( int ) setItem->size, ( int ) expItem->size );
                    }
                }

                for ( const auto& pair : setBindings )
                {
                    if ( expBindings.find( pair.first ) == expBindings.end() )
                    {
                        VRHI_ERR( "    Unexpected extra binding at Slot %u in Set. Type: %d\n", pair.first, ( int ) pair.second->type );
                    }
                }
            }

            anyErrors = true;
        }

        if ( expectedBindless && !setIsBindless )
        {
            VRHI_ERR( "vhDebugLayoutDiffCheck: Binding set in slot %d is regular while the layout expects a descriptor table\n", i );
            anyErrors = true;
        }
    }

    return !anyErrors;
}

void vhWriteStateToGlobalUniform( const vhState& state, vhGlobalUniform& out )
{
    memset( &out, 0, sizeof( vhGlobalUniform ) );

    // Viewport / Camera
    out.u_viewRect = state.viewRect;
    out.u_viewTexel = glm::vec4( 1.0f / state.viewRect.z, 1.0f / state.viewRect.w, state.viewRect.z, state.viewRect.w );
    out.u_view = state.viewMatrix;
    out.u_proj = state.projMatrix;
    
    // Derived Camera
    out.u_viewProj = out.u_proj * out.u_view;
    out.u_invView = glm::inverse( out.u_view );
    out.u_invProj = glm::inverse( out.u_proj );
    out.u_invViewProj = glm::inverse( out.u_viewProj );

    out.u_invViewProj = glm::inverse( out.u_viewProj );
}

void vhWriteStateToWorldUniform( const vhState& state, vhWorldUniform& out )
{
    memset( &out, 0, sizeof( vhWorldUniform ) );

    if ( !state.worldMatrix.empty() )
    {
        // u_world receives world[0]...world[3]
        for ( size_t i = 0; i < 4 && i < state.worldMatrix.size(); ++i )
        {
            out.u_world[i] = state.worldMatrix[i];
        }

        // u_worldView derived from world[0]
        out.u_worldView = state.viewMatrix * state.worldMatrix[0];
        out.u_worldViewProj = state.projMatrix * out.u_worldView;
    }
    else
    {
        // Identity fallback
        out.u_world[0] = glm::mat4( 1.0f );
        out.u_worldView = state.viewMatrix;
        out.u_worldViewProj = state.projMatrix * out.u_worldView; // viewProj
    }
}

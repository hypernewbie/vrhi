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

#pragma once

#ifndef VRHI_IMPLEMENTATION
#include "vrhi_impl.h"
#endif // VRHI_IMPLEMENTATION

// ------------ State Implementation ------------

// Query state from backend (fast-path direct access)
bool vhGetState( vhStateId id, vhState& outState )
{
    return vhBackendQueryState( id, outState );
}

void vhCmdSetStateViewRect( vhStateId id, glm::vec4 rect )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateViewRect( id, rect ) );
}

void vhCmdSetStateViewScissor( vhStateId id, glm::vec4 scissor )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateViewScissor( id, scissor ) );
}

void vhCmdSetStateViewClear( vhStateId id, uint16_t flags, uint32_t rgba, float depth, uint8_t stencil )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateViewClear( id, flags, rgba, depth, stencil ) );
}

void vhCmdSetStateProgram( vhStateId id, vhProgram program )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateProgram( id, program ) );
}

void vhCmdSetStateViewTransform( vhStateId id, glm::mat4 view, glm::mat4 proj )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateViewTransform( id, view, proj ) );
}

void vhCmdSetStateWorldTransform( vhStateId id, std::vector< glm::mat4 > matrices )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateWorldTransform( id, matrices ) );
}

void vhCmdSetStateFlags( vhStateId id, uint64_t flags )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateFlags( id, flags ) );
}

void vhCmdSetStateDebugFlags( vhStateId id, uint64_t flags )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateDebugFlags( id, flags ) );
}

void vhCmdSetStateStencil( vhStateId id, uint32_t front, uint32_t back )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateStencil( id, front, back ) );
}

void vhCmdSetStateVertexBuffer( vhStateId id, uint8_t stream, vhBuffer buffer, uint64_t offset, uint32_t start, uint32_t num )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateVertexBuffer( id, stream, buffer, offset, start, num ) );
}

void vhCmdSetStateIndexBuffer( vhStateId id, vhBuffer buffer, uint64_t offset, uint32_t first, uint32_t num )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateIndexBuffer( id, buffer, offset, first, num ) );
}

void vhCmdSetStateTextures( vhStateId id, const std::vector< vhState::TextureBinding >& textures )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateTextures( id, textures ) );
}

void vhCmdSetStateSamplers( vhStateId id, const std::vector< vhState::SamplerDefinition >& samplers )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateSamplers( id, samplers ) );
}

void vhCmdSetStateBuffers( vhStateId id, const std::vector< vhState::BufferBinding >& buffers )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateBuffers( id, buffers ) );
}

void vhCmdSetStateConstants( vhStateId id, const std::vector< vhState::ConstantBufferValue >& constants )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateConstants( id, constants ) );
}

void vhCmdSetStatePushConstants( vhStateId id, glm::vec4 data )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStatePushConstants( id, data ) );
}

void vhCmdSetStateUniforms( vhStateId id, const std::vector< vhState::UniformBufferValue >& uniforms )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateUniforms( id, uniforms ) );
}

void vhCmdSetStateAttachments( vhStateId id, const std::vector< vhState::RenderTarget >& colors, vhState::RenderTarget depth )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateAttachments( id, colors, depth ) );
}

bool vhSetState( vhStateId id, vhState& state, uint64_t dirtyForceMask )
{
    uint64_t dirty = state.dirty | dirtyForceMask;
    if ( !dirty ) return true;

    if ( dirty & VRHI_DIRTY_VIEWPORT )
    {
        vhCmdSetStateViewRect( id, state.viewRect );
        vhCmdSetStateViewScissor( id, state.viewScissor );
        vhCmdSetStateViewClear( id, state.clearFlags, state.clearRgba, state.clearDepth, state.clearStencil );
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
        vhCmdSetStateStencil( id, state.frontStencil, state.backStencil );
    }

    if ( dirty & VRHI_DIRTY_VERTEX_INDEX )
    {
        for ( uint8_t i = 0; i < ( uint8_t ) state.vertexBindings.size(); ++i )
        {
            const auto& b = state.vertexBindings[i];
            vhCmdSetStateVertexBuffer( id, i, b.buffer, b.byteOffset, b.startVertex, b.numVertices );
        }
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

    state.dirty = 0x0ull;
    return true;
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
    blendState.targets[0].colorWriteMask = ( nvrhi::ColorMask )( stateFlags & 0xF );

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

    blendState.alphaToCoverageEnable = ( stateFlags & VRHI_STATE_BLEND_ALPHA_TO_COVERAGE ) != 0;

    return blendState;
}

nvrhi::DepthStencilState vhTranslateDepthStencilState( uint64_t stateFlags, uint32_t frontStencil, uint32_t backStencil )
{
    nvrhi::DepthStencilState dsState;

    auto fnConvertComparisonFunc = []( uint32_t func ) -> nvrhi::ComparisonFunc
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

    uint32_t depthFunc = ( uint32_t ) ( ( stateFlags & VRHI_STATE_DEPTH_TEST_MASK ) >> VRHI_STATE_DEPTH_TEST_SHIFT );
    if ( depthFunc != 0 )
    {
        dsState.depthTestEnable = true;
        dsState.depthFunc = fnConvertComparisonFunc( depthFunc );
    }
    else
    {
        dsState.depthTestEnable = false;
        dsState.depthFunc = nvrhi::ComparisonFunc::Less;
    }

    auto fnConvertStencilOp = []( uint32_t op ) -> nvrhi::StencilOp
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

    auto fnUnpackStencil = [&]( uint32_t packed, nvrhi::DepthStencilState::StencilOpDesc& desc )
    {
        uint32_t failOp = ( packed & VRHI_STENCIL_OP_FAIL_S_MASK ) >> VRHI_STENCIL_OP_FAIL_S_SHIFT;
        uint32_t depthFailOp = ( packed & VRHI_STENCIL_OP_FAIL_Z_MASK ) >> VRHI_STENCIL_OP_FAIL_Z_SHIFT;
        uint32_t passOp = ( packed & VRHI_STENCIL_OP_PASS_Z_MASK ) >> VRHI_STENCIL_OP_PASS_Z_SHIFT;
        uint32_t func = ( packed & VRHI_STENCIL_TEST_MASK ) >> VRHI_STENCIL_TEST_SHIFT;
        
        desc.failOp = fnConvertStencilOp( failOp );
        desc.depthFailOp = fnConvertStencilOp( depthFailOp );
        desc.passOp = fnConvertStencilOp( passOp );
        desc.stencilFunc = fnConvertComparisonFunc( func );
    };

    if ( backStencil != VRHI_STENCIL_NONE )
    {
        dsState.stencilEnable = true;
        fnUnpackStencil( frontStencil, dsState.frontFaceStencil );
        fnUnpackStencil( backStencil, dsState.backFaceStencil );
        
        dsState.stencilRefValue = ( uint8_t ) ( ( frontStencil & VRHI_STENCIL_FUNC_REF_MASK ) >> VRHI_STENCIL_FUNC_REF_SHIFT );
        dsState.stencilReadMask = ( uint8_t ) ( ( frontStencil & VRHI_STENCIL_FUNC_RMASK_MASK ) >> VRHI_STENCIL_FUNC_RMASK_SHIFT );
        dsState.stencilWriteMask = dsState.stencilReadMask;
    }
    else if ( frontStencil != VRHI_STENCIL_NONE )
    {
        dsState.stencilEnable = true;
        fnUnpackStencil( frontStencil, dsState.frontFaceStencil );
        dsState.backFaceStencil = dsState.frontFaceStencil;
        
        dsState.stencilRefValue = ( uint8_t ) ( ( frontStencil & VRHI_STENCIL_FUNC_REF_MASK ) >> VRHI_STENCIL_FUNC_REF_SHIFT );
        dsState.stencilReadMask = ( uint8_t ) ( ( frontStencil & VRHI_STENCIL_FUNC_RMASK_MASK ) >> VRHI_STENCIL_FUNC_RMASK_SHIFT );
        dsState.stencilWriteMask = dsState.stencilReadMask;
    }

    return dsState;
}

nvrhi::RasterState vhTranslateRasterState( uint64_t stateFlags )
{
    nvrhi::RasterState rasterState;

    uint32_t cullMode = ( uint32_t ) ( ( stateFlags & VRHI_STATE_CULL_MASK ) >> VRHI_STATE_CULL_SHIFT );
    if ( cullMode == ( VRHI_STATE_CULL_CW >> VRHI_STATE_CULL_SHIFT ) )
    {
        rasterState.cullMode = nvrhi::RasterCullMode::Back;
    }
    else if ( cullMode == ( VRHI_STATE_CULL_CCW >> VRHI_STATE_CULL_SHIFT ) )
    {
        rasterState.cullMode = nvrhi::RasterCullMode::Front;
    }
    else
    {
        rasterState.cullMode = nvrhi::RasterCullMode::None;
    }

    rasterState.frontCounterClockwise = ( stateFlags & VRHI_STATE_FRONT_CCW ) != 0;
    rasterState.multisampleEnable = ( stateFlags & VRHI_STATE_MSAA ) != 0;
    rasterState.antialiasedLineEnable = ( stateFlags & VRHI_STATE_LINEAA ) != 0;
    rasterState.conservativeRasterEnable = ( stateFlags & VRHI_STATE_CONSERVATIVE_RASTER ) != 0;

    return rasterState;
}
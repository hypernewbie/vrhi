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
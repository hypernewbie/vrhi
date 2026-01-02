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

void vhCmdSetStateViewFramebuffer( vhStateId id, vhFramebuffer fb )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateViewFramebuffer( id, fb ) );
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

void vhCmdSetStateStencil( vhStateId id, uint32_t front, uint32_t back )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateStencil( id, front, back ) );
}

void vhCmdSetStateVertexBuffer( vhStateId id, uint8_t stream, vhBuffer buffer, uint32_t start, uint32_t num )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateVertexBuffer( id, stream, buffer, start, num ) );
}

void vhCmdSetStateIndexBuffer( vhStateId id, vhBuffer buffer, uint32_t first, uint32_t num )
{
    vhCmdEnqueue( new VIDL_vhCmdSetStateIndexBuffer( id, buffer, first, num ) );
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

    if ( dirty & VRHI_DIRTY_FRAMEBUFFER )
    {
        vhCmdSetStateViewFramebuffer( id, state.viewFramebuffer );
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
        vhCmdSetStateStencil( id, state.frontStencil, state.backStencil );
    }

    if ( dirty & VRHI_DIRTY_BINDINGS )
    {
        for ( uint8_t i = 0; i < ( uint8_t ) state.vertexBindings.size(); ++i )
        {
            const auto& b = state.vertexBindings[i];
            vhCmdSetStateVertexBuffer( id, i, b.buffer, b.startVertex, b.numVertices );
        }
        vhCmdSetStateIndexBuffer( id, state.indexBinding.buffer, state.indexBinding.firstIndex, state.indexBinding.numIndices );
    }

    state.dirty = 0x0ull;
    return true;
}
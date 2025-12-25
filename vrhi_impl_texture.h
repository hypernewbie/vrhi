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

#include "vrhi_impl.h"

vhTexture vhAllocTexture()
{
    std::lock_guard< std::mutex > lock( g_vhTextureIDListMutex );
    uint32_t id = g_vhTextureIDList.alloc();
    g_vhTextureIDValid[id] = true;
    return id;
}

void vhDestroyTexture( vhTexture texture )
{
    std::lock_guard< std::mutex > lock( g_vhTextureIDListMutex );

    if ( g_vhTextureIDValid.find( texture ) == g_vhTextureIDValid.end() )
    {
        // Invalid texture handle
        return;
    }

    g_vhTextureIDValid.erase( texture );
    g_vhTextureIDList.release( texture );

    // Queue up command to destroy texture
    auto cmd = vhCmdAlloc<VIDL_vhDestroyTexture>( texture );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhCreateTexture(
    vhTexture texture,
    nvrhi::TextureDimension target,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag,
    const std::vector<uint8_t> *data
)
{
    auto cmd = vhCmdAlloc<VIDL_vhCreateTexture>( texture, target, dimensions, numMips, numLayers, format, flag, data );
    assert( cmd );
    vhCmdEnqueue( cmd );
}
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

// Set state on backend via IDL command
bool vhSetState( vhStateId id, const vhState& state )
{
    auto cmd = new VIDL_vhSetState( id, state );
    vhCmdEnqueue( cmd );
    return true; // Async operation, always returns success
}

// Set world matrix fast-path via IDL command
bool vhSetStateWorldMatrix( vhStateId id, int index, const glm::mat4& matrix )
{
    if ( index < 0 || index >= VRHI_MAX_WORLD_MATRICES )
    {
        VRHI_ERR( "vhSetStateWorldMatrix: Invalid index %d (max %d)\n", index, VRHI_MAX_WORLD_MATRICES );
        return false;
    }
    
    auto cmd = new VIDL_vhSetStateWorldMatrix( id, index, matrix );
    vhCmdEnqueue( cmd );
    return true;
}

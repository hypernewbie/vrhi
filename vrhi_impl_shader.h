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
#include "vrhi_utils.h"

// ------------ Shader Utilities ------------


// ------------ Shader Implementation ------------

vhShader vhAllocShader()
{
    std::lock_guard< std::mutex > lock( g_vhShaderIDListMutex );
    uint32_t id = g_vhShaderIDList.alloc();
    g_vhShaderIDValid[id] = true;
    return id;
}

void vhDestroyShader( vhShader shader )
{
    std::lock_guard< std::mutex > lock( g_vhShaderIDListMutex );
    if ( g_vhShaderIDValid.find( shader ) == g_vhShaderIDValid.end() ) return;

    g_vhShaderIDValid.erase( shader );
    g_vhShaderIDList.release( shader );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyShader>( shader );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

vhProgram vhAllocProgram()
{
    std::lock_guard< std::mutex > lock( g_vhProgramIDListMutex );
    uint32_t id = g_vhProgramIDList.alloc();
    g_vhProgramIDValid[id] = true;
    return id;
}

void vhDestroyProgram( vhProgram program )
{
    std::lock_guard< std::mutex > lock( g_vhProgramIDListMutex );
    if ( g_vhProgramIDValid.find( program ) == g_vhProgramIDValid.end() ) return;

    g_vhProgramIDValid.erase( program );
    g_vhProgramIDList.release( program );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyProgram>( program );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

vhPipeline vhAllocPipeline()
{
    std::lock_guard< std::mutex > lock( g_vhPipelineIDListMutex );
    uint32_t id = g_vhPipelineIDList.alloc();
    g_vhPipelineIDValid[id] = true;
    return id;
}

void vhDestroyPipeline( vhPipeline pipeline )
{
    std::lock_guard< std::mutex > lock( g_vhPipelineIDListMutex );
    if ( g_vhPipelineIDValid.find( pipeline ) == g_vhPipelineIDValid.end() ) return;

    g_vhPipelineIDValid.erase( pipeline );
    g_vhPipelineIDList.release( pipeline );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyPipeline>( pipeline );
    assert( cmd );
    vhCmdEnqueue( cmd );
}
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

#ifdef VRHI_SHADER_COMPILER_IMPLEMENTATION
std::string vhBuildShaderFlagArgs_Internal( uint64_t flags )
{
    std::string args = "";

    uint64_t sm = ( flags & VRHI_SHADER_SM_MASK );
    if ( sm == VRHI_SHADER_SM_5_0 ) args += " -m 5_0";
    else if ( sm == VRHI_SHADER_SM_6_0 ) args += " -m 6_0";
    else if ( sm == VRHI_SHADER_SM_6_6 ) args += " -m 6_6";
    else args += " -m 6_5"; // Default or 6.5

    if ( flags & VRHI_SHADER_DEBUG )
    {
        args += " -O 0 --embedPDB";
    }
    else
    {
        args += " -O 3";
    }

    if ( flags & VRHI_SHADER_ROW_MAJOR ) args += " --matrixRowMajor";
    if ( flags & VRHI_SHADER_WARNINGS_AS_ERRORS ) args += " --WX";
    if ( flags & VRHI_SHADER_STRIP_REFLECTION ) args += " --stripReflection";
    if ( flags & VRHI_SHADER_ALL_RESOURCES_BOUND ) args += " --allResourcesBound";

    return args;
}
#endif

// ------------ Shader Implementation ------------

vhShader vhAllocShader()
{
    std::lock_guard< std::mutex > lock( g_vhShaderIDListMutex );
    uint32_t id = g_vhShaderIDList.alloc();
    g_vhShaderIDValid[id] = true;
    return id;
}

#ifdef VRHI_SHADER_COMPILER
bool vhCompileShader(
    const char* source,
    uint64_t flags,
    std::vector< uint32_t >& outSpirv,
    const char* entry,
    const std::vector< std::string >& defines,
    const std::vector< std::string >& includes,
    std::string* outError
)
{
    // Unpack the flags. WARNING: This must sync with vrhi_defines.h!!!

    uint64_t stage = ( flags & VRHI_SHADER_STAGE_MASK );
    const char* stageStr = nullptr;
    switch ( stage )
    {
        case VRHI_SHADER_STAGE_VERTEX: stageStr = "vs"; break;
        case VRHI_SHADER_STAGE_PIXEL: stageStr = "ps"; break;
        case VRHI_SHADER_STAGE_COMPUTE: stageStr = "cs"; break;
        case VRHI_SHADER_STAGE_RAYGEN: stageStr = "lib"; break;
        case VRHI_SHADER_STAGE_MISS: stageStr = "lib"; break;
        case VRHI_SHADER_STAGE_CLOSEST_HIT: stageStr = "lib"; break;
        case VRHI_SHADER_STAGE_MESH: stageStr = "ms"; break;
        case VRHI_SHADER_STAGE_AMPLIFICATION: stageStr = "as"; break;
        default: stageStr = "ps"; break;
    }

    uint64_t shaderModel = ( flags & VRHI_SHADER_SM_MASK ) >> 4;
    const char* smStr = "6_5"; // Default
    switch ( shaderModel )
    {
        case 1: smStr = "5_0"; break;
        case 2: smStr = "6_0"; break;
        case 3: smStr = "6_5"; break;
        case 4: smStr = "6_6"; break;
        default: smStr = "6_5"; break;
    }

    bool debug = ( flags & VRHI_SHADER_DEBUG ) != 0;
    bool rowMajor = ( flags & VRHI_SHADER_ROW_MAJOR ) != 0;
    bool warningsAsErrors = ( flags & VRHI_SHADER_WARNINGS_AS_ERRORS ) != 0;

    bool compatHlsl = ( flags & VRHI_SHADER_COMPAT_HLSL ) != 0;
    bool hlsl2021 = ( flags & VRHI_SHADER_HLSL_2021 ) != 0;
    bool stripReflection = ( flags & VRHI_SHADER_STRIP_REFLECTION ) != 0;
    bool allResourcesBound = ( flags & VRHI_SHADER_ALL_RESOURCES_BOUND ) != 0;

    std::filesystem::path sourcePath = source;
    if ( !std::filesystem::exists( sourcePath ) )
    {
        if ( outError ) *outError = "Shader source file does not exist: " + sourcePath.string();
        return false;
    }

    return true;
}
#endif // VRHI_SHADER_COMPILER

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
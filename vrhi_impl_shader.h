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
#include <komihash/komihash.h>

// ------------ Shader Utilities ------------

// Internal helper to populate GraphicsPipelineDesc from VRHI state flags.
void vhPartialFillGraphicsPipelineDescFromState_Internal( uint64_t state, nvrhi::GraphicsPipelineDesc& desc )
{
    auto fnConvertBlendFactor = []( uint32_t factor ) -> nvrhi::BlendFactor {
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

    auto fnConvertBlendOp = []( uint32_t op ) -> nvrhi::BlendOp {
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

    auto fnConvertComparisonFunc = []( uint32_t func ) -> nvrhi::ComparisonFunc {
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

    // Write Masks
    desc.renderState.blendState.targets[0].colorWriteMask = ( nvrhi::ColorMask )( state & 0xF );
    desc.renderState.depthStencilState.depthWriteEnable = ( state & VRHI_STATE_WRITE_Z ) != 0;

    // Depth Test
    uint32_t depthFunc = ( uint32_t ) ( ( state & VRHI_STATE_DEPTH_TEST_MASK ) >> VRHI_STATE_DEPTH_TEST_SHIFT );
    if ( depthFunc != 0 )
    {
        desc.renderState.depthStencilState.depthTestEnable = true;
        desc.renderState.depthStencilState.depthFunc = fnConvertComparisonFunc( depthFunc );
    }
    else
    {
        desc.renderState.depthStencilState.depthTestEnable = false;
        desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    }

    // Blend Factors
    uint32_t blendState = ( uint32_t ) ( ( state & VRHI_STATE_BLEND_MASK ) >> VRHI_STATE_BLEND_SHIFT );
    if ( blendState != 0 )
    {
        desc.renderState.blendState.targets[0].blendEnable = true;
        desc.renderState.blendState.targets[0].srcBlend = fnConvertBlendFactor( blendState & 0xF );
        desc.renderState.blendState.targets[0].destBlend = fnConvertBlendFactor( ( blendState >> 4 ) & 0xF );
        desc.renderState.blendState.targets[0].srcBlendAlpha = fnConvertBlendFactor( ( blendState >> 8 ) & 0xF );
        desc.renderState.blendState.targets[0].destBlendAlpha = fnConvertBlendFactor( ( blendState >> 12 ) & 0xF );
    }

    // Blend Equation
    uint32_t blendEq = ( uint32_t ) ( ( state & VRHI_STATE_BLEND_EQUATION_MASK ) >> VRHI_STATE_BLEND_EQUATION_SHIFT );
    if ( blendEq != 0 )
    {
        desc.renderState.blendState.targets[0].blendOp = fnConvertBlendOp( blendEq & 0x7 );
        desc.renderState.blendState.targets[0].blendOpAlpha = fnConvertBlendOp( ( blendEq >> 3 ) & 0x7 );
    }

    // Rasterization State
    uint32_t cullMode = ( uint32_t ) ( ( state & VRHI_STATE_CULL_MASK ) >> VRHI_STATE_CULL_SHIFT );
    if ( cullMode == ( VRHI_STATE_CULL_CW >> VRHI_STATE_CULL_SHIFT ) )
    {
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
    }
    else if ( cullMode == ( VRHI_STATE_CULL_CCW >> VRHI_STATE_CULL_SHIFT ) )
    {
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Front;
    }
    else
    {
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    }

    desc.renderState.rasterState.frontCounterClockwise = ( state & VRHI_STATE_FRONT_CCW ) != 0;
    desc.renderState.rasterState.multisampleEnable = ( state & VRHI_STATE_MSAA ) != 0;
    desc.renderState.rasterState.antialiasedLineEnable = ( state & VRHI_STATE_LINEAA ) != 0;
    desc.renderState.rasterState.conservativeRasterEnable = ( state & VRHI_STATE_CONSERVATIVE_RASTER ) != 0;
    desc.renderState.blendState.alphaToCoverageEnable = ( state & VRHI_STATE_BLEND_ALPHA_TO_COVERAGE ) != 0;

    // Primitive Type
    uint32_t pt = ( uint32_t ) ( ( state & VRHI_STATE_PT_MASK ) >> VRHI_STATE_PT_SHIFT );
    switch ( pt )
    {
        case 0: desc.primType = nvrhi::PrimitiveType::TriangleList; break; // TRIANGLES
        case 1: desc.primType = nvrhi::PrimitiveType::TriangleStrip; break;
        case 2: desc.primType = nvrhi::PrimitiveType::LineList; break;
        case 3: desc.primType = nvrhi::PrimitiveType::LineStrip; break;
        case 4: desc.primType = nvrhi::PrimitiveType::PointList; break;
    }
}

#ifdef VRHI_SHADER_COMPILER

static bool vhLoadSpirvFile( const std::filesystem::path& path, std::vector< uint32_t >& outSpirv )
{
    std::ifstream file( path, std::ios::binary | std::ios::ate );
    if ( !file.is_open( ) )
    {
        return false;
    }

    std::streamsize size = file.tellg( );
    file.seekg( 0, std::ios::beg );

    outSpirv.resize( ( size + 3 ) / 4 );

    // Ensure we don't read past end if file size is not multiple of 4.
    std::memset( outSpirv.data( ), 0, outSpirv.size( ) * 4 );

    if ( !file.read( ( char* ) outSpirv.data( ), size ) )
    {
        return false;
    }

    return true;
}

#ifdef _WIN32
    #include <stdio.h> 
    
    // Map to Windows underscore variants
    #define VH_POPEN       _popen
    #define VH_PCLOSE      _pclose
    #define VH_PUTENV      _putenv
    
    // Windows pclose returns the exit code directly
    #define VH_WEXITSTATUS(x) (x)
    #define VH_WIFEXITED(x)   ((x) != -1)
#else
    #include <unistd.h>
    #include <sys/wait.h>
    
    // Map to standard POSIX names
    #define VH_POPEN       popen
    #define VH_PCLOSE      pclose
    #define VH_PUTENV      putenv
    
    // Map to standard POSIX macros
    #define VH_WEXITSTATUS(x) WEXITSTATUS(x)
    #define VH_WIFEXITED(x)   WIFEXITED(x)
#endif

bool vhRunExe( const std::string& command, std::string& outOutput )
{
#ifdef _WIN32
    // Wrap in quotes to prevent cmd.exe from stripping the executable's quotes
    std::string fullCommand = "\"" + command + "\" 2>&1";
#else
    std::string fullCommand = command + " 2>&1";
#endif

    // Use the prefixed macro
    FILE* pipe = VH_POPEN( fullCommand.c_str( ), "r" ); 
    if ( !pipe )
    {
        return false;
    }

    char buffer[2048];
    while ( fgets( buffer, sizeof( buffer ), pipe ) )
    {
        outOutput += buffer;
    }

    // Use the prefixed close
    int result = VH_PCLOSE( pipe ); 

    // Use the prefixed status macros
    bool failed = false;
    if ( result == -1 || !VH_WIFEXITED( result ) || VH_WEXITSTATUS( result ) != 0 )
    {
        failed = true;
    }

    return !failed;
}

const char* vhGetShaderProfile( uint64_t flags )
{
    uint64_t stage = ( flags & VRHI_SHADER_STAGE_MASK );
    switch ( stage )
    {
        case VRHI_SHADER_STAGE_VERTEX:        return "vs";
        case VRHI_SHADER_STAGE_PIXEL:         return "ps";
        case VRHI_SHADER_STAGE_COMPUTE:       return "cs";
        case VRHI_SHADER_STAGE_RAYGEN:
        case VRHI_SHADER_STAGE_MISS:
        case VRHI_SHADER_STAGE_CLOSEST_HIT:   return "lib";
        case VRHI_SHADER_STAGE_MESH:          return "ms";
        case VRHI_SHADER_STAGE_AMPLIFICATION: return "as";
    }
    return "ps";
}

std::string vhBuildShaderFlagArgs_Internal( uint64_t flags )
{
    std::string args = "";

    // Shader Model
    uint64_t sm = ( flags & VRHI_SHADER_SM_MASK );
    const char* smStr = "6_5";
    if ( sm == VRHI_SHADER_SM_5_0 ) smStr = "5_0";
    else if ( sm == VRHI_SHADER_SM_6_0 ) smStr = "6_0";
    else if ( sm == VRHI_SHADER_SM_6_6 ) smStr = "6_6";

    // ShaderMake uses -m for model
    args += " -m ";
    args += smStr;

    // Optimization
    if ( flags & VRHI_SHADER_DEBUG )
    {
        args += " -O 0 --embedPDB";
    }
    else
    {
        args += " -O 3";
    }

    // Toggles
    if ( flags & VRHI_SHADER_ROW_MAJOR ) args += " --matrixRowMajor";
    if ( flags & VRHI_SHADER_WARNINGS_AS_ERRORS ) args += " --WX";
    if ( flags & VRHI_SHADER_STRIP_REFLECTION ) args += " --stripReflection";
    if ( flags & VRHI_SHADER_ALL_RESOURCES_BOUND ) args += " --allResourcesBound";

    return args;
}
#endif

// ------------ Shader Implementation ------------

vhShader vhAllocShader( )
{
    std::lock_guard< std::mutex > lock( g_vhShaderIDListMutex );
    uint32_t id = g_vhShaderIDList.alloc( );
    g_vhShaderIDValid[id] = true;
    return id;
}

#ifdef VRHI_SHADER_COMPILER
bool vhCompileShader(
    const char* name,
    const char* source,
    uint64_t flags,
    std::vector< uint32_t >& outSpirv,
    const char* entry,
    const std::vector< std::string >& defines,
    const std::vector< std::string >& includes,
    std::string* outError
)
{
    std::filesystem::path tempDir = g_vhInit.shaderCompileTempDir;
    if ( !std::filesystem::exists( tempDir ) )
    {
        std::filesystem::create_directories( tempDir );
    }

    // Hash input into cache key

    std::string hashInput = std::string( name ) + source + std::to_string( flags ) + entry;
    for ( const auto& d : defines ) hashInput += d;
    for ( const auto& i : includes ) hashInput += i;
    uint64_t hash = komihash( hashInput.data( ), hashInput.size( ), 0 );

    std::string prefix = std::string( name ) + "_" + std::to_string( hash );
    const char* profile = vhGetShaderProfile( flags );
    
    // ShaderMake usually appends profile to output, e.g. Name_hash_ps.spirv

    std::string outputFilename = prefix + ".spirv";
    std::filesystem::path spvPath = tempDir / outputFilename;
    if ( !g_vhInit.forceShaderRecompile && std::filesystem::exists( spvPath ) )
    {
        if ( vhLoadSpirvFile( spvPath, outSpirv ) )
        {
            return true;
        }
    }

    // Argument Construction
    std::string argString = vhBuildShaderFlagArgs_Internal( flags );

    // Write source to temporary file
    std::string sourceFilename = prefix + ".slang";
    std::filesystem::path sourceFilePath = tempDir / sourceFilename;
    {
        std::ofstream sourceFile( sourceFilePath );
        if ( !sourceFile.is_open( ) )
        {
            if ( outError ) *outError = "Failed to create temporary shader source file: " + sourceFilePath.string( );
            return false;
        }
        sourceFile << source;
    }
    
    // Write config file for ShaderMake
    std::string configFilename = prefix + ".cfg";
    std::filesystem::path configFilePath = tempDir / configFilename;
    {
        std::ofstream configFile( configFilePath );
        if ( !configFile.is_open( ) )
        {
             if ( outError ) *outError = "Failed to create temporary shader config file: " + configFilePath.string( );
             return false;
        }
        // Config format: filename -T profile -E entry
        configFile << sourceFilename << " -T " << profile << " -E " << entry;
    }

    // Build

    std::filesystem::path shaderMakePath = g_vhInit.shaderMakePath;
    std::filesystem::path slangPath = g_vhInit.shaderMakeSlangPath;
    
#ifdef _WIN32
    std::filesystem::path shaderMakeExe = shaderMakePath / "ShaderMake.exe";
    std::filesystem::path slangExe = slangPath / "slangc.exe";
#else
    std::filesystem::path shaderMakeExe = shaderMakePath / "ShaderMake";
    std::filesystem::path slangExe = slangPath / "slangc";
#endif
    shaderMakeExe.make_preferred( );
    slangExe.make_preferred( );

    std::string cmd = "\"" + shaderMakeExe.string( ) + "\"";
    cmd += " -p SPIRV --binary --flatten --serial"; // --serial to avoid overhead/issues in single file compile
    cmd += " -c \"" + configFilePath.string( ) + "\"";
    cmd += " -o \"" + tempDir.string( ) + "\"";
    cmd += " --compiler \"" + slangExe.string( ) + "\"";
    cmd += " --slang";
    cmd += argString; // Includes -m, -O, etc.

    for ( const auto& d : defines ) cmd += " -D " + d;
    for ( const auto& i : includes ) cmd += " -I \"" + i + "\"";

    // Run Command

    std::string output;
    if ( !vhRunExe( cmd, output ) )
    {
        if ( outError ) *outError = output;
        return false;
    }

    // Load Result
    if ( !std::filesystem::exists( spvPath ) )
    {
        std::filesystem::path fallbackPath = tempDir / ( prefix + ".spirv" );
        if ( !std::filesystem::exists( fallbackPath ) )
        {
            if ( outError ) *outError = "Compilation finished but output file not found: " + spvPath.string( ) + "\nOutput:\n" + output;
            return false;
        }
        spvPath = fallbackPath;
    }

    return vhLoadSpirvFile( spvPath, outSpirv );
}
#endif // VRHI_SHADER_COMPILER

void vhCreateShader(
    vhShader shader,
    const char* name,
    uint64_t flags,
    const std::vector< uint32_t >& spirv,
    const char* entry
)
{
    if ( shader == VRHI_INVALID_HANDLE ) return;

    auto cmd = vhCmdAlloc< VIDL_vhCreateShader >( shader, name, flags, spirv, entry );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhDestroyShader( vhShader shader )
{
    std::lock_guard< std::mutex > lock( g_vhShaderIDListMutex );
    if ( g_vhShaderIDValid.find( shader ) == g_vhShaderIDValid.end( ) ) return;

    g_vhShaderIDValid.erase( shader );
    g_vhShaderIDList.release( shader );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyShader>( shader );
    assert( cmd );
    vhCmdEnqueue( cmd );
}
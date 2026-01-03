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

// Handle SPIRV-Reflect.
#ifndef VRHI_SPIRV_REFLECT_ALREADY_LINKED
    #if defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 4244) // conversion loss of data
        #pragma warning(disable: 4267) // size_t to int
        #pragma warning(disable: 4013) // undefined function assumed extern (C issue)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #endif

    #include <spirv_reflect.c>

    #if defined(_MSC_VER)
        #pragma warning(pop)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma GCC diagnostic pop
    #endif
#else
    #include <spirv_reflect.h>
#endif

// ------------ Shader Utilities ------------

// SPIRV reflection helper.
bool vhReflectSpirv(
    const std::vector< uint32_t >& spirvBlob,
    nvrhi::BindingLayoutDesc& outDesc,
    std::vector< vhShaderReflectionResource >& outResources,
    glm::uvec3& outGroupSize,
    std::vector< vhPushConstantRange >& outPushConstants
)
{
    auto fnGetResourceTypeFromReflect = []( const SpvReflectDescriptorBinding& binding ) -> nvrhi::ResourceType
    {
        switch ( binding.descriptor_type )
        {
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return nvrhi::ResourceType::ConstantBuffer;

        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return nvrhi::ResourceType::Texture_SRV;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return nvrhi::ResourceType::Texture_UAV;

        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
            return nvrhi::ResourceType::Sampler;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        {
            // Check if read-only
            bool isReadOnly = false;
            if ( binding.type_description && ( binding.type_description->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE ) )
            {
                isReadOnly = true;
            }
            return isReadOnly ? nvrhi::ResourceType::StructuredBuffer_SRV : nvrhi::ResourceType::StructuredBuffer_UAV;
        }

        default:
            return nvrhi::ResourceType::None;
        }
    };

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule( spirvBlob.size() * sizeof(uint32_t), spirvBlob.data(), &module );
    if ( result != SPV_REFLECT_RESULT_SUCCESS )
    {
        VRHI_ERR( "vhReflectSpirv: Failed to create shader module reflection" );
        return false;
    }

    // Thread Group Size (Compute only, but harmless to query for others if not present)
    if ( module.entry_point_count > 0 )
    {
        auto& ep = module.entry_points[0];
        outGroupSize.x = ep.local_size.x;
        outGroupSize.y = ep.local_size.y;
        outGroupSize.z = ep.local_size.z;
    }

    // Push Constants
    if ( module.push_constant_block_count > 0 )
    {
            for ( uint32_t i = 0; i < module.push_constant_block_count; ++i )
            {
                auto& pc = module.push_constant_blocks[i];
                outPushConstants.push_back( { pc.offset, pc.size, pc.name ? pc.name : "" } );
            }
    }
    
    // Descriptor Sets
    uint32_t count = 0;
    spvReflectEnumerateDescriptorSets( &module, &count, nullptr );
    std::vector< SpvReflectDescriptorSet* > sets( count );
    spvReflectEnumerateDescriptorSets( &module, &count, sets.data() );

    for ( auto* set : sets )
    {
        for ( uint32_t i = 0; i < set->binding_count; ++i )
        {
            auto* binding = set->bindings[i];
            nvrhi::ResourceType type = fnGetResourceTypeFromReflect( *binding );
            
            if ( type == nvrhi::ResourceType::None ) continue;

            nvrhi::BindingLayoutItem item{};
            item.slot = binding->binding;
            item.type = type;
            
            outDesc.addItem( item );

            vhShaderReflectionResource res;
            res.name = binding->name ? binding->name : "";
            res.slot = binding->binding;
            res.set = binding->set;
            res.type = type;
            res.arraySize = binding->count;
            res.sizeInBytes = binding->block.size; 
            
            outResources.push_back( res );
        }
    }

    spvReflectDestroyShaderModule( &module );
    return true;
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

    // Optimisation
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

void vhGetShaderInfo(
    vhShader shader,
    glm::uvec3* outGroupSize,
    std::vector< vhShaderReflectionResource >* outResources,
    std::vector< vhPushConstantRange >* outPushConstants,
    std::vector< vhSpecConstant >* outSpecConstants
)
{
    vhBackendQueryShaderInfo( shader, outGroupSize, outResources, outPushConstants, outSpecConstants );
}

void* vhGetShaderNvrhiHandle( vhShader shader )
{
    return vhBackendQueryShaderHandle( shader );
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
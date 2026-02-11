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
#include <komihash/komihash.h>
#include <spirv_reflect.h>
#include <slang.h>
#include <slang-com-ptr.h>

// ------------ Shader Utilities ------------

uint32_t vhPatchSpirvDescriptorSet0( std::vector< uint32_t >& spirv, uint32_t targetSet )
{
    constexpr uint32_t spirvHeaderWords = 5;
    constexpr uint32_t spvOpDecorate = 71;
    constexpr uint32_t spvDecorationDescriptorSet = 34;

    if ( spirv.size() < spirvHeaderWords )
    {
        return 0;
    }

    uint32_t patchedCount = 0;
    size_t i = spirvHeaderWords;

    while ( i < spirv.size() )
    {
        uint32_t firstWord = spirv[i];
        uint32_t opcode = firstWord & 0xFFFF;
        uint32_t wordCount = firstWord >> 16;

        if ( wordCount == 0 || i + wordCount > spirv.size() )
        {
            break;
        }

        if ( opcode == spvOpDecorate && wordCount >= 4 )
        {
            uint32_t decoration = spirv[i + 2];

            if ( decoration == spvDecorationDescriptorSet && spirv[i + 3] == 0 )
            {
                spirv[i + 3] = targetSet;
                patchedCount++;
            }
        }

        i += wordCount;
    }

    return patchedCount;
}

// SPIRV reflection helper.
bool vhReflectSpirv(
    const std::vector< uint32_t >& spirvBlob,
    nvrhi::BindingLayoutDesc& outDesc,
    std::vector< vhShaderReflectionResource >& outResources,
    glm::uvec3& outGroupSize,
    std::vector< vhPushConstantRange >& outPushConstants,
    std::vector< vhVertexLayoutDef >* outInputLayout
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
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                return nvrhi::ResourceType::TypedBuffer_SRV;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                return nvrhi::ResourceType::TypedBuffer_UAV;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            {
                // isSRV from reflection is very unreliable. Instead we match binding against register range shifts.
                // uRegShift is guaranteed to have highest value, so we can just test against that.
                // isStructured is impossible from reflection, thus we use a hack to read member_count.
                static const uint64_t s_structTypedFlags = ( SPV_REFLECT_TYPE_FLAG_BOOL | SPV_REFLECT_TYPE_FLAG_FLOAT | SPV_REFLECT_TYPE_FLAG_VECTOR | SPV_REFLECT_TYPE_FLAG_MATRIX );
                bool isSRV = ( binding.binding < g_vhInit.shaderMake_uRegShift );
                bool isRaw = ( std::string( binding.name ).find( "Raw" ) != std::string::npos );
                if ( isRaw )
                    return isSRV ? nvrhi::ResourceType::RawBuffer_SRV : nvrhi::ResourceType::RawBuffer_UAV;
                else
                    return isSRV ? nvrhi::ResourceType::StructuredBuffer_SRV : nvrhi::ResourceType::StructuredBuffer_UAV;
            }
            default:
                return nvrhi::ResourceType::None;
        }
    };

    auto fnMapSpvFormat = []( SpvReflectFormat fmt ) -> nvrhi::Format
    {
        switch ( fmt )
        {
            case SPV_REFLECT_FORMAT_R32_SFLOAT: return nvrhi::Format::R32_FLOAT;
            case SPV_REFLECT_FORMAT_R32G32_SFLOAT: return nvrhi::Format::RG32_FLOAT;
            case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT: return nvrhi::Format::RGB32_FLOAT;
            case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT: return nvrhi::Format::RGBA32_FLOAT;

            case SPV_REFLECT_FORMAT_R16_SFLOAT: return nvrhi::Format::R16_FLOAT;
            case SPV_REFLECT_FORMAT_R16G16_SFLOAT: return nvrhi::Format::RG16_FLOAT;
            case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT: return nvrhi::Format::UNKNOWN; // Unsupported by NVRHI
            case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT: return nvrhi::Format::RGBA16_FLOAT;

            case SPV_REFLECT_FORMAT_R32_SINT: return nvrhi::Format::R32_SINT;
            case SPV_REFLECT_FORMAT_R32G32_SINT: return nvrhi::Format::RG32_SINT;
            case SPV_REFLECT_FORMAT_R32G32B32_SINT: return nvrhi::Format::RGB32_SINT;
            case SPV_REFLECT_FORMAT_R32G32B32A32_SINT: return nvrhi::Format::RGBA32_SINT;

            case SPV_REFLECT_FORMAT_R32_UINT: return nvrhi::Format::R32_UINT;
            case SPV_REFLECT_FORMAT_R32G32_UINT: return nvrhi::Format::RG32_UINT;
            case SPV_REFLECT_FORMAT_R32G32B32_UINT: return nvrhi::Format::RGB32_UINT;
            case SPV_REFLECT_FORMAT_R32G32B32A32_UINT: return nvrhi::Format::RGBA32_UINT;

            // Short (16-bit Int)
            case SPV_REFLECT_FORMAT_R16_SINT: return nvrhi::Format::R16_SINT;
            case SPV_REFLECT_FORMAT_R16G16_SINT: return nvrhi::Format::RG16_SINT;
            case SPV_REFLECT_FORMAT_R16G16B16_SINT: return nvrhi::Format::UNKNOWN; // Unsupported by NVRHI
            case SPV_REFLECT_FORMAT_R16G16B16A16_SINT: return nvrhi::Format::RGBA16_SINT;

            case SPV_REFLECT_FORMAT_R16_UINT: return nvrhi::Format::R16_UINT;
            case SPV_REFLECT_FORMAT_R16G16_UINT: return nvrhi::Format::RG16_UINT;
            case SPV_REFLECT_FORMAT_R16G16B16_UINT: return nvrhi::Format::UNKNOWN; // Unsupported by NVRHI
            case SPV_REFLECT_FORMAT_R16G16B16A16_UINT: return nvrhi::Format::RGBA16_UINT;

            default: return nvrhi::Format::UNKNOWN;
        }
    };
    auto fnMapSpvImageFormat = []( SpvImageFormat fmt ) -> nvrhi::Format
    {
        if ( fmt == SpvImageFormatRgba32f ) return nvrhi::Format::RGBA32_FLOAT;
        if ( fmt == SpvImageFormatRgba16f ) return nvrhi::Format::RGBA16_FLOAT;
        if ( fmt == SpvImageFormatR32f )    return nvrhi::Format::R32_FLOAT;
        if ( fmt == SpvImageFormatRgba8 )   return nvrhi::Format::RGBA8_UNORM;
        if ( fmt == SpvImageFormatRgba8Snorm ) return nvrhi::Format::RGBA8_SNORM;

        if ( fmt == SpvImageFormatRg32f )   return nvrhi::Format::RG32_FLOAT;
        if ( fmt == SpvImageFormatRg16f )   return nvrhi::Format::RG16_FLOAT;

        if ( fmt == SpvImageFormatR32i )    return nvrhi::Format::R32_SINT;
        if ( fmt == SpvImageFormatR32ui )   return nvrhi::Format::R32_UINT;

        if ( fmt == SpvImageFormatRg32i )    return nvrhi::Format::RG32_SINT;
        if ( fmt == SpvImageFormatRg32ui )   return nvrhi::Format::RG32_UINT;

        if ( fmt == SpvImageFormatRgba32i )  return nvrhi::Format::RGBA32_SINT;
        if ( fmt == SpvImageFormatRgba32ui ) return nvrhi::Format::RGBA32_UINT;

        return nvrhi::Format::UNKNOWN;
    };

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule( spirvBlob.size() * sizeof( uint32_t ), spirvBlob.data(), &module );
    if ( result != SPV_REFLECT_RESULT_SUCCESS )
    {
        VRHI_ERR( "vhReflectSpirv: Failed to create shader module reflection\n" );
        return false;
    }

    // Thread Group Size
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
            outDesc.addItem( nvrhi::BindingLayoutItem::PushConstants( 0, ( size_t ) pc.size ) );
        }
    }

    // Descriptor Sets
    uint32_t count = 0;
    spvReflectEnumerateDescriptorSets( &module, &count, nullptr );
    std::vector< SpvReflectDescriptorSet* > sets( count );
    spvReflectEnumerateDescriptorSets( &module, &count, sets.data() );

    auto fnGetTextureDimension = []( const SpvReflectImageTraits& image ) -> nvrhi::TextureDimension
    {
        switch ( image.dim )
        {
            case SpvDim1D:
                return image.arrayed ? nvrhi::TextureDimension::Texture1DArray : nvrhi::TextureDimension::Texture1D;
            case SpvDim2D:
                if ( image.ms )
                    return image.arrayed ? nvrhi::TextureDimension::Texture2DMSArray : nvrhi::TextureDimension::Texture2DMS;
                else
                    return image.arrayed ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
            case SpvDim3D:
                return nvrhi::TextureDimension::Texture3D;
            case SpvDimCube:
                return image.arrayed ? nvrhi::TextureDimension::TextureCubeArray : nvrhi::TextureDimension::TextureCube;
            default:
                return nvrhi::TextureDimension::Unknown;
        }
    };

    for ( auto* set : sets )
    {
        for ( uint32_t i = 0; i < set->binding_count; ++i )
        {
            auto* binding = set->bindings[i];
            nvrhi::ResourceType type = fnGetResourceTypeFromReflect( *binding );

            if ( type == nvrhi::ResourceType::None ) continue;
            assert( binding->count > 0 );

            nvrhi::BindingLayoutItem item{};
            item.setSlot( binding->binding );
            item.setType( type );
            item.setSize( binding->count );

            outDesc.addItem( item );

            vhShaderReflectionResource res;
            res.name = binding->name ? binding->name : "";
            res.slot = binding->binding;
            res.set = binding->set;
            res.type = type;
            res.format = fnMapSpvImageFormat( binding->image.image_format );
            res.dim = fnGetTextureDimension( binding->image );
            res.arraySize = binding->count;
            res.sizeInBytes = binding->block.size;

            if ( res.type == nvrhi::ResourceType::ConstantBuffer && ( res.name == "$Globals" || res.name == "_Globals" || res.name == "globalParams" ) )
            {
                for ( uint32_t j = 0; j < binding->block.member_count; ++j )
                {
                    const auto& m = binding->block.members[j];
                    res.members.push_back( { m.name ? m.name : "", m.offset, m.size } );
                }
            }

            outResources.push_back( res );
        }
    }

    // Input Variables (Vertex Attributes)
    if ( outInputLayout )
    {
        uint32_t varCount = 0;
        spvReflectEnumerateInputVariables( &module, &varCount, nullptr );
        std::vector< SpvReflectInterfaceVariable* > inputVars( varCount );
        spvReflectEnumerateInputVariables( &module, &varCount, inputVars.data() );

        for ( auto* var : inputVars )
        {
            // Ignore built-ins (gl_VertexIndex etc)
            if ( var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN ) continue;

            vhVertexLayoutDef def;
            def.location = var->location;
            def.format = fnMapSpvFormat( var->format );
            def.offset = 0; // Not relevant for shader reflection input

            outInputLayout->push_back( def );
        }
    }

    spvReflectDestroyShaderModule( &module );
    return true;
}

static bool vhLoadSpirvFile( const std::filesystem::path& path, std::vector< uint32_t >& outSpirv )
{
    std::ifstream file( path, std::ios::binary | std::ios::ate );
    if ( !file.is_open() )
    {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg( 0, std::ios::beg );

    outSpirv.resize( ( size + 3 ) / 4 );

    // Ensure we don't read past end if file size is not multiple of 4.
    std::memset( outSpirv.data(), 0, outSpirv.size() * 4 );

    if ( !file.read( ( char* ) outSpirv.data(), size ) )
    {
        return false;
    }

    return true;
}

const char* vhGetShaderProfile( uint64_t flags )
{
    uint64_t stage = ( flags & VRHI_SHADER_STAGE_MASK );
    switch ( stage )
    {
        case VRHI_SHADER_STAGE_VERTEX:        return "vs";
        case VRHI_SHADER_STAGE_PIXEL:         return "ps";
        case VRHI_SHADER_STAGE_COMPUTE:       return "cs";
        case VRHI_SHADER_STAGE_HULL:          return "hs";
        case VRHI_SHADER_STAGE_DOMAIN:        return "ds";
        case VRHI_SHADER_STAGE_GEOMETRY:      return "gs";
        case VRHI_SHADER_STAGE_RAYGEN:
        case VRHI_SHADER_STAGE_MISS:
        case VRHI_SHADER_STAGE_CLOSEST_HIT:   return "lib";
        case VRHI_SHADER_STAGE_MESH:          return "ms";
        case VRHI_SHADER_STAGE_AMPLIFICATION: return "as";
    }
    return "ps";
}

// ------------ Shader Implementation ------------

vhShader vhAllocShader()
{
    std::lock_guard< std::mutex > lock( g_vhShaderIDListMutex );
    uint32_t id = g_vhShaderIDList.alloc();
    g_vhShaderIDValid[id] = true;
    return id;
}

bool vhCompileShaderSlang(
    const char* source,
    const std::filesystem::path& sourcePath,
    std::vector< uint32_t >& outSpirv,
    uint64_t flags,
    const char* entry,
    const std::vector< std::string >& defines,
    const std::vector< std::string >& includes,
    std::string* outError
)
{
    if ( outError ) *outError = "";
    outSpirv.clear();
    static Slang::ComPtr< slang::IGlobalSession > g_slang;
    if ( !g_slang ) slang::createGlobalSession( g_slang.writeRef() );
    if ( !g_slang )
    {
        if ( outError ) *outError = "Failed to create Slang global session";
        VRHI_ERR( "vhCompileShaderSlang: Failed to create Slang global session\n" );
        return false;
    }

    Slang::ComPtr< slang::ISession > session;
    slang::SessionDesc sessionDesc = {};
    sessionDesc.targetCount = 0;
    sessionDesc.targets = nullptr;
    
    std::vector< const char* > searchPaths;
    std::string sourceDir = sourcePath.parent_path().generic_string();
    searchPaths.push_back( sourceDir.c_str() );
    
    for ( const auto& inc : includes )
    {
        searchPaths.push_back( inc.c_str() );
    }
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = (SlangInt)searchPaths.size();

    if ( SLANG_FAILED( g_slang->createSession( sessionDesc, session.writeRef() ) ) )
    {
        if ( outError ) *outError = "Failed to create Slang session";
        return false;
    }
    Slang::ComPtr< slang::ICompileRequest > request;
    if ( SLANG_FAILED( session->createCompileRequest( request.writeRef() ) ) )
    {
        if ( outError ) *outError = "Failed to create Slang compile request";
        return false;
    }

    std::vector< const char* > args;
    args.push_back( "-target" );
    args.push_back( "spirv" );

    // Profile
    // Map VRHI_SHADER_SM_* to Slang profiles.
    // e.g., VRHI_SHADER_SM_6_5 + VERTEX -> vs_6_5
    const char* stageProfile = vhGetShaderProfile( flags ); // "vs", "ps", etc.
    uint64_t sm = ( flags & VRHI_SHADER_SM_MASK );
    std::string smStr = "6_5";
    if ( sm == VRHI_SHADER_SM_5_0 ) smStr = "5_0";
    else if ( sm == VRHI_SHADER_SM_6_0 ) smStr = "6_0";
    else if ( sm == VRHI_SHADER_SM_6_6 ) smStr = "6_6";
    std::string profileArg = std::string( stageProfile ) + "_" + smStr;
    
    // We need to keep the string alive until processCommandLineArguments is called.
    // Use std::list to ensure pointers remain valid after insertion.
    std::list< std::string > argStrings;
    auto fnSlangAddArg = [&]( const std::string& arg )
    {
        argStrings.push_back( arg );
        args.push_back( argStrings.back().c_str() );
    };

    // Override fnSlangAddArg to push directly to args for literals to avoid string copies/allocs
    auto fnSlangAddLit = [&]( const char* literal )
    {
        args.push_back( literal );
    };

    fnSlangAddLit( "-profile" );
    fnSlangAddArg( profileArg );

    // Entry Point
    fnSlangAddLit( "-entry" );
    fnSlangAddLit( entry );
    fnSlangAddLit( "-fvk-use-entrypoint-name" );

    // Optimization & Debug
    if ( flags & VRHI_SHADER_DEBUG )
    {
        fnSlangAddLit( "-O0" );
        fnSlangAddLit( "-g" );
    }
    else
    {
        fnSlangAddLit( "-O3" );
    }

    // Matrix Layout
    if ( flags & VRHI_SHADER_ROW_MAJOR )
        fnSlangAddLit( "-matrix-layout-row-major" );
    else
        fnSlangAddLit( "-matrix-layout-column-major" );
    if ( flags & VRHI_SHADER_WARNINGS_AS_ERRORS )
        fnSlangAddLit( "-warnings-as-errors" );

    // Defines
    for ( const auto& d : defines )
    {
        fnSlangAddLit( "-D" );
        fnSlangAddArg( d );
    }

    for ( uint32_t space = 0; space < VRHI_DESCRIPTOR_SET_MAX; ++space )
    {
        // s-shift
        fnSlangAddLit( "-fvk-s-shift" );
        fnSlangAddArg( std::to_string( g_vhInit.shaderMake_sRegShift ) );
        fnSlangAddArg( std::to_string( space ) );

        // t-shift
        fnSlangAddLit( "-fvk-t-shift" );
        fnSlangAddArg( std::to_string( g_vhInit.shaderMake_tRegShift ) );
        fnSlangAddArg( std::to_string( space ) );

        // b-shift
        fnSlangAddLit( "-fvk-b-shift" );
        fnSlangAddArg( std::to_string( g_vhInit.shaderMake_bRegShift ) );
        fnSlangAddArg( std::to_string( space ) );

        // u-shift
        fnSlangAddLit( "-fvk-u-shift" );
        fnSlangAddArg( std::to_string( g_vhInit.shaderMake_uRegShift ) );
        fnSlangAddArg( std::to_string( space ) );
    }

    // VRHI Stage Space Define
    uint32_t stageSpace = vhGetDescriptorSetForStage( flags );
    fnSlangAddLit( "-D" );
    fnSlangAddArg( "VRHI_STAGE_SPACE=space" + std::to_string( stageSpace ) );

    // Process arguments
    if ( SLANG_FAILED( request->processCommandLineArguments( args.data(), (int)args.size() ) ) )
    {
        if ( outError ) *outError = "Failed to process Slang command line arguments";
        return false;
    }
    int translationUnitIndex = request->addTranslationUnit( SLANG_SOURCE_LANGUAGE_SLANG, nullptr );
    request->addTranslationUnitSourceString( translationUnitIndex, "source", source );

    auto fnGetSlangStage = []( uint64_t flags ) -> SlangStage
    {
        uint64_t stage = ( flags & VRHI_SHADER_STAGE_MASK );
        switch ( stage )
        {
            case VRHI_SHADER_STAGE_VERTEX:        return SLANG_STAGE_VERTEX;
            case VRHI_SHADER_STAGE_PIXEL:         return SLANG_STAGE_FRAGMENT;
            case VRHI_SHADER_STAGE_COMPUTE:       return SLANG_STAGE_COMPUTE;
            case VRHI_SHADER_STAGE_HULL:          return SLANG_STAGE_HULL;
            case VRHI_SHADER_STAGE_DOMAIN:        return SLANG_STAGE_DOMAIN;
            case VRHI_SHADER_STAGE_GEOMETRY:      return SLANG_STAGE_GEOMETRY;
            case VRHI_SHADER_STAGE_RAYGEN:        return SLANG_STAGE_RAY_GENERATION;
            case VRHI_SHADER_STAGE_MISS:          return SLANG_STAGE_MISS;
            case VRHI_SHADER_STAGE_CLOSEST_HIT:   return SLANG_STAGE_CLOSEST_HIT;
            case VRHI_SHADER_STAGE_ANY_HIT:       return SLANG_STAGE_ANY_HIT;
            case VRHI_SHADER_STAGE_INTERSECTION:  return SLANG_STAGE_INTERSECTION;
            case VRHI_SHADER_STAGE_CALLABLE:      return SLANG_STAGE_CALLABLE;
            case VRHI_SHADER_STAGE_MESH:          return SLANG_STAGE_MESH;
            case VRHI_SHADER_STAGE_AMPLIFICATION: return SLANG_STAGE_AMPLIFICATION;
            default:                              return SLANG_STAGE_NONE;
        }
    };

    SlangStage stage = fnGetSlangStage( flags );
    int entryPointIndex = request->addEntryPoint( translationUnitIndex, entry, stage );
    int targetIndex = request->addCodeGenTarget( SLANG_SPIRV );

    // Compile
    bool compileSuccess = true;
    if ( SLANG_FAILED( request->compile() ) )
    {
        compileSuccess = false;
    }
    const char* diag = request->getDiagnosticOutput();
    if ( diag && diag[0] )
    {
        if ( outError ) *outError += diag;
        // VRHI_LOG( "%s\n", diag );
    }
    if ( !compileSuccess )
        return false;

    // Get output
    Slang::ComPtr< slang::IBlob > blob;
    if ( SLANG_FAILED( request->getEntryPointCodeBlob( entryPointIndex, targetIndex, blob.writeRef() ) ) )
    {
        if ( outError ) *outError = "Shader compilation failed (see output)";
        if ( diag && outError ) *outError += "\n" + std::string( diag );
        return false;
    }

    // Copy SPIRV blob to output vector
    size_t blobSize = blob->getBufferSize();
    size_t numWords = ( blobSize + 3 ) / 4;
    outSpirv.resize( numWords );
    std::memcpy( outSpirv.data(), blob->getBufferPointer(), blobSize );
    // Zero out any padding bytes in the last word
    if ( blobSize % 4 != 0 )
    {
        uint8_t* bytes = reinterpret_cast< uint8_t* >( outSpirv.data() );
        for ( size_t i = blobSize; i < numWords * 4; ++i )
        {
            bytes[i] = 0;
        }
    }
    return true;
}

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

    std::string hashInput = std::string( name ) + "@@SRC@@" + source + "@@CACHESALT_V3@@" + std::to_string(flags) + "@@ENTRY@@" + entry;
    hashInput += "@@DEFINES@@";
    for ( const auto& d : defines ) hashInput += d;
    hashInput += "@@INCLUDES@@";
    for ( const auto& i : includes ) hashInput += i;
    hashInput += "@@SRC@@";
    uint64_t hash = komihash( hashInput.data(), hashInput.size(), 0 );

    std::string prefix = std::string( name ) + "_" + std::to_string( hash );
    
    // Construct output filename compatible with ShaderMake's conventions (mostly for visual consistency in cache)
    std::string outputFilename = prefix;
    if ( entry && strcmp( entry, "main" ) != 0 )
        outputFilename += "_" + std::string( entry );
    outputFilename += ".spirv";
    std::filesystem::path spvPath = tempDir / outputFilename;

    if ( !g_vhInit.forceShaderRecompile && std::filesystem::exists( spvPath ) )
    {
        if ( vhLoadSpirvFile( spvPath, outSpirv ) )
        {
            // Apply descriptor set patching even when loading from cache.
            // The cache key does not include the patch flag, so we must patch after load.
            if ( flags & VRHI_SHADER_PATCH_DSET0 )
            {
                uint32_t stageSpace = vhGetDescriptorSetForStage( flags );
                vhPatchSpirvDescriptorSet0( outSpirv, stageSpace );
            }
            return true;
        }
    }

    std::filesystem::path sourceFilePath = tempDir / ( prefix + ".slang" );
    if ( g_vhInit.dumpShaderSource )
    {
        std::ofstream sourceFile( sourceFilePath );
        if ( !sourceFile.is_open() )
        {
            if ( outError ) *outError = "Failed to create temporary shader source file: " + sourceFilePath.string();
            return false;
        }
        sourceFile << source;
    }

    if ( !vhCompileShaderSlang( source, sourceFilePath, outSpirv, flags, entry, defines, includes, outError ) )
    {
        return false;
    }

    if ( !g_vhInit.skipShaderCacheWrite )
    {
        std::ofstream outFile( spvPath, std::ios::binary );
        if ( outFile.is_open() ) outFile.write( reinterpret_cast< const char* >( outSpirv.data() ), outSpirv.size() * sizeof( uint32_t ) );
    }
    if ( flags & VRHI_SHADER_PATCH_DSET0 )
    {
        uint32_t stageSpace = vhGetDescriptorSetForStage( flags );
        vhPatchSpirvDescriptorSet0( outSpirv, stageSpace );
    }
    return true;
}

vhShader vhCreateShader(
    vhShader shader,
    const char* name,
    uint64_t flags,
    const std::vector< uint32_t >& spirv,
    const char* entry
)
{
    if ( shader == VRHI_INVALID_HANDLE ) return shader;

    auto cmd = vhCmdAlloc< VIDL_vhCreateShader >( shader, name, flags, spirv, entry );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return shader;
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

nvrhi::ShaderHandle vhGetShaderNvrhiHandle( vhShader shader )
{
    return vhBackendQueryShaderHandle( shader );
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

bool vhShaderValidateBinding( const vhShaderReflectionResource& reflection, const nvrhi::BindingLayoutItem& binding, bool logError )
{
    if ( reflection.slot != binding.slot )
    {
        if ( logError ) VRHI_ERR( "Binding Slot %d mismatch in shader reflection (Expected Slot %d).\n", binding.slot, reflection.slot );
        return false;
    }

    if ( reflection.type != binding.type )
    {
        if ( logError ) VRHI_ERR( "Binding Slot %d type mismatch: Layout expects %d, Shader reflects %d.\n", binding.slot, ( int ) binding.type, ( int ) reflection.type );
        return false;
    }

    if ( reflection.arraySize != binding.getArraySize() )
    {
        if ( logError ) VRHI_ERR( "Binding Slot %d array size mismatch: Layout expects %d, Shader reflects %d.\n", binding.slot, binding.getArraySize(), reflection.arraySize );
        return false;
    }

    return true;
}

void vhPackUserGlobals(
    const std::vector< vhState::UniformBufferValue >& uniforms,
    const std::vector< vhReflectionMember >& members,
    uint8_t* outData,
    uint64_t dataSize
)
{
    if ( !outData || dataSize == 0 )
        return;

    memset( outData, 0, dataSize );

    for ( const auto& member : members )
    {
        const vhState::UniformBufferValue* match = nullptr;
        for ( const auto& u : uniforms )
        {
            if ( u.name == member.name )
            {
                match = &u;
                break;
            }
        }

        if ( match )
        {
            if ( member.offset >= dataSize )
                continue;

            uint64_t copySize = member.size;
            if ( member.offset + copySize > dataSize )
            {
                copySize = dataSize - member.offset;
            }

            uint64_t srcSizeBytes = match->data.size() * sizeof( glm::vec4 );
            uint64_t actualCopy = std::min( copySize, srcSizeBytes );
            memcpy( outData + member.offset, match->data.data(), actualCopy );
        }
    }
}

// ------------ Raytracing Allocations ------------

vhAccelStruct vhAllocAS()
{
    std::lock_guard< std::mutex > lock( g_vhAccelStructIDListMutex );
    uint32_t id = g_vhAccelStructIDList.alloc();
    g_vhAccelStructIDValid[id] = true;
    return id;
}

vhRTPipeline vhAllocRTPipeline()
{
    std::lock_guard< std::mutex > lock( g_vhRTPipelineIDListMutex );
    uint32_t id = g_vhRTPipelineIDList.alloc();
    g_vhRTPipelineIDValid[id] = true;
    return id;
}

vhShaderTable vhAllocShaderTable()
{
    std::lock_guard< std::mutex > lock( g_vhShaderTableIDListMutex );
    uint32_t id = g_vhShaderTableIDList.alloc();
    g_vhShaderTableIDValid[id] = true;
    return id;
}

vhAccelStruct vhCreateAS( vhAccelStruct as, const nvrhi::rt::AccelStructDesc& desc )
{
    auto cmd = vhCmdAlloc< VIDL_vhCreateAS >( as, desc );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return as;
}

void vhDestroyAS( vhAccelStruct as )
{
    std::lock_guard< std::mutex > lock( g_vhAccelStructIDListMutex );
    if ( g_vhAccelStructIDValid.find( as ) == g_vhAccelStructIDValid.end() ) return;
    g_vhAccelStructIDValid.erase( as );
    g_vhAccelStructIDList.release( as );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyAS>( as );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhBuildBLAS( vhAccelStruct blas, std::vector< nvrhi::rt::GeometryDesc > geometries )
{
    auto cmd = vhCmdAlloc< VIDL_vhBuildBLAS >( blas, std::move( geometries ) );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhBuildTLAS( vhAccelStruct tlas, std::vector< nvrhi::rt::InstanceDesc > instances )
{
    auto cmd = vhCmdAlloc< VIDL_vhBuildTLAS >( tlas, std::move( instances ) );
    assert( cmd );
    vhCmdEnqueue( cmd );
}



vhRTPipeline vhCreateRTPipeline( vhRTPipeline pipeline, const nvrhi::rt::PipelineDesc& desc )
{
    auto cmd = vhCmdAlloc< VIDL_vhCreateRTPipeline >( pipeline, desc );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return pipeline;
}

void vhDestroyRTPipeline( vhRTPipeline pipeline )
{
    std::lock_guard< std::mutex > lock( g_vhRTPipelineIDListMutex );
    if ( g_vhRTPipelineIDValid.find( pipeline ) == g_vhRTPipelineIDValid.end() ) return;
    g_vhRTPipelineIDValid.erase( pipeline );
    g_vhRTPipelineIDList.release( pipeline );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyRTPipeline>( pipeline );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

vhShaderTable vhCreateShaderTable( vhShaderTable table, vhRTPipeline pipeline )
{
    auto cmd = vhCmdAlloc< VIDL_vhCreateShaderTable >( table, pipeline );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return table;
}

void vhDestroyShaderTable( vhShaderTable table )
{
    std::lock_guard< std::mutex > lock( g_vhShaderTableIDListMutex );
    if ( g_vhShaderTableIDValid.find( table ) == g_vhShaderTableIDValid.end() ) return;
    g_vhShaderTableIDValid.erase( table );
    g_vhShaderTableIDList.release( table );

    auto cmd = vhCmdAlloc<VIDL_vhDestroyShaderTable>( table );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhShaderTableSetRayGen( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet )
{
    if ( !exportName ) exportName = "";
    auto cmd = vhCmdAlloc< VIDL_vhShaderTableSetRayGen >( table, exportName, bindingSet );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhShaderTableAddMiss( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet )
{
    if ( !exportName ) exportName = "";
    auto cmd = vhCmdAlloc< VIDL_vhShaderTableAddMiss >( table, exportName, bindingSet );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhShaderTableAddHitGroup( vhShaderTable table, const char* exportName, nvrhi::BindingSetHandle bindingSet )
{
    if ( !exportName ) exportName = "";
    auto cmd = vhCmdAlloc< VIDL_vhShaderTableAddHitGroup >( table, exportName, bindingSet );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhDispatchRays( vhStateId stateID, vhShaderTable table, const nvrhi::rt::DispatchRaysArguments& args )
{
    auto cmd = vhCmdAlloc< VIDL_vhDispatchRays >( stateID, table, args );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

nvrhi::rt::AccelStructHandle vhGetASNvrhiHandle( vhAccelStruct as )
{
    return vhBackendQueryAccelStructHandle( as );
}

nvrhi::rt::PipelineHandle vhGetRTPipelineNvrhiHandle( vhRTPipeline pipeline )
{
    return vhBackendQueryRTPipelineHandle( pipeline );
}

nvrhi::rt::ShaderTableHandle vhGetShaderTableNvrhiHandle( vhShaderTable table )
{
    return vhBackendQueryShaderTableHandle( table );
}

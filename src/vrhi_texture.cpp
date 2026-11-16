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


// ------------ Texture Utilities ------------

glm::ivec2 vhGetImageSliceSize( const vhFormatInfo& info, const glm::ivec3& dimensions )
{
    int pitch = -1, blockHeight = -1;

    bool compressed = info.compressionBlockWidth != -1;
    if ( compressed )
    {
        // For compressed formats, round dimensions to blockWidth / blockHeight.
        assert( info.compressionBlockWidth > 0 && info.compressionBlockHeight > 0 && info.elementSize > 0 );
        pitch = std::max( 1, ( ( dimensions.x + ( info.compressionBlockWidth - 1 ) ) / info.compressionBlockWidth ) ) * info.elementSize;
        blockHeight = std::max( 1, ( ( dimensions.y + ( info.compressionBlockHeight - 1 ) ) / info.compressionBlockHeight ) );
    }
    else
    {
        // For non-compressed formats, treat as tightly packed.
        pitch = dimensions.x * info.elementSize;
        blockHeight = dimensions.y;
    }

    return glm::ivec2( pitch * blockHeight, pitch );
}

void vhTextureMiplevelInfo( std::vector< vhTextureMipInfo >& mipInfo, int64_t& pitchSize, int64_t& arraySize, const vhTexInfo& info )
{
    mipInfo.clear();
    pitchSize = 0;

    auto levelDimensions = info.dimensions;
    auto formatInfo = vhGetFormat( info.format );
    bool compressed = formatInfo.compressionBlockWidth > 0;

    // Figure out texture dimensionality from target.
    int dim = 0;
    switch ( info.target )
    {
        case nvrhi::TextureDimension::Texture1D: dim = 1; break;
        case nvrhi::TextureDimension::Texture2D: dim = 2; break;
        case nvrhi::TextureDimension::Texture2DArray: dim = 2; break;
        case nvrhi::TextureDimension::Texture3D: dim = 3; break;
        case nvrhi::TextureDimension::TextureCube: dim = 2; break;
        case nvrhi::TextureDimension::TextureCubeArray: dim = 2; break;
        default:
            assert( !"Unknown texture target." );
            break;
    }

    // Loop through mip map levels and calculate info.
    int offset = 0;
    for ( int i = 0; i < info.mipLevels; i++ )
    {
        mipInfo.push_back( vhTextureMipInfo() );
        vhTextureMipInfo& linfo = mipInfo.back();

        linfo.dimensions = levelDimensions;

        auto sinfo = vhGetImageSliceSize( formatInfo, levelDimensions );
        linfo.slice_size = sinfo.x;
        linfo.pitch = sinfo.y;
        linfo.size = linfo.slice_size * levelDimensions.z;
        linfo.offset = offset;
        offset += ( int ) linfo.size;

        // Calculate the size of the next mipmap level.
        levelDimensions = vhGetImageNextMipmapDim( levelDimensions );
    }

    arraySize = offset;
    pitchSize = arraySize * info.arrayLayers;
}

int64_t vhGetRegionDataSize( const vhFormatInfo& info, glm::ivec3 extent, int mipLevel )
{
    ( void ) mipLevel;
    if ( extent.x <= 0 || extent.y <= 0 || extent.z <= 0 ) return 0;
    auto sinfo = vhGetImageSliceSize( info, extent );
    return ( int64_t ) sinfo.x * extent.z;
}

bool vhVerifyRegionInTexture( const vhFormatInfo& fmt, glm::ivec3 mipDimensions, glm::ivec3 offset, glm::ivec3 extent, const char* debugName )
{
    if ( extent.x < 0 || extent.y < 0 || extent.z < 0 )
    {
        VRHI_ERR( "%s: Invalid extent (%d, %d, %d)\n", debugName, extent.x, extent.y, extent.z );
        return false;
    }
    if ( offset.x < 0 || offset.y < 0 || offset.z < 0 )
    {
        VRHI_ERR( "%s: Invalid offset (%d, %d, %d)\n", debugName, offset.x, offset.y, offset.z );
        return false;
    }
    if ( offset.x + extent.x > mipDimensions.x ||
        offset.y + extent.y > mipDimensions.y ||
        offset.z + extent.z > mipDimensions.z )
    {
        VRHI_ERR( "%s: Region [%d, %d, %d] + [%d, %d, %d] exceeds mip dimensions [%d, %d, %d]\n",
            debugName, offset.x, offset.y, offset.z, extent.x, extent.y, extent.z, mipDimensions.x, mipDimensions.y, mipDimensions.z );
        return false;
    }
    return true;
}

static bool vhIsAliasEligible2DTextureDesc( const nvrhi::TextureDesc& desc )
{
    const bool isPlain2D =
        desc.dimension == nvrhi::TextureDimension::Texture2D &&
        desc.depth == 1 &&
        desc.arraySize == 1 &&
        desc.sampleCount == 1;
    const bool isStandardPlacement =
        desc.isVirtual &&
        !desc.isTiled &&
        desc.sharedResourceFlags == nvrhi::SharedResourceFlags::None &&
        !desc.isTypeless &&
        desc.format != nvrhi::Format::UNKNOWN;
    return isPlain2D && isStandardPlacement;
}

static bool vhIsValidAliasActivationState( nvrhi::ResourceStates state, const nvrhi::TextureDesc& desc )
{
    const auto& format = nvrhi::getFormatInfo( desc.format );
    const bool isDepthStencil = format.hasDepth || format.hasStencil;
    switch ( state )
    {
        case nvrhi::ResourceStates::CopyDest:
            return true;
        case nvrhi::ResourceStates::RenderTarget:
            return desc.isRenderTarget && !isDepthStencil;
        case nvrhi::ResourceStates::DepthWrite:
            return desc.isRenderTarget && isDepthStencil;
        case nvrhi::ResourceStates::UnorderedAccess:
            return desc.isUAV;
        default:
            return false;
    }
}

bool vhVerifyTextureMemoryBinding( const nvrhi::TextureDesc& desc, glm::u64vec2 requirements, uint64_t offset, uint64_t capacity )
{
    if ( requirements.x == 0 || requirements.y == 0 )
    {
        VRHI_ERR( "vhBindTextureMemory(): Invalid memory requirements for texture '%s'\n", desc.debugName.c_str() );
        return false;
    }
    if ( ( offset % requirements.y ) != 0 )
    {
        VRHI_ERR( "vhBindTextureMemory(): Offset %llu is not aligned to %llu for texture '%s'\n", offset, requirements.y, desc.debugName.c_str() );
        return false;
    }
    if ( offset > capacity || requirements.x > capacity - offset )
    {
        VRHI_ERR( "vhBindTextureMemory(): Range [%llu, +%llu) exceeds heap capacity %llu\n", offset, requirements.x, capacity );
        return false;
    }
    return true;
}

bool vhVerifyTextureMemoryOverlap( const nvrhi::TextureDesc& desc, glm::u64vec2 range, const nvrhi::TextureDesc& otherDesc, glm::u64vec2 otherRange )
{
    const bool overlaps = range.x < otherRange.x + otherRange.y && otherRange.x < range.x + range.y;
    if ( !overlaps ) return true;

    if ( range.x != otherRange.x )
    {
        VRHI_ERR( "vhBindTextureMemory(): Overlapping binding for texture '%s' must start at offset %llu (got %llu)\n", desc.debugName.c_str(), otherRange.x, range.x );
        return false;
    }
    if ( !vhIsAliasEligible2DTextureDesc( desc ) || !vhIsAliasEligible2DTextureDesc( otherDesc ) )
    {
        VRHI_ERR( "vhBindTextureMemory(): Textures '%s' and '%s' cannot overlap: unsupported texture class\n", desc.debugName.c_str(), otherDesc.debugName.c_str() );
        return false;
    }
#ifdef __APPLE__
    // Shared placement has not been qualified with MoltenVK's Metal heap configurations.
    VRHI_ERR( "vhBindTextureMemory(): Shared texture placement is not supported on macOS\n" );
    return false;
#endif
    if ( desc.format != otherDesc.format )
    {
        VRHI_ERR( "vhBindTextureMemory(): Texture '%s' format does not match texture '%s' in the same region\n", desc.debugName.c_str(), otherDesc.debugName.c_str() );
        return false;
    }
    const bool sameUsage = desc.isRenderTarget == otherDesc.isRenderTarget && desc.isUAV == otherDesc.isUAV;
    if ( !sameUsage )
    {
        VRHI_ERR( "vhBindTextureMemory(): Texture '%s' render-target/UAV usage does not match texture '%s' in the same region\n", desc.debugName.c_str(), otherDesc.debugName.c_str() );
        return false;
    }
    return true;
}

bool vhVerifyTextureAliasState( const nvrhi::TextureDesc& desc, nvrhi::ResourceStates state )
{
    if ( !vhIsAliasEligible2DTextureDesc( desc ) )
    {
        VRHI_ERR( "vhActivateTextureAliases(): Texture '%s' does not satisfy 2D alias contract\n", desc.debugName.c_str() );
        return false;
    }
    if ( !vhIsValidAliasActivationState( state, desc ) )
    {
        VRHI_ERR( "vhActivateTextureAliases(): Invalid activation state for texture '%s'\n", desc.debugName.c_str() );
        return false;
    }
    return true;
}

// ------------ Texture Implementation ------------

vhTexture vhAllocTexture()
{
    std::lock_guard< std::mutex > lock( g_vhTextureIDListMutex );
    uint32_t id = g_vhTextureIDList.alloc();
    g_vhTextureIDValid[id] = true;

    vhResetTexture( id );
    return id;
}

void vhResetTexture( vhTexture texture )
{
    if ( texture == VRHI_INVALID_HANDLE ) return;

    auto cmd = vhCmdAlloc<VIDL_vhResetTexture>( texture );
    assert( cmd );
    vhCmdEnqueue( cmd );
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

vhTexture vhCreateTexture(
    vhTexture texture,
    const char* name,
    nvrhi::TextureDimension target,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag,
    const vhMem* data
)
{
    if ( texture == VRHI_INVALID_HANDLE ) return texture;

    // Verify parameters

    if ( target == nvrhi::TextureDimension::TextureCube || target == nvrhi::TextureDimension::TextureCubeArray )
    {
        dimensions.y = dimensions.x;
        dimensions.z = 1;
    }
    if ( target == nvrhi::TextureDimension::TextureCube )
    {
        numLayers = 6;
    }
    if ( target == nvrhi::TextureDimension::TextureCubeArray )
    {
        numLayers *= 6;
    }
    if ( target == nvrhi::TextureDimension::Texture2DArray )
    {
        dimensions.z = 1;
    }

    // Resolve VRHI_MIPMAP_COMPLETE to actual mipmap count.
    if ( numMips == VRHI_MIPMAP_COMPLETE )
    {
        numMips = vhGetImageMaxMipCount( dimensions );
    }

    // Queue up command to create texture
    auto cmd = vhCmdAlloc<VIDL_vhCreateTexture>( texture, vhArenaCopyString( name ), target, dimensions, numMips, numLayers, format, flag, data );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return texture;
}

void vhUpdateTexture(
    vhTexture texture,
    int startMips, int startLayers,
    int numMips, int numLayers,
    const vhMem* data
)
{
    if ( !data ) return;

    // Queue up command to update texture
    auto cmd = vhCmdAlloc<VIDL_vhUpdateTexture>( texture, startMips, startLayers, numMips, numLayers, data );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhReadTextureSlow(
    vhTexture texture,
    int mip, int layer,
    vhMem* outData
)
{
    // Ensure all pending GPU work is complete before reading
    vhFinish();

    // Queue up command to read texture
    auto cmd = vhCmdAlloc<VIDL_vhReadTextureSlow>( texture, mip, layer, outData );
    assert( cmd );
    vhCmdEnqueue( cmd );

    // Wait for readback to complete
    vhFinish();
}

void vhBlitTexture(
    vhTexture dst, vhTexture src,
    int dstMip, int srcMip,
    int dstLayer, int srcLayer,
    glm::ivec3 dstOffset, glm::ivec3 srcOffset,
    glm::ivec3 extent
)
{
    if ( dst == VRHI_INVALID_HANDLE || src == VRHI_INVALID_HANDLE ) return;

    // Queue up command to blit texture
    auto cmd = vhCmdAlloc<VIDL_vhBlitTexture>( dst, src, dstMip, srcMip, dstLayer, srcLayer, dstOffset, srcOffset, extent );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

vhTexInfo vhGetTextureInfo( vhTexture texture, std::vector< vhTextureMipInfo >* outMipInfo )
{
    return vhBackendQueryTextureInfo( texture, outMipInfo );
}

nvrhi::TextureHandle vhGetTextureNvrhiHandle( vhTexture texture )
{
    return vhBackendQueryTextureHandle( texture );
}

bool vhBindTextureMemory_DeviceStateLocked( nvrhi::ITexture* texture, nvrhi::IHeap* heap, uint64_t offset )
{
    // NVRHI uses a throwing Vulkan-Hpp overload for the native image bind.
    bool bound = false;
    try
    {
        bound = g_vhDevice->bindTextureMemory( texture, heap, offset );
    }
    catch ( const vk::SystemError& e )
    {
        VRHI_ERR( "vhBindTextureMemory(): Failed to bind texture '%s' at offset %llu: %s\n", texture->getDesc().debugName.c_str(), offset, e.what() );
        return false;
    }
    if ( !bound )
    {
        VRHI_ERR( "vhBindTextureMemory(): Failed to bind texture '%s' at offset %llu\n", texture->getDesc().debugName.c_str(), offset );
        return false;
    }
    return true;
}

bool vhIsTextureMemoryBound( vhTexture texture )
{
    if ( texture == VRHI_INVALID_HANDLE )
    {
        VRHI_ERR( "vhIsTextureMemoryBound(): Invalid texture handle\n" );
        return false;
    }
    if ( g_vhNullMode ) return false;

    return vhBackendQueryTextureMemoryBound( texture );
}

void vhActivateTextureAliases( const std::vector< vhTextureAlias >& aliases )
{
    if ( aliases.empty() )
    {
        return;
    }

    for ( const vhTextureAlias& alias : aliases )
    {
        if ( alias.texture == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhActivateTextureAliases(): Invalid texture handle\n" );
            return;
        }
    }

    auto cmd = vhCmdAlloc<VIDL_vhActivateTextureAliases>( vhArenaCopySpan( aliases ) );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhActivateTextureAlias_DeviceStateLocked( nvrhi::ICommandList* cmdList, nvrhi::ITexture* texture, nvrhi::ResourceStates state )
{
    cmdList->beginTrackingTextureState( texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common );
    cmdList->setTextureState( texture, nvrhi::AllSubresources, state );
}

// ------------ Sampler Implementation ------------

nvrhi::SamplerDesc vhGetSamplerDesc( uint64_t samplerFlags )
{
    nvrhi::SamplerDesc desc;

    auto mapAddrMode = []( uint32_t mode ) -> nvrhi::SamplerAddressMode
    {
        switch ( mode ) {
            case 0: return nvrhi::SamplerAddressMode::Wrap;
            case 1: return nvrhi::SamplerAddressMode::Mirror;
            case 2: return nvrhi::SamplerAddressMode::Clamp;
            case 3: return nvrhi::SamplerAddressMode::Border;
            default: return nvrhi::SamplerAddressMode::Wrap;
        }
    };

    desc.addressU = mapAddrMode( ( samplerFlags >> VRHI_SAMPLER_U_SHIFT ) & 0x3 );
    desc.addressV = mapAddrMode( ( samplerFlags >> VRHI_SAMPLER_V_SHIFT ) & 0x3 );
    desc.addressW = mapAddrMode( ( samplerFlags >> VRHI_SAMPLER_W_SHIFT ) & 0x3 );

    desc.minFilter = ( ( samplerFlags >> VRHI_SAMPLER_MIN_SHIFT ) & 0x3 ) != 1; // 1 is POINT, 0 is LINEAR, 2 is ANISO
    desc.magFilter = ( ( samplerFlags >> VRHI_SAMPLER_MAG_SHIFT ) & 0x3 ) != 1;
    desc.mipFilter = ( ( samplerFlags >> VRHI_SAMPLER_MIP_SHIFT ) & 0x3 ) == 0; // 0 is LINEAR, 1 is POINT, 2 is NONE. 

    // Mip Bias: 8-bit signed 4.4 fixed point
    int8_t biasRaw = ( int8_t ) ( ( samplerFlags >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFF );
    desc.mipBias = ( float ) biasRaw / 16.0f;

    // Border Colour: 4-bit index
    uint32_t borderColour = ( samplerFlags >> VRHI_SAMPLER_BORDER_COLOUR_SHIFT ) & 0xF;
    if ( borderColour == 0 ) desc.borderColor = nvrhi::Color( 0.f );
    else if ( borderColour == 1 ) desc.borderColor = nvrhi::Color( 0.f, 0.f, 0.f, 1.f ); // Black
    else desc.borderColor = nvrhi::Color( 1.f ); // Default White

    // Max Anisotropy: 3-bit index
    uint32_t anisoIndex = ( samplerFlags >> VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT ) & 0x7;
    static float anisoMap[] = { 1.f, 2.f, 4.f, 8.f, 16.f, 1.f, 1.f, 1.f };
    desc.maxAnisotropy = anisoMap[anisoIndex];

    uint32_t cmpFunc = ( samplerFlags >> VRHI_SAMPLER_COMPARE_SHIFT ) & 0xF;
    if ( cmpFunc != 0 )
    {
        desc.reductionType = nvrhi::SamplerReductionType::Comparison;
        static const nvrhi::ComparisonFunc compareFuncMap[] =
        {
            nvrhi::ComparisonFunc::Always,
            nvrhi::ComparisonFunc::Less,
            nvrhi::ComparisonFunc::LessOrEqual,
            nvrhi::ComparisonFunc::Equal,
            nvrhi::ComparisonFunc::GreaterOrEqual,
            nvrhi::ComparisonFunc::Greater,
            nvrhi::ComparisonFunc::NotEqual,
            nvrhi::ComparisonFunc::Never,
            nvrhi::ComparisonFunc::Always,
        };
        desc.compareFunc = ( cmpFunc < 9 ) ? compareFuncMap[cmpFunc] : nvrhi::ComparisonFunc::Always;
    }

    return desc;
}

static std::unordered_map< uint64_t, nvrhi::SamplerHandle > s_samplerCache;
static std::mutex s_samplerCacheMutex;

nvrhi::SamplerHandle vhGetSamplerHandle( uint64_t samplerFlags )
{
    nvrhi::SamplerDesc desc = vhGetSamplerDesc( samplerFlags );
    if ( !g_vhDeviceInfo.samplerMipLodBias && desc.mipBias != 0.f )
    {
        return nullptr;
    }

    uint64_t hash = vhHashSamplerDesc( desc );

    std::lock_guard< std::mutex > lock( s_samplerCacheMutex );
    auto it = s_samplerCache.find( hash );
    if ( it != s_samplerCache.end() )
    {
        assert( it->second );
        return it->second;
    }

    nvrhi::SamplerHandle sampler = nullptr;
    {
        std::lock_guard< std::mutex > lock2( g_nvRHIStateMutex );
        sampler = g_vhDevice->createSampler( desc );
    }
    if ( sampler ) s_samplerCache[hash] = sampler;
    return sampler;
}

void vhSamplerCacheShutdown()
{
    std::lock_guard< std::mutex > lock2( g_nvRHIStateMutex );
    s_samplerCache.clear(); 
}


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

void vhTextureMiplevelInfo( std::vector< vhTextureMipInfo >& mipInfo, int64_t &pitchSize, int64_t& arraySize, const vhTexInfo& info )
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
		offset += linfo.size;

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

void vhCreateTexture(
    vhTexture texture,
    nvrhi::TextureDimension target,
    glm::ivec3 dimensions,
    int numMips, int numLayers,
    nvrhi::Format format,
    uint64_t flag,
    const vhMem* data
)
{
    if ( texture == VRHI_INVALID_HANDLE ) return;
    
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
    
    // Queue up command to create texture
    auto cmd = vhCmdAlloc<VIDL_vhCreateTexture>( texture, target, dimensions, numMips, numLayers, format, flag, data );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhUpdateTexture(
    vhTexture texture,
    int startMips, int startLayers,
    int numMips, int numLayers,
    const vhMem* data,
    glm::ivec3 offset,
    glm::ivec3 extent
)
{
    if ( !data ) return;

    // Queue up command to update texture
    auto cmd = vhCmdAlloc<VIDL_vhUpdateTexture>( texture, startMips, startLayers, numMips, numLayers, data, offset, extent );
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

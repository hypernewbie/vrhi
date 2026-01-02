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

// -------------------------------------------------------- Utils --------------------------------------------------------

// Allocator for list of object IDs.
// Works be using a free list.
class vhAllocatorObjectFreeList
{
    vhAllocatorObjectFreeList( const vhAllocatorObjectFreeList& ) = delete;
    vhAllocatorObjectFreeList& operator=( const vhAllocatorObjectFreeList& ) = delete;

    std::vector< uint32_t > m_freeList;
    uint32_t m_allocCount = 0;
    uint32_t m_size = 0;
    uint32_t m_end = 0;

public:
    vhAllocatorObjectFreeList() {}
    vhAllocatorObjectFreeList( uint32_t sz ) { m_size = sz; }

    int32_t alloc( int32_t size = 1 /* unused */, int32_t algn = 0 /* unused */ )
    {
        if ( size == 0 ) return -1;
        if ( size != 1 ) return -1;
        if ( algn != 0 && algn != -1 ) return -1;

        uint32_t ret = m_end;

        if ( m_freeList.size( ) > 0 ) {
            ret = m_freeList[m_freeList.size( ) - 1];
            m_freeList.pop_back( );
            m_allocCount++;
            return ret;
        }
        if ( ( uint32_t ) ret + size > m_size ) {
            return -1;
        }
        m_end = ret + size;
        m_allocCount++;
        return ret;
    }

    void release( int32_t addr )
    {
        m_freeList.push_back( addr );
        m_allocCount--;
        return;
    }

    void purge()
    {
        m_freeList.clear( );
        m_end = 0;
        m_allocCount = 0;
    }
};


// ------------ Texture Utilities ------------

// Get next mipmap dimension
inline int vhGetImageNextMipmapDim( int x )
{
	return ( x > 1 ) ? ( x >> 1 ) : 1;
}

// Get next mipmap dimension
inline glm::ivec3 vhGetImageNextMipmapDim( const glm::ivec3& dimensions )
{
	return glm::ivec3(
        vhGetImageNextMipmapDim( dimensions.x ),
        vhGetImageNextMipmapDim( dimensions.y ),
        vhGetImageNextMipmapDim( dimensions.z )
    );
}

// Get the size of a single array slice of the image
glm::ivec2 vhGetImageSliceSize( const vhFormatInfo& info, const glm::ivec3& dimensions );

// Get the mip level info for the entire texture
void vhTextureMiplevelInfo( std::vector< vhTextureMipInfo >& mipInfo, int64_t &pitchSize, int64_t& arraySize, const vhTexInfo& info );

// Round the size to the next power of 2.

inline uint32_t vhNextPow2( uint32_t v )

{

    if ( v == 0 ) return 1u;

    v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++;

    return v;

}

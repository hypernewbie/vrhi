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
#include "vdeps/OffsetAllocator/offsetAllocator.hpp"

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
#include <cctype>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

// Forward declaration for OffsetAllocator internal type
namespace OffsetAllocator
{
    class Allocator;
}

// Helper to resolve NVRHI format from type string
nvrhi::Format vhGetFormatFromTypeString( const std::string& type, int count )
{
    using namespace nvrhi;
    if ( type == "float" )
    {
        if ( count == 1 ) return Format::R32_FLOAT;
        if ( count == 2 ) return Format::RG32_FLOAT;
        if ( count == 3 ) return Format::RGB32_FLOAT;
        if ( count == 4 ) return Format::RGBA32_FLOAT;
    }
    else if ( type == "half" )
    {
        if ( count == 1 ) return Format::R16_FLOAT;
        if ( count == 2 ) return Format::RG16_FLOAT;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA16_FLOAT;
    }
    else if ( type == "int" )
    {
        if ( count == 1 ) return Format::R32_SINT;
        if ( count == 2 ) return Format::RG32_SINT;
        if ( count == 3 ) return Format::RGB32_SINT;
        if ( count == 4 ) return Format::RGBA32_SINT;
    }
    else if ( type == "uint" )
    {
        if ( count == 1 ) return Format::R32_UINT;
        if ( count == 2 ) return Format::RG32_UINT;
        if ( count == 3 ) return Format::RGB32_UINT;
        if ( count == 4 ) return Format::RGBA32_UINT;
    }
    else if ( type == "short" )
    {
        if ( count == 1 ) return Format::R16_SINT;
        if ( count == 2 ) return Format::RG16_SINT;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA16_SINT;
    }
    else if ( type == "ushort" )
    {
        if ( count == 1 ) return Format::R16_UINT;
        if ( count == 2 ) return Format::RG16_UINT;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA16_UINT;
    }
    else if ( type == "byte" )
    {
        if ( count == 1 ) return Format::R8_SINT;
        if ( count == 2 ) return Format::RG8_SINT;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA8_SINT;
    }
    else if ( type == "ubyte" )
    {
        if ( count == 1 ) return Format::R8_UINT;
        if ( count == 2 ) return Format::RG8_UINT;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA8_UINT;
    }
    else if ( type == "unorm" )
    {
        if ( count == 1 ) return Format::R8_UNORM;
        if ( count == 2 ) return Format::RG8_UNORM;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA8_UNORM;
    }
    else if ( type == "snorm" )
    {
        if ( count == 1 ) return Format::R8_SNORM;
        if ( count == 2 ) return Format::RG8_SNORM;
        if ( count == 3 ) return Format::UNKNOWN;
        if ( count == 4 ) return Format::RGBA8_SNORM;
    }
    return Format::UNKNOWN;
}

// Parses a vertex layout string.
// Vertex layouts are defined as standard strings.
// Format: "TYPE[COUNT][:i] [ATTRn][:i]"
// Supported types: float, half, int, uint, short, ushort, byte, ubyte, unorm, snorm.
// The :i suffix marks an attribute as instanced (per-instance rate).
// Examples: 
//   "float3" -> Location 0 (Implicit), per-vertex
//   "float3 ATTR5" -> Location 5 (Explicit), per-vertex
//   "float3 float2" -> Loc 0, Loc 1, per-vertex
//   "float4:i" -> Location 0 (Implicit), per-instance
//   "mat4 ATTR5:i" -> Location 5 (Explicit), per-instance
//
// Note: All attributes in a buffer must have consistent instancing rates.
// Use vhState::VertexBinding::isInstanced to override at bind time.
// If both layout string and VertexBinding specify isInstanced, they must match.
//
//
template < bool EMIT_OUTPUT >
bool vhParseVertexLayout( const vhVertexLayout& layout, std::vector<vhVertexLayoutDef>* outDefs )
{
    if constexpr ( EMIT_OUTPUT )
    {
        if ( outDefs ) outDefs->clear();
    }

    const char* ptr = layout.c_str();
    const char* baseTypes[] = { "float", "half", "int", "uint", "short", "ushort", "byte", "ubyte", "unorm", "snorm", nullptr };
    int currentOffset = 0;
    int currentLocation = 0;

    // Collision detection
    std::set<int> usedLocations;

    while ( *ptr )
    {
        // Skip whitespace
        while ( *ptr && isspace( *ptr ) ) ptr++;
        if ( !*ptr ) break;

        // Extract Type Token
        const char* tStart = ptr;
        while ( *ptr && !isspace( *ptr ) ) ptr++;
        std::string typeStr( tStart, ptr );

        // Validate Type & Count
        std::string baseType;
        int componentCount = 1;
        bool typeValid = false;
        bool isInstanced = false;

        // Check for :i suffix (instanced) - avoid string allocation
        size_t typeLen = typeStr.size();
        if ( typeLen >= 2 && typeStr[typeLen - 2] == ':' && typeStr[typeLen - 1] == 'i' )
        {
            isInstanced = true;
            typeLen -= 2;
        }

        for ( const char** bt = baseTypes; *bt; ++bt )
        {
            std::string base = *bt;
            if ( ( int ) typeLen >= ( int ) base.size() && typeStr.compare( 0, base.size(), base ) == 0 )
            {
                int suffixLen = ( int ) typeLen - ( int ) base.size();
                if ( suffixLen == 0 )
                {
                    typeValid = true;
                    baseType = base;
                    componentCount = 1;
                    break;
                }
                else if ( suffixLen == 1 && typeStr[base.size()] >= '2' && typeStr[base.size()] <= '4' )
                {
                    typeValid = true;
                    baseType = base;
                    componentCount = typeStr[base.size()] - '0';
                    break;
                }
            }
        }
        if ( !typeValid ) return false;

        // Resolve Format
        nvrhi::Format format = vhGetFormatFromTypeString( baseType, componentCount );
        if ( format == nvrhi::Format::UNKNOWN ) return false;

        // Skip whitespace
        while ( *ptr && isspace( *ptr ) ) ptr++;

        // Peek next token for Location (ATTRn)
        int resolvedLocation = currentLocation;
        if ( *ptr )
        {
            const char* peek = ptr;
            if ( strncmp( peek, "ATTR", 4 ) == 0 )
            {
                // Explicit Location
                ptr += 4;
                const char* numStart = ptr;
                while ( *ptr && isdigit( *ptr ) ) ptr++;
                std::string numStr( numStart, ptr );

                if ( numStr.empty() ) return false; // "ATTR" without number
                resolvedLocation = std::stoi( numStr );

                // Update implicit counter to next
                currentLocation = resolvedLocation + 1;
            }
            else
            {
                // Implicit: Use currentLocation and increment
                currentLocation++;
            }
        }
        else
        {
            // End of string, use implicit
            currentLocation++;
        }

        // Validate Location Collision
        if ( usedLocations.find( resolvedLocation ) != usedLocations.end() ) return false;
        usedLocations.insert( resolvedLocation );

        if constexpr ( EMIT_OUTPUT )
        {
            if ( outDefs )
            {
                vhVertexLayoutDef def;
                def.format = format;
                def.location = resolvedLocation;
                def.offset = currentOffset;
                def.isInstanced = isInstanced;
                outDefs->push_back( def );
            }
        }

        const auto& fmtInfo = nvrhi::getFormatInfo( format );
        currentOffset += fmtInfo.bytesPerBlock;
    }

    return !usedLocations.empty();
}

bool vhValidateVertexLayout( const vhVertexLayout& layout )
{
    return vhParseVertexLayout< false >( layout, nullptr );
}

uint32_t vhGetVertexLayoutStride( const vhVertexLayout& layout )
{
    std::vector< vhVertexLayoutDef > defs;
    if ( !vhParseVertexLayoutInternal( layout, defs ) ) return 0;
    return static_cast< uint32_t >( vhVertexLayoutDefSize( defs ) );
}

bool vhParseVertexLayoutInternal( const vhVertexLayout& layout, std::vector< vhVertexLayoutDef >& outDefs )
{
    return vhParseVertexLayout< true >( layout, &outDefs );
}

int vhVertexLayoutDefSize( const vhVertexLayoutDef& def )
{
    const nvrhi::FormatInfo& info = nvrhi::getFormatInfo( def.format );
    return info.bytesPerBlock;
}

int vhVertexLayoutDefSize( const std::vector< vhVertexLayoutDef >& def )
{
    if ( def.empty() ) return 0;
    auto& lastDef = def.back();
    return lastDef.offset + vhVertexLayoutDefSize( lastDef );
}

int64_t vhValidateAttributeMatch( const vhVertexLayoutDef& bufferDef, const std::vector<vhVertexLayoutDef>& shaderInputLayout )
{
    // Check against all shader inputs
    for ( size_t i = 0; i < shaderInputLayout.size(); ++i )
    {
        const auto& shaderDef = shaderInputLayout[i];

        if ( shaderDef.location == bufferDef.location )
        {
            // Strict matching: Format must be identical.
            if ( bufferDef.format == shaderDef.format )
            {
                return ( int64_t ) i;
            }

            return -1;
        }
    }

    // Location not found in shader (Unused).
    return -INT64_MAX;
}

vhBuffer vhAllocBuffer()
{
    std::lock_guard<std::mutex> lock( g_vhBufferIDListMutex );
    uint32_t id = g_vhBufferIDList.alloc();
    g_vhBufferIDValid[id] = true;

    vhResetBuffer( id );
    return id;
}

void vhResetBuffer( vhBuffer buffer )
{
    if ( buffer == VRHI_INVALID_HANDLE ) return;

    auto cmd = vhCmdAlloc<VIDL_vhResetBuffer>( buffer );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

void vhDestroyBuffer( vhBuffer buffer )
{
    std::lock_guard<std::mutex> lock( g_vhBufferIDListMutex );

    if ( g_vhBufferIDValid.find( buffer ) == g_vhBufferIDValid.end() )
    {
        // Invalid buffer handle
        return;
    }

    g_vhBufferIDValid.erase( buffer );
    g_vhBufferIDList.release( buffer );

    // Queue up command to destroy the buffer
    auto cmd = vhCmdAlloc<VIDL_vhDestroyBuffer>( buffer );
    assert( cmd );
    vhCmdEnqueue( cmd );
}

vhBuffer vhCreateVertexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    const vhVertexLayout layout,
    uint64_t numVerts,
    uint16_t flags
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return buffer;

    // Queue up command to create vertex buffer
    auto cmd = vhCmdAlloc<VIDL_vhCreateVertexBuffer>( buffer, name, data, layout, numVerts, flags );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return buffer;
}

void vhUpdateVertexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offsetVerts,
    uint64_t numVerts
)
{
    auto cmd = vhCmdAlloc<VIDL_vhUpdateVertexBuffer>( buffer, data, offsetVerts, numVerts );
    vhCmdEnqueue( cmd );
}

vhBuffer vhCreateIndexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t numIndices,
    uint16_t flags
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return buffer;

    auto cmd = vhCmdAlloc<VIDL_vhCreateIndexBuffer>( buffer, name, data, numIndices, flags );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return buffer;
}

void vhUpdateIndexBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offsetIndices,
    uint64_t numIndices
)
{
    auto cmd = vhCmdAlloc<VIDL_vhUpdateIndexBuffer>( buffer, data, offsetIndices, numIndices );
    vhCmdEnqueue( cmd );
}

vhBuffer vhCreateUniformBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size,
    uint16_t flags
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return buffer;

    auto cmd = vhCmdAlloc<VIDL_vhCreateUniformBuffer>( buffer, name, data, size, flags );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return buffer;
}

void vhUpdateUniformBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset,
    uint64_t size
)
{
    auto cmd = vhCmdAlloc<VIDL_vhUpdateUniformBuffer>( buffer, data, offset, size );
    vhCmdEnqueue( cmd );
}

vhBuffer vhCreateStorageBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size,
    uint16_t flags,
    uint32_t stride,
    nvrhi::Format format
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return buffer;

    auto cmd = vhCmdAlloc<VIDL_vhCreateStorageBuffer>( buffer, name, data, size, flags, stride, format );
    assert( cmd );
    vhCmdEnqueue( cmd );

    return buffer;
}

void vhUpdateStorageBuffer(
    vhBuffer buffer,
    const vhMem* data,
    uint64_t offset,
    uint64_t size
)
{
    auto cmd = vhCmdAlloc<VIDL_vhUpdateStorageBuffer>( buffer, data, offset, size );
    vhCmdEnqueue( cmd );
}

uint64_t vhGetBufferInfo( vhBuffer buffer, uint32_t* outStride, uint64_t* outFlags )
{
    return vhBackendQueryBufferInfo( buffer, outStride, outFlags );
}

nvrhi::BufferHandle vhGetBufferNvrhiHandle( vhBuffer buffer )
{
    return vhBackendQueryBufferHandle( buffer );
}

void vhTransientBuffer::Reset()
{
    size = 0;
    frameIdx = 0;
    offset = 0;
    ptr = nullptr;
}

int64_t vhTransientBuffer::Alloc( int64_t bytes )
{
    if ( offset + bytes > size )
        return -1;
    int64_t ret = offset;
    offset += bytes;
    return ret;
}

void vhTransientBuffer::Step()
{
    frameIdx = ( frameIdx + 1 ) % VRHI_MAX_FRAMES_INFLIGHT;
    offset = 0;
}

void vhTransientBuffer::Init_DeviceStateLocked( const nvrhi::BufferDesc& desc )
{
    // WARNING: Lock g_nvRHIStateMutex before calling this.
    Reset();
    size = desc.byteSize;
    for ( uint32_t i = 0; i < VRHI_MAX_FRAMES_INFLIGHT; i++ )
    {
        handle[i] = g_vhDevice->createBuffer( desc );
        assert( handle[i] != nullptr );
    }
}

uint8_t* vhTransientBuffer::Map_DeviceStateLocked()
{
    // WARNING: Lock g_nvRHIStateMutex before calling this.
    if ( ptr ) return ptr;
    ptr = ( uint8_t* ) g_vhDevice->mapBuffer( handle[frameIdx], nvrhi::CpuAccessMode::Write );
    return ptr;
}

void vhTransientBuffer::Unmap_DeviceStateLocked()
{
    // WARNING: Lock g_nvRHIStateMutex before calling this.
    if ( ptr ) g_vhDevice->unmapBuffer( handle[frameIdx] );
    ptr = nullptr;
}


void vhTransientBuffer::Shutdown_DeviceStateLocked()
{
    // WARNING: Lock g_nvRHIStateMutex before calling this.
    for ( uint32_t i = 0; i < VRHI_MAX_FRAMES_INFLIGHT; i++ )
    {
        handle[i] = nullptr;
    }
}

int64_t vhTransientBuffer::Write( const void* data, size_t bytes )
{
    // Alignment check
    if ( ( offset % VRHI_CBUF_ALIGN ) != 0 )
        return -1;

    // Allocation
    int64_t writeOffset = Alloc( bytes );
    if ( writeOffset < 0 )
        return -1;

    // Write to mapped buffer
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        uint8_t* mappedPtr = Map_DeviceStateLocked();
        if ( !mappedPtr )
        {
            VRHI_ERR("vhTransientBuffer::Write: Failed to map uniform buffer!\n");
            return -1;
        }
        memcpy( mappedPtr + writeOffset, data, bytes );
    }

    return writeOffset;
}

// --------------------------------------------------------------------------
// vhSubAllocator
// --------------------------------------------------------------------------

vhSubAllocator::~vhSubAllocator()
{
    if ( m_allocator )
    {
        delete static_cast< OffsetAllocator::Allocator* >( m_allocator );
        m_allocator = nullptr;
    }
}

void vhSubAllocator::Init( vhBuffer buffer, uint64_t size, uint32_t alignment )
{
    m_buffer = buffer;
    m_size = size;
    m_alignment = alignment;
    m_frameIndex = 0;
    
    // Clean up existing allocator if any
    if ( m_allocator )
    {
        delete static_cast< OffsetAllocator::Allocator* >( m_allocator );
        m_allocator = nullptr;
    }
    
    // Validate size fits in uint32_t for OffsetAllocator
    if ( size > UINT32_MAX )
    {
        VRHI_ERR( "vhSubAllocator::Init: Size %llu exceeds maximum supported size %u\n", size, UINT32_MAX );
        return;
    }
    
    // Create the underlying offset allocator with default max allocations (128K)
    m_allocator = new OffsetAllocator::Allocator( static_cast< uint32_t >( size ) );
    
    // Clear all state
    m_allocations.clear();
    for ( uint32_t i = 0; i < 3; i++ )
    {
        m_deferredFrees[i].clear();
    }
}

int64_t vhSubAllocator::Alloc( uint64_t size )
{
    if ( !m_allocator ) return -1;
    
    // Align size up to alignment boundary
    uint64_t alignedSize = ( size + m_alignment - 1 ) & ~( ( uint64_t ) m_alignment - 1 );
    
    // Validate aligned size fits in uint32_t for OffsetAllocator
    if ( alignedSize > UINT32_MAX )
    {
        VRHI_ERR( "vhSubAllocator::Alloc: Aligned size %llu exceeds maximum supported size %u\n", alignedSize, UINT32_MAX );
        return -1;
    }
    
    // Allocate from the underlying allocator
    OffsetAllocator::Allocation allocation = static_cast< OffsetAllocator::Allocator* >( m_allocator )->allocate( static_cast< uint32_t >( alignedSize ) );
    if ( allocation.offset == OffsetAllocator::Allocation::NO_SPACE )
    {
        return -1;
    }
    
    // Store the allocation metadata for later freeing
    m_allocations[allocation.offset] = allocation.metadata;
    
    return static_cast< int64_t >( allocation.offset );
}

void vhSubAllocator::Free( int64_t offset )
{
    if ( offset < 0 || !m_allocator ) return;
    
    uint64_t uoffset = static_cast< uint64_t >( offset );
    
    // Find the allocation metadata
    auto it = m_allocations.find( uoffset );
    if ( it == m_allocations.end() )
    {
        VRHI_ERR( "vhSubAllocator::Free: Attempted to free unknown offset %llu\n", uoffset );
        return;
    }
    
    // Add to deferred free queue for current frame
    DeferredFree deferred;
    deferred.offset = uoffset;
    deferred.metadata = it->second;
    m_deferredFrees[m_frameIndex].push_back( deferred );
    
    // Remove from active allocations map
    m_allocations.erase( it );
}

void vhSubAllocator::Step()
{
    // Advance frame index
    m_frameIndex = ( m_frameIndex + 1 ) % 3;
    
    // Process deferred frees from the frame that is now 3 frames old
    // (which is the new current frame index after increment)
    std::vector< DeferredFree >& freesToProcess = m_deferredFrees[m_frameIndex];
    
    for ( const DeferredFree& deferred : freesToProcess )
    {
        OffsetAllocator::Allocation allocation;
        allocation.offset = static_cast< uint32_t >( deferred.offset );
        allocation.metadata = deferred.metadata;
        static_cast< OffsetAllocator::Allocator* >( m_allocator )->free( allocation );
    }
    
    freesToProcess.clear();
}

void vhSubAllocator::Reset()
{
    if ( !m_allocator ) return;
    
    // Reset the underlying allocator
    static_cast< OffsetAllocator::Allocator* >( m_allocator )->reset();
    
    // Clear all state
    m_allocations.clear();
    for ( uint32_t i = 0; i < 3; i++ )
    {
        m_deferredFrees[i].clear();
    }
    
    m_frameIndex = 0;
}

uint64_t vhSubAllocator::GetUsedSpace() const
{
    if ( !m_allocator ) return 0;
    
    OffsetAllocator::StorageReport report = static_cast< OffsetAllocator::Allocator* >( m_allocator )->storageReport();
    return m_size - report.totalFreeSpace;
}

uint64_t vhSubAllocator::GetAvailableSpace() const
{
    if ( !m_allocator ) return 0;
    
    OffsetAllocator::StorageReport report = static_cast< OffsetAllocator::Allocator* >( m_allocator )->storageReport();
    return report.totalFreeSpace;
}


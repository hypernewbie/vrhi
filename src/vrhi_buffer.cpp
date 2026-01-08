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

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
#include <cctype>
#include <cstring>
#include <vector>
#include <string>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

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
    return Format::UNKNOWN;
}

// Parses a vertex layout string.
// Vertex layouts are defined as standard strings.
// Format: "TYPE[COUNT] [ATTRn]"
// Examples: 
//   "float3" -> Location 0 (Implicit)
//   "float3 ATTR5" -> Location 5 (Explicit)
//   "float3 float2" -> Loc 0, Loc 1
//
template < bool EMIT_OUTPUT >
bool vhParseVertexLayout( const vhVertexLayout& layout, std::vector<vhVertexLayoutDef>* outDefs )
{
    if constexpr ( EMIT_OUTPUT )
    {
        if ( outDefs ) outDefs->clear();
    }

    const char* ptr = layout.c_str();
    const char* baseTypes[] = { "float", "half", "int", "uint", "short", "ushort", nullptr };
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

        for ( const char** bt = baseTypes; *bt; ++bt )
        {
            std::string base = *bt;
            if ( typeStr.size() >= base.size() && typeStr.compare( 0, base.size(), base ) == 0 )
            {
                std::string suffix = typeStr.substr( base.size() );
                if ( suffix.empty() )
                {
                    typeValid = true;
                    baseType = base;
                    componentCount = 1;
                    break;
                }
                else if ( suffix.size() == 1 && suffix[0] >= '2' && suffix[0] <= '4' )
                {
                    typeValid = true;
                    baseType = base;
                    componentCount = suffix[0] - '0';
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

void vhCreateVertexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    const vhVertexLayout layout,
    uint64_t numVerts,
    uint16_t flags
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return;

    // Queue up command to create vertex buffer
    auto cmd = vhCmdAlloc<VIDL_vhCreateVertexBuffer>( buffer, name, data, layout, numVerts, flags );
    assert( cmd );
    vhCmdEnqueue( cmd );
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

void vhCreateIndexBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t numIndices,
    uint16_t flags
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return;

    auto cmd = vhCmdAlloc<VIDL_vhCreateIndexBuffer>( buffer, name, data, numIndices, flags );
    assert( cmd );
    vhCmdEnqueue( cmd );
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

void vhCreateUniformBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size,
    uint16_t flags
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return;

    auto cmd = vhCmdAlloc<VIDL_vhCreateUniformBuffer>( buffer, name, data, size, flags );
    assert( cmd );
    vhCmdEnqueue( cmd );
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

void vhCreateStorageBuffer(
    vhBuffer buffer,
    const char* name,
    const vhMem* data,
    uint64_t size,
    uint16_t flags,
    uint32_t stride,
    nvrhi::Format format
)
{
    if ( buffer == VRHI_INVALID_HANDLE ) return;

    auto cmd = vhCmdAlloc<VIDL_vhCreateStorageBuffer>( buffer, name, data, size, flags, stride, format );
    assert( cmd );
    vhCmdEnqueue( cmd );
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

void* vhGetBufferNvrhiHandle( vhBuffer buffer )
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


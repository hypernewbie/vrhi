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

struct vhBackendTexture
{
    std::string name;
    nvrhi::TextureHandle handle;
    vhTexInfo info;
    int64_t pitchSize;
    int64_t arraySize;
    std::vector< vhTextureMipInfo > mipInfo;
};

struct vhBackendBuffer
{
    std::string name;
    nvrhi::BufferHandle handle;
    nvrhi::BufferDesc desc;
    uint32_t stride = 0;
    uint64_t flags = 0;
};

// --------------------------------------------------------------------------
// Main Backend State
// --------------------------------------------------------------------------

// WARNING: Serious systems-level multithreading code ahead. Proceed with caution. Hard hats required.
// The backend thread is the thread that calls into NVRHI. It is the only thread that calls into NVRHI
// other than init / shutdown special case when backend thread isn't running.
//
// vhCmdBackendState is protected by backendMutex. It is the only place where backendMutex is used.
// This mutex guards access to vhCmdBackendState members. It does not guard the nvRHI state, which is g_nvRHIStateMutex.
// g_nvRHIStateMutex still needs to be locked when accessing the nvRHI state.

struct vhCmdBackendState : public VIDLHandler
{
    // --------------------------------------------------------------------------
    // Backend :: State Management Variables
    // --------------------------------------------------------------------------

    std::mutex backendMutex;
    char temps[1024];
    std::map< vhTexture, std::unique_ptr< vhBackendTexture > > backendTextures;
    std::map< vhBuffer, std::unique_ptr< vhBackendBuffer > > backendBuffers;

    // RAII for vhMem, takes ownership of the pointer and auto-destructs it.
    std::unique_ptr< vhMem > BE_MemRAII( const vhMem* mem )
    {
        return std::unique_ptr< vhMem >( const_cast< vhMem* >( mem ) );
    }

    // --------------------------------------------------------------------------
    // Backend :: Utils & Helpers
    // --------------------------------------------------------------------------

    // RAII helper for the VIDL_* commands. Helps with syntax.
    // Usage: BE_CmdRAII cmdRAII( cmd );
    template< typename T >
    struct BE_CmdRAII
    {
        T* ptr = nullptr;
        BE_CmdRAII( T* ptr ) : ptr( ptr ) {}
        ~BE_CmdRAII() { if ( ptr ) vhCmdRelease( ptr ); }

        // Non-copyable (prevents double-free)
        BE_CmdRAII( const BE_CmdRAII& ) = delete;
        BE_CmdRAII& operator=( const BE_CmdRAII& ) = delete;

        // Movable
        BE_CmdRAII( BE_CmdRAII&& other ) noexcept : ptr( other.ptr ) { other.ptr = nullptr; }
        BE_CmdRAII& operator=( BE_CmdRAII&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( ptr ) vhCmdRelease( ptr );
                ptr = other.ptr;
                other.ptr = nullptr;
            }
            return *this;
        }
    };

    // Deduction guide (usually implicit in C++17, but being explicit helps some compilers)
    template< typename T > BE_CmdRAII( T* ) -> BE_CmdRAII<T>; 
    

    // --------------------------------------------------------------------------
    // Backend :: Complex BE Low Level NVRHI Device Functions
    // --------------------------------------------------------------------------

    void BE_UpdateTexture( vhBackendTexture& btex, const vhMem* data, glm::ivec4 arrayMipUpdateRange = glm::ivec4( 0, INT_MAX, 0, INT_MAX ) )
    {
        if ( !btex.handle || !data || !data->size() ) return;
        auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );

        // Clamp to texture mip / array boundaries.
        int32_t mipStart = arrayMipUpdateRange.x, mipEnd = arrayMipUpdateRange.y;
        int32_t layerStart = arrayMipUpdateRange.z, layerEnd = arrayMipUpdateRange.w;
        mipStart = glm::clamp( mipStart, 0, btex.info.mipLevels );
        mipEnd = glm::clamp( mipEnd, 0, ( int32_t ) btex.info.mipLevels );
        layerStart = glm::clamp( layerStart, 0, btex.info.arrayLayers );
        layerEnd = glm::clamp( layerEnd, 0, ( int32_t ) btex.info.arrayLayers );
        assert( mipStart <= mipEnd );

        // Calculate layer size.
        int64_t totalLayerSize = 0;
        for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
        {
            totalLayerSize += btex.mipInfo[mip].size;
        }

        // Update the texture.
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            
            for ( int32_t layer = layerStart; layer < layerEnd; ++layer )
            {
                const uint8_t* layerSrcPtr = data->data() + ( size_t ) ( layer - layerStart ) * totalLayerSize;
                const auto& mipStartData = btex.mipInfo[mipStart];
                for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
                {
                    if ( mip >= ( int32_t ) btex.mipInfo.size() )
                        break;

                    const auto& mipData = btex.mipInfo[mip];
                    const uint8_t* srcMipPtr = layerSrcPtr + ( mipData.offset - mipStartData.offset );

                    cmdlist->writeTexture( btex.handle, layer, mip, srcMipPtr, mipData.pitch, mipData.slice_size );
                }
            }
        }
    }

    void BE_BlitTexture( vhBackendTexture& bdst, vhBackendTexture& bsrc, int dstMip, int srcMip, int dstLayer, int srcLayer, glm::ivec3 dstOffset, glm::ivec3 srcOffset, glm::ivec3 extent )
    {
        if ( !bdst.handle || !bsrc.handle ) return;

        // Higher level layers should already handle the validation.
        assert ( srcMip >= 0 && srcMip < bsrc.info.mipLevels );
        assert ( dstMip >= 0 && dstMip < bdst.info.mipLevels );
        assert ( srcLayer >= 0 && srcLayer < bsrc.info.arrayLayers );
        assert ( dstLayer >= 0 && dstLayer < bdst.info.arrayLayers );

        nvrhi::TextureSlice srcSlice;
        srcSlice.mipLevel = srcMip;
        srcSlice.arraySlice = srcLayer;
        srcSlice.x = srcOffset.x; srcSlice.y = srcOffset.y; srcSlice.z = srcOffset.z;
        srcSlice.width = extent.x; srcSlice.height = extent.y; srcSlice.depth = extent.z;

        nvrhi::TextureSlice dstSlice;
        dstSlice.mipLevel = dstMip;
        dstSlice.arraySlice = dstLayer;
        dstSlice.x = dstOffset.x; dstSlice.y = dstOffset.y; dstSlice.z = dstOffset.z;
        dstSlice.width = extent.x; dstSlice.height = extent.y; dstSlice.depth = extent.z;

        // Acquire command list and execute copy
        auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            cmdlist->copyTexture( bdst.handle, dstSlice, bsrc.handle, srcSlice );
        }
    }

    void BE_ReadTextureSlow( vhBackendTexture& btex, vhMem* outData, int mip, int layer )
    {
        if ( !btex.handle || !outData ) return;
        assert ( btex.info.target != nvrhi::TextureDimension::Texture3D );

        // Staging Texture
        auto desc = btex.handle->getDesc();
        desc.isVirtual = false;
        desc.isRenderTarget = false;
        desc.isUAV = false;
        desc.keepInitialState = true;
        desc.initialState = nvrhi::ResourceStates::CopyDest; 
        
        nvrhi::StagingTextureHandle stagingTex;
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            stagingTex = g_vhDevice->createStagingTexture( desc, nvrhi::CpuAccessMode::Read );
        }

        if ( !stagingTex ) return;

        // For this slow-path operation, just use Graphics queue for everything
        // (avoids complexity with transfer queue barriers)
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            
            nvrhi::CommandListParameters params = { .queueType = nvrhi::CommandQueue::Graphics };
            auto cmdList = g_vhDevice->createCommandList( params );
            cmdList->open();
            
            nvrhi::TextureSlice slice;
            slice.mipLevel = mip;
            slice.arraySlice = layer;

            // Make sure source is in CopySource state
            cmdList->setTextureState( btex.handle, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource );
            cmdList->commitBarriers();

            cmdList->copyTexture( stagingTex, slice, btex.handle, slice );
            
            cmdList->close();
            g_vhDevice->executeCommandList( cmdList, nvrhi::CommandQueue::Graphics );
            g_vhDevice->waitForIdle();
        }

        // CPU copy
        void* pData = nullptr;
        size_t rowPitch = 0;
        
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            nvrhi::TextureSlice slice;
            slice.mipLevel = mip;
            slice.arraySlice = layer;
            pData = g_vhDevice->mapStagingTexture( stagingTex, slice, nvrhi::CpuAccessMode::Read, &rowPitch );
        }

        if ( pData )
        {
            const auto& mipInfo = btex.mipInfo[mip];
            int height = mipInfo.dimensions.y;
            int expectedPitch = mipInfo.pitch;

            if ( outData->size() < ( size_t ) mipInfo.slice_size )
            {
                outData->resize( mipInfo.slice_size );
            }

            uint8_t* src = ( uint8_t* ) pData;
            uint8_t* dst = outData->data();

            for ( int y = 0; y < height; ++y )
            {
                memcpy( dst + ( size_t ) y * expectedPitch, src + ( size_t ) y * rowPitch, expectedPitch );
            }

            {
                std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
                g_vhDevice->unmapStagingTexture( stagingTex );
            }
        }
    }

    void BE_ResizeBuffer( vhBackendBuffer& bbuf, uint64_t size )
    {
        if ( !bbuf.handle ) return;

        auto oldHandle = bbuf.handle;
        auto oldSize = bbuf.desc.byteSize;

        bbuf.desc.setByteSize( size );
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            bbuf.handle = g_vhDevice->createBuffer( bbuf.desc );
        }

        if ( !bbuf.handle )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Failed to create bhandle!\n" );
            return;
        }

        auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
        cmdlist->copyBuffer( bbuf.handle, 0, oldHandle, 0, glm::min( bbuf.desc.byteSize, oldSize ) );
    }

    void BE_UpdateBuffer( vhBackendBuffer& bbuf, uint64_t offset, const vhMem* data )
    {
        if ( !bbuf.handle || !data || !data->size() ) return;

        if ( offset + data->size() > bbuf.desc.byteSize )
        {
            assert( bbuf.flags & VRHI_BUFFER_ALLOW_RESIZE );
            BE_ResizeBuffer( bbuf, offset + data->size() );
        }

        auto cmdlist = vhCmdListGet( nvrhi::CommandQueue::Graphics );
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            cmdlist->writeBuffer( bbuf.handle, data->data(), data->size(), offset );
        }
    }

public:
    void init()
    {
        std::lock_guard< std::mutex > lock( backendMutex );
    }

    void shutdown()
    {
        std::lock_guard< std::mutex > lock( backendMutex );
        std::lock_guard< std::mutex > lock2( g_nvRHIStateMutex );
        backendTextures.clear();
    }

    // --------------------------------------------------------------------------
    // Backend :: VIDL Command Handlers
    // --------------------------------------------------------------------------

    void Handle_vhResetTexture( VIDL_vhResetTexture* cmd ) override
    {
        if ( cmd->texture == VRHI_INVALID_HANDLE )
        {
            vhCmdRelease( cmd );
            return;
        }

        // Ensure entry exists to make subsequent Destroy/Update safe
        if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
        {
            backendTextures[ cmd->texture ] = std::make_unique<vhBackendTexture>();
        }

        vhCmdRelease( cmd );
    }

    void Handle_vhDestroyTexture( VIDL_vhDestroyTexture* cmd ) override
    {
        if ( cmd->texture == VRHI_INVALID_HANDLE )
        {
            vhCmdRelease( cmd );
            return;
        }

        if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
        {
            VRHI_ERR( "vhDestroyTexture() : Texture %d not found!\n", cmd->texture );
            vhCmdRelease( cmd );
            return;
        }

        // Destroy texture by releasing our reference. NVRHI handles GPU destruction safety.
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            backendTextures.erase( cmd->texture );
        }

        vhCmdRelease( cmd );
    }

    void Handle_vhCreateTexture( VIDL_vhCreateTexture* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        if ( cmd->texture == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhCreateTexture() : Invalid texture handle!\n" );
            return;
        }
        if ( cmd->dimensions.x <= 0 || cmd->dimensions.y <= 0 || cmd->dimensions.z <= 0 ||
            cmd->numMips == 0 || cmd->numLayers <= 0 || cmd->format == nvrhi::Format::UNKNOWN )
        {
            VRHI_ERR( "vhCreateTexture() : Invalid parameters! TexID %u %d x %d x %d mips %d layers %d format %d\n",
                cmd->texture, cmd->dimensions.x, cmd->dimensions.y, cmd->dimensions.z, cmd->numMips, cmd->numLayers, cmd->format );
            return;
        }

        // Create the NVRHI texture.
        snprintf( temps, sizeof(temps), "Texture %d\n", cmd->texture );
        auto textureDesc = nvrhi::TextureDesc()
            .setDimension( cmd->target )
            .setWidth( cmd->dimensions.x )
            .setHeight( cmd->dimensions.y )
            .setDepth( cmd->dimensions.z )
            .setFormat( cmd->format )
            .setMipLevels( cmd->numMips )
            .setArraySize( cmd->numLayers )
            .enableAutomaticStateTracking( nvrhi::ResourceStates::ShaderResource )
            .setDebugName( temps );

        nvrhi::TextureHandle texture = nullptr;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            texture = g_vhDevice->createTexture( textureDesc );
        }
        if ( !texture )
        {
            VRHI_ERR( "vhCreateTexture() : Failed to create texture!\n" );
            return;
        }

        // Calculate metadata for the texture.
        auto btex = std::make_unique< vhBackendTexture >();
        btex->handle = texture;
        btex->name = temps;
        btex->info.target = cmd->target;
        btex->info.dimensions = cmd->dimensions;
        btex->info.format = cmd->format;
        btex->info.mipLevels = cmd->numMips;
        btex->info.arrayLayers = cmd->numLayers;
        vhTextureMiplevelInfo( btex->mipInfo, btex->pitchSize, btex->arraySize, btex->info );

        if ( cmd->data )
        {
            BE_UpdateTexture( *btex, cmd->data );
        }

        backendTextures[ cmd->texture ] = std::move( btex );
    }

    void Handle_vhUpdateTexture( VIDL_vhUpdateTexture* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );
    
        if ( cmd->texture == VRHI_INVALID_HANDLE )
        {
            return;
        }

        auto it = backendTextures.find( cmd->texture );
        if ( it == backendTextures.end() )
        {
            VRHI_ERR( "vhUpdateTexture() : Texture %d not found!\n", cmd->texture );
            return;
        }
        auto& btex = *it->second;

        // Calculate expected data size for the range.
        int32_t mipStart = cmd->startMips, mipEnd = cmd->startMips + cmd->numMips;
        int32_t layerStart = cmd->startLayers, layerEnd = cmd->startLayers + cmd->numLayers;
        
        // Validation: range must be within texture limits.
        if ( mipStart < 0 || mipEnd > ( int32_t ) btex.info.mipLevels ||
             layerStart < 0 || layerEnd > ( int32_t ) btex.info.arrayLayers )
        {
            VRHI_ERR( "vhUpdateTexture(): Update range out of bounds.\n" );
            return;
        }

        // Calculate total size of mips in the range for one layer.
        int64_t totalLayerSize = 0;
        for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
        {
            totalLayerSize += btex.mipInfo[mip].size;
        }
        
        int64_t expectedSize = totalLayerSize * cmd->numLayers;
        if ( ( int64_t ) cmd->data->size() < expectedSize )
        {
            VRHI_ERR( "vhUpdateTexture(): Data size %llu is too small for update range, expected %llu\n", ( uint64_t ) cmd->data->size(), ( uint64_t ) expectedSize );
            return;
        }

        glm::ivec4 range = glm::ivec4( cmd->startMips, cmd->startMips + cmd->numMips, cmd->startLayers, cmd->startLayers + cmd->numLayers );
        BE_UpdateTexture( btex, cmd->data, range );
    }

    void Handle_vhReadTextureSlow( VIDL_vhReadTextureSlow* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        // NO dataRAII here - outData is owned by the caller.

        if ( cmd->texture == VRHI_INVALID_HANDLE )
        {
            return;
        }

        if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
        {
            VRHI_ERR( "vhReadTextureSlow() : Texture %d not found!\n", cmd->texture );
            return;
        }

        auto& btex = *backendTextures[ cmd->texture ];
        if ( btex.info.target == nvrhi::TextureDimension::Texture3D )
        {
            VRHI_ERR( "vhReadTextureSlow() : 3D textures are not supported for readback yet!\n" );
            return;
        }

        BE_ReadTextureSlow( btex, cmd->outData, cmd->mip, cmd->layer );
    }

    void Handle_vhBlitTexture( VIDL_vhBlitTexture* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );

        if ( cmd->dst == VRHI_INVALID_HANDLE || cmd->src == VRHI_INVALID_HANDLE )
        {
            return;
        }

        auto itDst = backendTextures.find( cmd->dst );
        auto itSrc = backendTextures.find( cmd->src );
        if ( itDst == backendTextures.end() || itSrc == backendTextures.end() )
        {
            VRHI_ERR( "vhBlitTexture() : Texture handle(s) %d or %d not found!\n", cmd->dst, cmd->src );
            return;
        }
        auto& bdst = *itDst->second;
        auto& bsrc = *itSrc->second;

        glm::ivec3 extent = cmd->extent;
        if ( extent.x <= 0 || extent.y <= 0 )
        {
            if ( cmd->srcMip >= 0 && cmd->srcMip < ( int ) bsrc.mipInfo.size() )
                extent = bsrc.mipInfo[cmd->srcMip].dimensions;
        }

        if ( cmd->srcMip < 0 || cmd->srcMip >= ( int ) bsrc.mipInfo.size() )
        {
            VRHI_ERR( "vhBlitTexture: srcMip %d out of range (0..%d)\n", cmd->srcMip, ( int ) bsrc.mipInfo.size() - 1 );
            return;
        }
        if ( !vhVerifyRegionInTexture( vhGetFormat( bsrc.info.format ), bsrc.mipInfo[cmd->srcMip].dimensions, cmd->srcOffset, extent, "vhBlitTexture Source" ) )
        {
            return;
        }

        if ( cmd->dstMip < 0 || cmd->dstMip >= ( int ) bdst.mipInfo.size() )
        {
            VRHI_ERR( "vhBlitTexture: dstMip %d out of range (0..%d)\n", cmd->dstMip, ( int ) bdst.mipInfo.size() - 1 );
            return;
        }
        if ( !vhVerifyRegionInTexture( vhGetFormat( bdst.info.format ), bdst.mipInfo[cmd->dstMip].dimensions, cmd->dstOffset, extent, "vhBlitTexture Dest" ) )
        {
            return;
        }

        BE_BlitTexture( bdst, bsrc, cmd->dstMip, cmd->srcMip, cmd->dstLayer, cmd->srcLayer, cmd->dstOffset, cmd->srcOffset, extent );
    }

    void Handle_vhResetBuffer( VIDL_vhResetBuffer* cmd ) override
    {
        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            vhCmdRelease( cmd );
            return;
        }

        // Ensure entry exists to make subsequent Destroy/Update safe
        if ( backendBuffers.find( cmd->buffer ) == backendBuffers.end() )
        {
            backendBuffers[ cmd->buffer ] = std::make_unique<vhBackendBuffer>();
        }

        vhCmdRelease( cmd );
    }

    void Handle_vhCreateBufferCommon_Internal( const char* fn, vhBuffer buffer, nvrhi::BufferDesc& desc, const char* name, const char* autoname,
        const vhMem* data, uint64_t count, uint64_t stride, uint64_t flags )
    {
        if ( buffer == VRHI_INVALID_HANDLE ) return;

        if ( backendBuffers.find( buffer ) != backendBuffers.end() && backendBuffers[buffer]->handle )
        {
            VRHI_ERR( "%s() : Buffer %d already exists!\n", fn, buffer );
            return;
        }

        uint64_t byteSize = 0;
        if ( data )
        {
            byteSize = data->size();
        }
        else
        {
            if ( count == 0 )
            {
                VRHI_ERR( "%s() : Memory bhandle is empty/null AND count is 0!\n", fn );
                return;
            }
            byteSize = count * stride;
        }

        // Create the NVRHI bhandle
        if ( !name || !name[0] ) snprintf( temps, sizeof(temps), "%s %d", autoname, buffer );
        auto bufferDesc = desc
            .setByteSize( byteSize )
            .setCanHaveUAVs( flags & VRHI_BUFFER_COMPUTE_WRITE )
            .setCanHaveTypedViews( flags & VRHI_BUFFER_COMPUTE_READ )
            .setCanHaveRawViews( flags & VRHI_BUFFER_COMPUTE_READ )
            .setIsDrawIndirectArgs( flags & VRHI_BUFFER_DRAW_INDIRECT )
            .setDebugName( (name && name[0]) ? name : temps );

        nvrhi::BufferHandle bhandle = nullptr;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            bhandle = g_vhDevice->createBuffer( bufferDesc );
        }

        if ( !bhandle )
        {
            VRHI_ERR( "%s() : Failed to create bhandle!\n", fn );
            return;
        }

        auto bbuf = std::make_unique< vhBackendBuffer >();
        bbuf->handle = bhandle;
        bbuf->name = ( name && name[0] ) ? name : temps;
        bbuf->desc = bufferDesc;
        bbuf->stride = ( uint32_t ) stride;
        bbuf->flags = flags;

        if ( data )
        {
            BE_UpdateBuffer( *bbuf, 0, data );
        }

        backendBuffers[ buffer ] = std::move( bbuf );
    }

    void Handle_vhUpdateBufferCommon_Internal( const char* fn, vhBuffer buffer, uint64_t offsetElements, const vhMem* data, uint64_t count, bool isVertexBuffer )
    {
        if ( buffer == VRHI_INVALID_HANDLE ) return;

        if ( backendBuffers.find( buffer ) == backendBuffers.end() )
        {
            VRHI_ERR( "%s() : Buffer %d not found!\n", fn, buffer );
            return;
        }
        auto& bbuf = backendBuffers[ buffer ];

        // Convert element offset to byte offset
        uint64_t byteOffset = 0;
        if ( isVertexBuffer )
        {
            byteOffset = offsetElements * bbuf->stride;
        }
        else
        {
            // Index buffer - determine index size from flags
            uint64_t indexSize = (bbuf->flags & VRHI_BUFFER_INDEX32) ? sizeof(uint32_t) : sizeof(uint16_t);
            byteOffset = offsetElements * indexSize;
        }

        if ( data )
        {
            if ( byteOffset + data->size() > bbuf->desc.byteSize && !( bbuf->flags & VRHI_BUFFER_ALLOW_RESIZE ) )
            {
                VRHI_ERR( "%s() : Update range [%llu, %llu] exceeds buffer size %llu!\n", 
                    fn, byteOffset, byteOffset + data->size(), bbuf->desc.byteSize );
                return;
            }
            BE_UpdateBuffer( *bbuf, byteOffset, data );
        }
        else if ( count > 0 )
        {
            if ( !( bbuf->flags & VRHI_BUFFER_ALLOW_RESIZE ) )
            {
                VRHI_ERR( "%s() : resize requested but buffer does not have ALLOW_RESIZE flag!\n", fn );
                return;
            }
            BE_ResizeBuffer( *bbuf, count * bbuf->stride );
        }
        else
        {
            VRHI_ERR( "%s() : Both data and count are null/zero.\n", fn );
        }
    }

    void Handle_vhCreateVertexBuffer( VIDL_vhCreateVertexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Invalid bhandle handle!\n" );
            return;
        }

        std::vector< vhVertexLayoutDef > layoutDefs;
        if ( !vhParseVertexLayoutInternal( cmd->layout, layoutDefs ) )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Invalid vertex layout!\n" );
            return;
        }
        uint32_t stride = ( uint32_t ) vhVertexLayoutDefSize( layoutDefs );
        if ( stride == 0 )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Vertex layout has 0 size!\n" );
            return;
        }

        // Partially initialise nvrhi::BufferDesc
        nvrhi::BufferDesc desc; 
        desc.setIsVertexBuffer( true );
        desc.enableAutomaticStateTracking( nvrhi::ResourceStates::VertexBuffer );

        Handle_vhCreateBufferCommon_Internal( "vhCreateVertexBuffer", cmd->buffer, desc, cmd->name, "VertexBuffer", cmd->data, cmd->numVerts, stride, cmd->flags );
    }

    void Handle_vhUpdateVertexBuffer( VIDL_vhUpdateVertexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );
        
        Handle_vhUpdateBufferCommon_Internal( "vhUpdateVertexBuffer", cmd->buffer, cmd->offsetVerts, cmd->data, cmd->numVerts, true );
    }

    void Handle_vhCreateIndexBuffer( VIDL_vhCreateIndexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );
        
        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhCreateIndexBuffer() : Invalid bhandle handle!\n" );
            return;
        }

        // Partially initialise nvrhi::BufferDesc
        nvrhi::BufferDesc desc; 
        desc.setIsIndexBuffer( true );
        desc.enableAutomaticStateTracking( nvrhi::ResourceStates::IndexBuffer );
        uint64_t stride = cmd->flags & VRHI_BUFFER_INDEX32 ? sizeof( uint32_t ) : sizeof( uint16_t );

        Handle_vhCreateBufferCommon_Internal( "vhCreateIndexBuffer", cmd->buffer, desc, cmd->name, "IndexBuffer", cmd->data, cmd->numIndices, stride, cmd->flags );
    }

    void Handle_vhUpdateIndexBuffer( VIDL_vhUpdateIndexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );
        
        Handle_vhUpdateBufferCommon_Internal( "vhUpdateIndexBuffer", cmd->buffer, cmd->offsetIndices, cmd->data, cmd->numIndices, false );
    }

    void Handle_vhCreateUniformBuffer( VIDL_vhCreateUniformBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhCreateUniformBuffer() : Invalid buffer handle!\n" );
            return;
        }

        // Partially initialize nvrhi::BufferDesc for uniform buffer
        nvrhi::BufferDesc desc;
        desc.setIsConstantBuffer( true );
        desc.enableAutomaticStateTracking( nvrhi::ResourceStates::ConstantBuffer );

        // Reuse common logic with stride = 1 (byte-oriented)
        Handle_vhCreateBufferCommon_Internal( "vhCreateUniformBuffer", cmd->buffer, desc, cmd->name, "UniformBuffer", cmd->data, cmd->size, 1, cmd->flags );
    }

    void Handle_vhUpdateUniformBuffer( VIDL_vhUpdateUniformBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        // Reuse common update logic (isVertexBuffer = true uses stride from creation)
        Handle_vhUpdateBufferCommon_Internal( "vhUpdateUniformBuffer", cmd->buffer, cmd->offset, cmd->data, cmd->size, true );
    }

    void Handle_vhCreateStorageBuffer( VIDL_vhCreateStorageBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhCreateStorageBuffer() : Invalid buffer handle!\n" );
            return;
        }

        // Partially initialize nvrhi::BufferDesc for storage buffer
        nvrhi::BufferDesc desc;
        desc.setByteSize( cmd->size );
        desc.setCanHaveUAVs( true );
        desc.setCanHaveRawViews( true );
        desc.enableAutomaticStateTracking( nvrhi::ResourceStates::UnorderedAccess );

        // Reuse common logic with stride = 1 (byte-oriented)
        Handle_vhCreateBufferCommon_Internal( "vhCreateStorageBuffer", cmd->buffer, desc, cmd->name, "StorageBuffer", cmd->data, cmd->size, 1, cmd->flags );
    }

    void Handle_vhUpdateStorageBuffer( VIDL_vhUpdateStorageBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        // Reuse common update logic (isVertexBuffer = true uses stride from creation)
        Handle_vhUpdateBufferCommon_Internal( "vhUpdateStorageBuffer", cmd->buffer, cmd->offset, cmd->data, cmd->size, true );
    }

    void Handle_vhDestroyBuffer( VIDL_vhDestroyBuffer* cmd ) override
    {
        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            vhCmdRelease( cmd );
            return;
        }

        if ( backendBuffers.find( cmd->buffer ) == backendBuffers.end() )
        {
            VRHI_ERR( "vhDestroyBuffer() : Buffer %d not found!\n", cmd->buffer );
            vhCmdRelease( cmd );
            return;
        }

        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            backendBuffers.erase( cmd->buffer );
        }
        
        vhCmdRelease( cmd );
    }

    void Handle_vhFlushInternal( VIDL_vhFlushInternal* cmd ) override
    {
        // Free all cmd memory allocations, because hitting this flush means all previous commands have been processed.
        {
            std::lock_guard<std::mutex> lock( g_vhMemListMutex );
            g_vhMemList.clear();
        }

        if ( cmd->waitForGPU )
        {
            // This uses g_nvRHIStateMutex then gives it up, we need to avoid double-locking.
            vhCmdListFlushAll(); 
        }

        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex ); 
            if ( cmd->waitForGPU )
            {
                g_vhDevice->waitForIdle();
            }
            g_vhDevice->runGarbageCollection();
        }

        // Notify caller that we're done.
        // Safety warning : fence is probably from stack of caller
        if ( cmd->fence )
            cmd->fence->store( true );

        vhCmdRelease( cmd );
    }

    // --------------------------------------------------------------------------
    // Backend :: RHIThreadEntry
    // --------------------------------------------------------------------------

    void RHIThreadEntry( std::function<void()> initCallback )
    {
        VRHI_LOG( "    RHI Thread started.\n" );
        g_vhCmdThreadReady = true;
        if ( initCallback ) initCallback();

        while ( !g_vhCmdsQuit )
        {
            void* cmd = nullptr;
            if ( !g_vhCmds.try_dequeue( cmd ) )
            {
                // Block until there is a command to process
                if ( ! g_vhCmds.wait_dequeue_timed( cmd, std::chrono::milliseconds( 8 ) ) )
                    continue;
            }
            if ( cmd != nullptr )
            {
                std::lock_guard< std::mutex > lock( backendMutex );
                HandleCmd( cmd );
            }
        }

        VRHI_LOG( "    RHI Thread exiting.\n" );
    }
};

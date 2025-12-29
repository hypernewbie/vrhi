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
};

// Main backend thread state.
//
// WARNING: Serious systems-level multithreading code ahead. Proceed with caution. Hard hats required.
// The backend thread is the thread that calls into NVRHI. It is the only thread that calls into NVRHI
// other than init / shutdown special case when backend thread isn't running.
//
// vhCmdBackendState is protected by backendMutex. It is the only place where backendMutex is used.
// This mutex guards access to vhCmdBackendState members. It does not guard the nvRHI state, which is g_nvRHIStateMutex.
// g_nvRHIStateMutex still needs to be locked when accessing the nvRHI state.
//
struct vhCmdBackendState : public VIDLHandler
{
    std::mutex backendMutex;
    char temps[1024];
    std::map< vhTexture, std::unique_ptr< vhBackendTexture > > backendTextures;
    std::map< vhBuffer, std::unique_ptr< vhBackendBuffer > > backendBuffers;

    // RAII for vhMem, takes ownership of the pointer and auto-destructs it.
    std::unique_ptr< vhMem > BE_MemRAII( const vhMem* mem )
    {
        return std::unique_ptr< vhMem >( const_cast< vhMem* >( mem ) );
    }

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
    template< typename T > BE_CmdRAII( T* ) -> BE_CmdRAII<T>; // Deduction guide (usually implicit in C++17, but being explicit helps some compilers)

    void BE_UpdateTexture( vhBackendTexture& btex, const vhMem* data, nvrhi::CommandQueue queueType, glm::ivec4 arrayMipUpdateRange = glm::ivec4( 0, INT_MAX, 0, INT_MAX ), glm::ivec3 offset = glm::ivec3( 0 ), glm::ivec3 extent = glm::ivec3( -1 ) )
    {
        if ( !btex.handle || !data || !data->size() ) return;

        /* Partial region update logic (suggestions):
           if (extent.x == -1) extent.x = mipDim.x - offset.x;
           if (extent.y == -1) extent.y = mipDim.y - offset.y;
           if (extent.z == -1) extent.z = mipDim.z - offset.z;
           validate: offset + extent <= mipDim
           srcPtr calculation based on region pitch and region offset within user data...
        */

        /* Data size validation disabled temporarily for partial update implementation task.
        if ( data->size() != btex.arraySize * btex.info.arrayLayers )
        {
            VRHI_ERR( "vhUpdateTexture() : Data size %llu does not match texture size %llu!\n", ( uint64_t ) data->size(), btex.arraySize * btex.info.arrayLayers );
            return;
        }
        */

        auto cmdlist = vhCmdListGet( queueType );

        // Clamp the update range to valid values.
        int32_t mipStart = arrayMipUpdateRange.x, mipEnd = arrayMipUpdateRange.y;
        int32_t layerStart = arrayMipUpdateRange.z, layerEnd = arrayMipUpdateRange.w;
        mipStart = glm::clamp( mipStart, 0, btex.info.mipLevels );
        mipEnd = glm::clamp( mipEnd, 0, btex.info.mipLevels );
        layerStart = glm::clamp( layerStart, 0, btex.info.arrayLayers );
        layerEnd = glm::clamp( layerEnd, 0, btex.info.arrayLayers );

        // Update the texture.
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            
            // Disable auto barriers on Copy queue - transfer-only queues can't do shader barriers
            if ( queueType == nvrhi::CommandQueue::Copy )
            {
                cmdlist->setEnableAutomaticBarriers( false );
            }
            
            for ( int32_t layer = layerStart; layer < layerEnd; ++layer )
            {
                const uint8_t* srcPtr = data->data() + ( size_t ) layer * btex.arraySize;
                for ( int32_t mip = mipStart; mip < mipEnd; ++mip )
                {
                    if ( mip >= ( int32_t ) btex.mipInfo.size() )
                        break;

                    const auto& mipData = btex.mipInfo[mip];
                    size_t depthPitch = ( btex.info.target == nvrhi::TextureDimension::Texture3D ) ? mipData.slice_size : 0;
                    cmdlist->writeTexture( btex.handle, layer, mip, srcPtr + mipData.offset, mipData.pitch, depthPitch );
                }
            }
            
            // Re-enable for next operations on this command list
            if ( queueType == nvrhi::CommandQueue::Copy )
            {
                cmdlist->setEnableAutomaticBarriers( true );
            }
        }

        if ( queueType == nvrhi::CommandQueue::Copy )
        {
            g_vhCmdListTransferSizeHeuristic += data->size();
            vhCmdListFlushTransferIfNeeded();
        }
    }

    void BE_BlitTexture( vhBackendTexture& bdst, vhBackendTexture& bsrc, int dstMip, int srcMip, int dstLayer, int srcLayer, glm::ivec3 dstOffset, glm::ivec3 srcOffset, glm::ivec3 extent )
    {
        if ( !bdst.handle || !bsrc.handle ) return;

        VRHI_LOG( "BE_BlitTexture Stub - Not Yet Implemented\n" );
        VRHI_LOG( "BE_BlitTexture stub called: %s -> %s\n", bsrc.name.c_str(), bdst.name.c_str() );

        /* Implementation Suggestions:
        
        // 1. Parameter Validation
        if (srcMip < 0 || srcMip >= bsrc.info.mipLevels || dstMip < 0 || dstMip >= bdst.info.mipLevels) return;
        if (srcLayer < 0 || srcLayer >= bsrc.info.arrayLayers || dstLayer < 0 || dstLayer >= bdst.info.arrayLayers) return;

        // 2. Default extent calculation
        if (extent.x <= 0 || extent.y <= 0) {
            extent = bsrc.mipInfo[srcMip].dimensions;
        }

        // 3. Coordinate clamping
        // Ensure offsets + extent fit within respective textures

        // 4. Setup NVRHI Texture Slices
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

        // 5. State Transitions
        // Transition src to CopySource, dst to CopyDest

        // 6. Execute Blit
        // Use cmdlist->copyTexture or a scaling blit if dimensions differ
        
        */
    }

    void BE_ReadTextureSlow( vhBackendTexture& btex, vhMem* outData, int mip, int layer )
    {
        if ( !btex.handle || !outData ) return;

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
            stagingTex = g_vhDevice->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
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

    void BE_UpdateBuffer( vhBackendBuffer& bbuf, nvrhi::CommandQueue queueType, uint64_t offset, const vhMem* data )
    {
        if ( !bbuf.handle || !data || !data->size() ) return;
        printf("BE_UpdateBuffer Dummy Implementation\n");
        
        /* Commented out implementation as per request:
        if ( offset + data->size() > bbuf.desc.byteSize )
        {
            VRHI_ERR( "vhUpdateVertexBuffer() : Update range [%llu, %llu] exceeds buffer size %llu!\n", 
                offset, offset + data->size(), bbuf.desc.byteSize );
            return;
        }

        auto cmdlist = vhCmdListGet( queueType );
        {
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            cmdlist->writeBuffer( bbuf.handle, data->data(), data->size(), offset );
        }

        if ( queueType == nvrhi::CommandQueue::Copy )
        {
            g_vhCmdListTransferSizeHeuristic += data->size();
            vhCmdListFlushTransferIfNeeded();
        }
        */
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
            .setInitialState( nvrhi::ResourceStates::Common )
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
        auto btex = std::make_unique<vhBackendTexture>();
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
            // Texture creation just uses graphics queue for simplicity, as these happen rarely.
            BE_UpdateTexture( *btex, cmd->data, nvrhi::CommandQueue::Graphics );
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

        glm::ivec3 extent = cmd->extent;
        if ( cmd->numMips == 1 )
        {
            if ( extent.x == -1 ) extent.x = btex.mipInfo[cmd->startMips].dimensions.x - cmd->offset.x;
            if ( extent.y == -1 ) extent.y = btex.mipInfo[cmd->startMips].dimensions.y - cmd->offset.y;
            if ( extent.z == -1 ) extent.z = btex.mipInfo[cmd->startMips].dimensions.z - cmd->offset.z;

            if ( !vhVerifyRegionInTexture( vhGetFormat( btex.info.format ), btex.mipInfo[cmd->startMips].dimensions, cmd->offset, extent, "vhUpdateTexture" ) )
            {
                return;
            }

            int64_t expectedSize = vhGetRegionDataSize( vhGetFormat( btex.info.format ), extent, cmd->startMips ) * cmd->numLayers;
            if ( ( int64_t ) cmd->data->size() < expectedSize )
            {
                VRHI_ERR( "vhUpdateTexture(): Data size %llu is too small for region [%d, %d, %d], expected %llu\n", ( uint64_t ) cmd->data->size(), extent.x, extent.y, extent.z, ( uint64_t ) expectedSize );
                return;
            }
        }
        else
        {
            if ( cmd->offset != glm::ivec3( 0 ) || cmd->extent != glm::ivec3( -1 ) )
            {
                VRHI_ERR( "vhUpdateTexture(): Partial region updates are only supported for single mip updates.\n" );
                return;
            }
        }

        glm::ivec4 range = glm::ivec4( cmd->startMips, cmd->startMips + cmd->numMips, cmd->startLayers, cmd->startLayers + cmd->numLayers );
        BE_UpdateTexture( btex, cmd->data, nvrhi::CommandQueue::Graphics, range, cmd->offset, extent );
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

        BE_ReadTextureSlow( *backendTextures[ cmd->texture ], cmd->outData, cmd->mip, cmd->layer );
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

    void Handle_vhCreateVertexBuffer( VIDL_vhCreateVertexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto memRAII = BE_MemRAII( cmd->mem );

        if ( cmd->buffer == VRHI_INVALID_HANDLE )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Invalid buffer handle!\n" );
            return;
        }
        
        std::vector< vhVertexLayoutDef > layoutDefs;
        if ( !vhParseVertexLayoutInternal( cmd->layout, layoutDefs ) )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Invalid vertex layout!\n" );
            return;
        }
        size_t layoutDefSize = vhVertexLayoutDefSize(layoutDefs);
        if ( layoutDefSize == 0 )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Vertex layout has 0 size!\n" );
            return;
        }
        
        /*
        struct VIDL_vhCreateVertexBuffer
{
    static constexpr uint64_t kMagic = 0xBBF8D184;
    uint64_t MAGIC = kMagic;
    vhBuffer buffer;
    const char* name;
    const vhMem* mem;
    const vhVertexLayout layout;
    uint16_t flags = VRHI_BUFFER_NONE;*/

        
        
        // Parse layout
        // NOTE: We allow overwriting existing entry from Reset
        if ( backendBuffers.find( cmd->buffer ) != backendBuffers.end() && backendBuffers[cmd->buffer]->handle )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Buffer %d already exists!\n", cmd->buffer );
            return;
        }

        // Validate memory presence for static buffers (for now we assume all are static/initialized)
        if ( !cmd->mem || cmd->mem->empty() )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Memory buffer is empty or null, cannot determine size!\n" );
            return;
        }
        uint64_t byteSize = cmd->mem->size();

        // Create the NVRHI buffer
        if ( !cmd->name || !cmd->name[0] ) snprintf( temps, sizeof(temps), "VertexBuffer %d", cmd->buffer );
        auto bufferDesc = nvrhi::BufferDesc()
            .setByteSize( byteSize )
            .setIsVertexBuffer( true )
            .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer)
            .setDebugName( (cmd->name && cmd->name[0]) ? cmd->name : temps );

        nvrhi::BufferHandle buffer = nullptr;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            buffer = g_vhDevice->createBuffer( bufferDesc );
        }

        if ( !buffer )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Failed to create buffer!\n" );
            return;
        }

        auto bbuf = std::make_unique<vhBackendBuffer>();
        bbuf->handle = buffer;
        bbuf->name = (cmd->name && cmd->name[0]) ? cmd->name : temps;
        bbuf->desc = bufferDesc;

        // TODO: Upload initial data if cmd->mem is provided
        if ( cmd->mem )
        {
            BE_UpdateBuffer( *bbuf, nvrhi::CommandQueue::Graphics, 0, cmd->mem );
        }

        backendBuffers[ cmd->buffer ] = std::move( bbuf );
    }

    void Handle_vhUpdateVertexBuffer( VIDL_vhUpdateVertexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->data );

        if ( cmd->buffer == VRHI_INVALID_HANDLE ) return;

        if ( backendBuffers.find( cmd->buffer ) == backendBuffers.end() )
        {
            VRHI_ERR( "vhUpdateVertexBuffer() : Buffer %d not found!\n", cmd->buffer );
            return;
        }

        BE_UpdateBuffer( *backendBuffers[ cmd->buffer ], nvrhi::CommandQueue::Graphics, cmd->offset, cmd->data );
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

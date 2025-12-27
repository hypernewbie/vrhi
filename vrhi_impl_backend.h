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

    void BE_UpdateTexture( vhBackendTexture& btex, const vhMem* data, nvrhi::CommandQueue queueType, glm::ivec4 arrayMipUpdateRange = glm::ivec4( 0, INT_MAX, 0, INT_MAX ) )
    {
        if ( !btex.handle || !data || !data->size() ) return;
        if ( data->size() != btex.arraySize * btex.info.arrayLayers )
        {
            VRHI_ERR( "vhUpdateTexture() : Data size %llu does not match texture size %llu!\n", ( uint64_t ) data->size(), btex.arraySize * btex.info.arrayLayers );
            return;
        }

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

    void Handle_vhDestroyTexture( VIDL_vhDestroyTexture* cmd ) override
    {
        if ( !cmd->texture )
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

        if ( cmd->texture == VRHI_INVALID_HANDLE ||
            cmd->dimensions.x <= 0 || cmd->dimensions.y <= 0 || cmd->dimensions.z <= 0 ||
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
            // Continual per-frame updates can use transfer queue.
            BE_UpdateTexture( *btex, cmd->data, nvrhi::CommandQueue::Graphics );
        }

        backendTextures[ cmd->texture ] = std::move( btex );
    }

    void Handle_vhUpdateTexture( VIDL_vhUpdateTexture* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto dataRAII = BE_MemRAII( cmd->fullImageData );
    
        if ( !cmd->texture )
        {
            return;
        }

        if ( backendTextures.find( cmd->texture ) == backendTextures.end() )
        {
            VRHI_ERR( "vhUpdateTexture() : Texture %d not found!\n", cmd->texture );
            return;
        }

        glm::ivec4 range = glm::ivec4( cmd->startMips, cmd->startMips + cmd->numMips, cmd->startLayers, cmd->startLayers + cmd->numLayers );
        BE_UpdateTexture( *backendTextures[ cmd->texture ], cmd->fullImageData, nvrhi::CommandQueue::Graphics, range );
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

    virtual void Handle_vhCreateVertexBuffer( VIDL_vhCreateVertexBuffer* cmd ) override
    {
        BE_CmdRAII cmdRAII( cmd );
        auto memRAII = BE_MemRAII( cmd->mem );

        std::vector< vhVertexLayoutDef > layoutDefs;
        if ( !vhParseVertexLayoutInternal( cmd->layout, layoutDefs ) )
        {
            VRHI_ERR( "vhCreateVertexBuffer() : Invalid vertex layout!\n" );
            return;
        }
        
        /*
        auto vertexBufferDesc = nvrhi::BufferDesc()
    .setByteSize(sizeof(g_Vertices))
    .setIsVertexBuffer(true)
    .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer)
    .setDebugName("Vertex Buffer");

nvrhi::BufferHandle vertexBuffer = nvrhiDevice->createBuffer(vertexBufferDesc);
        */
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


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

#include "vrhi_impl.h"

struct vhCmdBackendState : public VIDLHandler
{
private:
    char temps[1024];
    std::map< vhTexture, nvrhi::TextureHandle > backendTextures;

    void BE_UpdateTexture( nvrhi::TextureHandle texh, const std::vector<uint8_t> *data )
    {
        if ( !texh || !data ) return;
        auto cmdlist = vhCmdListGet();
        //cmdlist->updateTexture( texh, data );
    }

public:
    void init()
    {
    }

    void shutdown()
    {
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
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            backendTextures.erase( cmd->texture );
        }

        vhCmdRelease( cmd );
    }

    void Handle_vhCreateTexture( VIDL_vhCreateTexture* cmd ) override
    {
        if ( cmd->texture == VRHI_INVALID_HANDLE ||
            cmd->dimensions.x <= 0 || cmd->dimensions.y <= 0 || cmd->dimensions.z <= 0 ||
            cmd->numMips == 0 || cmd->numLayers <= 0 || cmd->format == nvrhi::Format::UNKNOWN )
        {
            VRHI_ERR( "vhCreateTexture() : Invalid parameters! TexID %u %d x %d x %d mips %d layers %d format %d\n",
                cmd->texture, cmd->dimensions.x, cmd->dimensions.y, cmd->dimensions.z, cmd->numMips, cmd->numLayers, cmd->format );
            vhCmdRelease( cmd );
            return;
        }

        sprintf_s( temps, "Texture %d\n", cmd->texture );
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
            std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
            // RefCountPtr handle will be released automatically by NVRHI.
            texture = g_vhDevice->createTexture(textureDesc);
        }
        if ( !texture )
        {
            VRHI_ERR( "vhCreateTexture() : Failed to create texture!\n" );
            vhCmdRelease( cmd );
            return;
        }

        backendTextures[ cmd->texture ] = texture;
        vhCmdRelease( cmd );
    }

    void Handle_vhFlushInternal( VIDL_vhFlushInternal* cmd ) override
    {
        if ( cmd->waitForGPU )
        {
            vhCmdListFlushAll();
            g_vhDevice->waitForIdle();
        }

        // Safety: fence is from stack of caller
        g_vhDevice->runGarbageCollection();
        if ( cmd->fence )
            cmd->fence->store( true );
        vhCmdRelease( cmd );
    }

    void stepFrame()
    {
        // This cleans up any resources that are no longer in use.
        g_vhDevice->runGarbageCollection();
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
            if ( cmd == nullptr ) continue;
            HandleCmd( cmd );
        }

        VRHI_LOG( "    RHI Thread exiting.\n" );
    }
};


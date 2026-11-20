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
    OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
    IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "vrhi_internal.h"
#include "vrhi_backend.h"

extern "C" void* vhGetMetalLayerFromNSView( void* viewPtr );

void* vhGetMetalLayer( void* windowHandle, bool macOSWindowIsNSView )
{
    if ( macOSWindowIsNSView ) return vhGetMetalLayerFromNSView( windowHandle );
    return windowHandle;
}

// Keep the backbuffer ID when the underlying drawable changes.
static bool vhAcquireMetalDrawable( vhSwapchain& sc )
{
    [( id )sc.metalDrawable release];
    sc.metalDrawable = nil;
    sc.isMinimized = true;
    sc.windowSize = glm::uvec2( 0, 0 );

    @autoreleasepool
    {
        CAMetalLayer* layer = ( CAMetalLayer* )sc.metalLayer;
        id< CAMetalDrawable > drawable = [layer nextDrawable];
        if ( !drawable ) return false;

        id< MTLTexture > texture = drawable.texture;
        nvrhi::TextureDesc desc;
        desc.width = ( uint32_t )texture.width;
        desc.height = ( uint32_t )texture.height;
        switch ( texture.pixelFormat )
        {
            case MTLPixelFormatBGRA8Unorm:      desc.format = nvrhi::Format::BGRA8_UNORM; break;
            case MTLPixelFormatBGRA8Unorm_sRGB: desc.format = nvrhi::Format::SBGRA8_UNORM; break;
            case MTLPixelFormatRGBA16Float:     desc.format = nvrhi::Format::RGBA16_FLOAT; break;
            case MTLPixelFormatRGB10A2Unorm:    desc.format = nvrhi::Format::R10G10B10A2_UNORM; break;
            default:
                VRHI_ERR( "vhAcquireMetalDrawable(): Unsupported drawable format %lu.\n", ( unsigned long )texture.pixelFormat );
                return false;
        }

        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        desc.isRenderTarget = true;
        desc.debugName = "MetalDrawable";

        nvrhi::TextureHandle handle = g_vhDevice->createHandleForNativeTexture(
            nvrhi::ObjectTypes::Metal_Texture, nvrhi::Object( ( __bridge void* )texture ), desc );
        if ( !handle )
        {
            VRHI_ERR( "vhAcquireMetalDrawable(): Failed to wrap the drawable texture.\n" );
            return false;
        }

        sc.metalDrawable = [drawable retain];
        if ( sc.textures.empty() ) sc.textures.push_back( vhAllocTexture() );
        g_vhCmdBackendState.RegisterInternalTexture( sc.textures[0], handle, desc );
        sc.nvrhiHandles.assign( 1, handle );
        sc.currentSwapchainIndex = 0;
        sc.isMinimized = false;
        sc.windowSize = glm::uvec2( desc.width, desc.height );
        return true;
    }
}

void vhSwapchainCreateMetal_Internal( vhSwapchain& sc, int width, int height )
{
    CAMetalLayer* layer = ( CAMetalLayer* )sc.metalLayer;
    if ( !layer )
    {
        VRHI_ERR( "vhSwapchainCreateMetal_Internal(): No CAMetalLayer for the swapchain.\n" );
        return;
    }

    if ( !layer.device )
    {
        layer.device = ( __bridge id< MTLDevice > )g_vhMetalDevice->getNativeDevice().pointer;
        layer.displaySyncEnabled = g_vhInit.vsync ? YES : NO;
        layer.pixelFormat = g_vhInit.scrgb ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
    }

    // Allow copies from the backbuffer.
    layer.framebufferOnly = NO;
    layer.drawableSize = CGSizeMake( width, height );
    vhAcquireMetalDrawable( sc );
}

// Schedule the present before the command-list flush commits it.
bool vhSwapchainPresentAndAcquireMetal_Internal( vhSwapchain& sc, uint64_t& outInstance, nvrhi::EventQueryHandle* frameQuery )
{
    outInstance = 0;
    if ( sc.isHeadless ) return true;
    if ( sc.isMinimized || !sc.metalLayer || !sc.metalDrawable ) return false;

    @autoreleasepool
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        id< MTLCommandBuffer > commandBuffer =
            ( id< MTLCommandBuffer > )cmdList->getNativeObject( nvrhi::ObjectTypes::Metal_CommandBuffer ).pointer;
        if ( !commandBuffer ) return false;
        [commandBuffer presentDrawable:( id< CAMetalDrawable > )sc.metalDrawable];
        outInstance = vhCmdListFlush_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        if ( frameQuery && outInstance != 0 )
        {
            if ( !*frameQuery ) *frameQuery = g_vhDevice->createEventQuery();
            else g_vhDevice->resetEventQuery( *frameQuery );
            g_vhDevice->setEventQuery( *frameQuery, nvrhi::CommandQueue::Graphics );
        }
    }

    return outInstance != 0 && vhAcquireMetalDrawable( sc );
}

void vhSwapchainDestroyMetal_Internal( vhSwapchain& sc )
{
    [( id )sc.metalDrawable release];
    sc.metalDrawable = nil;
    for ( vhTexture texture : sc.textures )
    {
        vhSwapchainReleaseTexture( texture );
    }
    sc.textures.clear();
    sc.nvrhiHandles.clear();
}

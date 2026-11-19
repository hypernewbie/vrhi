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

// [UAA] - START - native Metal backend
// The windowing seams of the Metal backend. VRHI owns the layer, the drawable and
// the presentation order; the fork owns the device and command buffers.
// Compiled without ARC, like the fork's Metal sources: the retained layer and
// drawable pointers live in the C++ swapchain struct.

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include <vrhi_internal.h>
#include "vrhi_backend.h"

extern "C" void* vhGetMetalLayerFromNSView( void* viewPtr );

// Resolves the swapchain handle contract into the layer the backend draws into.
void* vhMetalResolveLayer( void* windowHandle, bool macOSWindowIsNSView )
{
    if ( !windowHandle ) return nullptr;
    if ( macOSWindowIsNSView ) return vhGetMetalLayerFromNSView( windowHandle );
    return windowHandle;
}

// Acquires the next drawable of the swapchain's layer and re-registers the
// backbuffer texture for it. The drawable rotates inside the layer's pool, so
// the single backbuffer keeps its texture ID and changes its backing.
static bool vhMetalAcquireDrawable( vhSwapchain& sc )
{
    CAMetalLayer* layer = ( CAMetalLayer* )sc.metalLayer;
    id< CAMetalDrawable > drawable = [layer nextDrawable];
    if ( !drawable )
    {
        // No drawable means the layer is detached or has no size left, the
        // equivalent of a minimized window.
        [( id )sc.metalDrawable release];
        sc.metalDrawable = nil;
        sc.isMinimized = true;
        return false;
    }

    [( id )sc.metalDrawable release];
    sc.metalDrawable = [drawable retain];

    nvrhi::TextureDesc desc;
    desc.width = ( uint32_t )drawable.texture.width;
    desc.height = ( uint32_t )drawable.texture.height;
    desc.format = nvrhi::Format::BGRA8_UNORM;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.keepInitialState = true;
    desc.setIsRenderTarget( true );
    desc.debugName = "MetalDrawable";

    nvrhi::TextureHandle handle = g_vhDevice->createHandleForNativeTexture(
        nvrhi::ObjectTypes::Metal_Texture, nvrhi::Object( ( void* )drawable.texture ), desc );
    if ( !handle )
    {
        VRHI_ERR( "vhMetalAcquireDrawable(): Failed to wrap the drawable texture.\n" );
        return false;
    }

    if ( sc.textures.empty() )
    {
        sc.textures.push_back( vhAllocTexture() );
    }
    g_vhCmdBackendState.RegisterInternalTexture( sc.textures[0], handle, desc );
    sc.nvrhiHandles.assign( 1, handle );
    sc.currentSwapchainIndex = 0;
    sc.isMinimized = false;
    sc.windowSize = glm::uvec2( desc.width, desc.height );
    return true;
}

// Configures the layer for the requested size and format and acquires the first
// drawable. The layer belongs to the caller; only the drawable is retained.
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
        layer.device = ( id< MTLDevice > )g_vhMetalDevice->getNativeObject( nvrhi::ObjectTypes::Metal_Device ).pointer;
        // Reads and blits against the backbuffer rule out a framebuffer-only layer.
        layer.framebufferOnly = NO;
        layer.displaySyncEnabled = g_vhInit.vsync ? YES : NO;
        if ( g_vhInit.scrgb )
        {
            layer.pixelFormat = MTLPixelFormatRGBA16Float;
        }
        else
        {
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        }
    }

    layer.drawableSize = CGSizeMake( width, height );
    vhMetalAcquireDrawable( sc );
}

// Presents the current drawable through the graphics command list and acquires
// the next one. The present is scheduled on the borrowed Metal command buffer
// before the flush commits it, so it follows the frame's work in queue order.
bool vhSwapchainPresentAndAcquireMetal_Internal( vhSwapchain& sc, uint64_t& outInstance )
{
    if ( sc.isHeadless ) return true;
    if ( sc.isMinimized || !sc.metalLayer ) return true;

    outInstance = 0;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        auto cmdList = vhCmdListGet_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
        if ( sc.metalDrawable )
        {
            id< MTLCommandBuffer > commandBuffer =
                ( id< MTLCommandBuffer > )cmdList->getNativeObject( nvrhi::ObjectTypes::Metal_CommandBuffer ).pointer;
            if ( commandBuffer )
            {
                [commandBuffer presentDrawable:( id< CAMetalDrawable > )sc.metalDrawable];
            }
        }
        outInstance = vhCmdListFlush_DeviceStateLocked( nvrhi::CommandQueue::Graphics );
    }

    vhMetalAcquireDrawable( sc );
    return true;
}

// Drops the current drawable and the backbuffer texture ID. The layer belongs
// to the caller and stays untouched.
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
// [UAA] - END

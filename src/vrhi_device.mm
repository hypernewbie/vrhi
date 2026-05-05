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

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" void* vhGetMetalLayerFromNSView( void* viewPtr )
{
    if ( !viewPtr ) return nullptr;
    NSView* view = ( __bridge NSView* )viewPtr;
    [view setWantsLayer:YES];
    CAMetalLayer* layer = [CAMetalLayer layer];

    // Match the backing-store scale of the view's window (or screen as fallback) so the
    // Metal drawable matches the framebuffer pixels GLFW reports via glfwGetFramebufferSize().
    // Without this the layer defaults to contentsScale 1.0, producing a non-retina swapchain
    // that the renderer then mismatches against (everything draws at 1/2 resolution and macOS
    // upscales 2x, making content + text appear "zoomed in" on retina displays).
    CGFloat scale = 1.0;
    if ( view.window )
        scale = view.window.backingScaleFactor;
    else if ( NSScreen.mainScreen )
        scale = NSScreen.mainScreen.backingScaleFactor;
    layer.contentsScale = scale;

    view.layer = layer;
    return ( __bridge void* )layer;
}

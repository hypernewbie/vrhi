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
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#import <Metal/Metal.h>

namespace vrhi_test
{

bool vhMetalHasArgumentBufferTier2( void* device )
{
    if ( !device ) return false;
    id< MTLDevice > metalDevice = ( __bridge id< MTLDevice > )device;
    return metalDevice.argumentBuffersSupport == MTLArgumentBuffersTier2;
}

struct vhMetalGpuGate
{
    id< MTLCommandQueue > queue = nil;
    id< MTLSharedEvent >  event = nil;
};

vhMetalGpuGate* vhMetalGpuGateCreate( void* device, void* queue )
{
    if ( !device || !queue ) return nullptr;

    id< MTLDevice > metalDevice = ( __bridge id< MTLDevice > )device;
    id< MTLSharedEvent > event = [metalDevice newSharedEvent];
    if ( !event ) return nullptr;

    return new vhMetalGpuGate{ ( __bridge id< MTLCommandQueue > )queue, event };
}

bool vhMetalGpuGateEnqueueWait( vhMetalGpuGate* gate )
{
    @autoreleasepool
    {
        id< MTLCommandBuffer > buffer = [gate->queue commandBuffer];
        if ( !buffer ) return false;

        [buffer encodeWaitForEvent:gate->event value:1];
        [buffer commit];
        return true;
    }
}

void vhMetalGpuGateRelease( vhMetalGpuGate* gate )
{
    [gate->event setSignaledValue:1];
}

void vhMetalGpuGateDestroy( vhMetalGpuGate* gate )
{
    delete gate;
}

}

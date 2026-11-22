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

#include <string.h>

int main( int argc, const char* argv[] )
{
    if ( argc != 2 ) return 1;

    @autoreleasepool
    {
        id< MTLDevice > device = MTLCreateSystemDefaultDevice();
        if ( !device ) return 1;

        MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
        if ( strcmp( argv[1], "White" ) == 0 )
        {
            desc.sAddressMode = MTLSamplerAddressModeClampToBorderColor;
            desc.borderColor = MTLSamplerBorderColorOpaqueWhite;
        }
        else if ( strcmp( argv[1], "Black" ) == 0 )
        {
            desc.sAddressMode = MTLSamplerAddressModeClampToBorderColor;
            desc.borderColor = MTLSamplerBorderColorOpaqueBlack;
        }
        else if ( strcmp( argv[1], "MirrorOnce" ) == 0 )
        {
            desc.sAddressMode = MTLSamplerAddressModeMirrorClampToEdge;
        }
        else return 1;
        return [device newSamplerStateWithDescriptor:desc] ? 0 : 1;
    }
}

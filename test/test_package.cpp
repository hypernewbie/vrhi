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

#include <vrhi.h>
#include <cstdio>

extern nvrhi::DeviceHandle g_vhDevice;

// Test compute shader
static const char* kComputeShaderSource
    = "#version 450\n"
      "layout(local_size_x = 64) in;\n"
      "layout(binding = 0) buffer InputBuffer { float data[]; } input_buffer;\n"
      "layout(binding = 1) buffer OutputBuffer { float data[]; } output_buffer;\n"
      "void main()\n"
      "{\n"
      "    uint idx = gl_GlobalInvocationID.x;\n"
      "    output_buffer.data[idx] = input_buffer.data[idx] * 2.0;\n"
      "}\n";

int main( )
{
    // Initialise VRHI in quiet mode
    vhInit( true );

    if ( !g_vhDevice )
    {
        fprintf( stderr, "FATAL: vhInit failed\n" );
        return 1;
    }

    // Test 1: Buffer allocation and creation
    vhBuffer buffer = vhAllocBuffer( );
    if ( buffer == VRHI_INVALID_HANDLE )
    {
        fprintf( stderr, "FATAL: Buffer allocation failed\n" );
        vhShutdown( );
        return 2;
    }

    vhMem* dataBuf = vhAllocMem( 1024 * sizeof( float ) );
    vhCreateStorageBuffer( buffer, "test_buffer", dataBuf, dataBuf->size() );
    vhFlush( ); // Ensure resource is created

    // Test 2: Texture allocation and creation
    vhTexture texture = vhAllocTexture( );
    if ( texture == VRHI_INVALID_HANDLE )
    {
        fprintf( stderr, "FATAL: Texture allocation failed\n" );
        vhDestroyBuffer( buffer );
        vhShutdown( );
        return 3;
    }

    vhCreateTexture2D( texture, "PackageTestTex", glm::ivec2( 128, 128 ), 1, nvrhi::Format::RGBA8_UNORM );
    vhFlush( );

    // Test 3: Shader allocation (without compilation - Vulkan SDK required for runtime compile)
    vhShader shader = vhAllocShader( );
    if ( shader == VRHI_INVALID_HANDLE )
    {
        fprintf( stderr, "FATAL: Shader allocation failed\n" );
        vhDestroyTexture( texture );
        vhDestroyBuffer( buffer );
        vhShutdown( );
        return 4;
    }

    // Cleanup
    vhDestroyShader( shader );
    vhDestroyTexture( texture );
    vhDestroyBuffer( buffer );
    vhFlush( );

    vhShutdown( );

    // Success
    return 0;
}

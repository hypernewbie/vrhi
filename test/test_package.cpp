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
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static bool TestMetalDispatch( const char* shaderPath )
{
    std::ifstream file( shaderPath, std::ios::binary | std::ios::ate );
    if ( !file )
    {
        fprintf( stderr, "FATAL: Cannot open frozen shader: %s\n", shaderPath );
        return false;
    }
    const std::streamsize size = file.tellg();
    if ( size <= 0 || size % sizeof( uint32_t ) != 0 ) return false;
    std::vector< uint32_t > spirv( size_t( size ) / sizeof( uint32_t ) );
    file.seekg( 0 );
    if ( !file.read( reinterpret_cast< char* >( spirv.data() ), size ) ) return false;

    constexpr uint32_t count = 4;
    vhBuffer output = vhAllocBuffer();
    vhShader shader = vhAllocShader();
    if ( output == VRHI_INVALID_HANDLE || shader == VRHI_INVALID_HANDLE ) return false;
    vhCreateStorageBuffer( output, "PackageOutput", nullptr, count * sizeof( uint32_t ),
        VRHI_BUFFER_COMPUTE_READ_WRITE, sizeof( uint32_t ) );
    vhCreateShader( shader, "PackageSmoke", VRHI_SHADER_STAGE_COMPUTE, spirv, "main" );
    vhState state;
    state.SetProgram( vhCreateComputeProgram( shader ) );
    state.SetBuffer( 0, output, -1, "g_Output", 0, count * sizeof( uint32_t ), true );
    constexpr vhStateId stateId = 1;
    vhSetState( stateId, state );
    vhDispatch( stateId, { count, 1, 1 } );
    vhMem readback;
    vhReadBufferSlow( output, 0, count * sizeof( uint32_t ), &readback );

    bool valid = readback.size() == count * sizeof( uint32_t );
    for ( uint32_t i = 0; valid && i < count; ++i )
    {
        uint32_t value = 0;
        memcpy( &value, readback.data() + i * sizeof( value ), sizeof( value ) );
        valid = value == 0xA11CE000u + i;
        if ( !valid ) fprintf( stderr, "FATAL: Metal readback at %u is 0x%08x\n", i, value );
    }
    vhState empty;
    vhSetState( stateId, empty, VRHI_DIRTY_ALL );
    vhDestroyShader( shader );
    vhDestroyBuffer( output );
    vhFinish();
    return valid;
}

int main( int argc, char** argv )
{
    const bool metal = argc == 3 && strcmp( argv[1], "--backend=metal" ) == 0;
    if ( argc != 1 && !metal )
    {
        fprintf( stderr, "Usage: %s [--backend=metal frozen_shader.spv]\n", argv[0] );
        return 5;
    }
    std::atomic< uint32_t > errors{ 0 };
    g_vhInit.useMetal = metal;
    if ( metal )
    {
        g_vhInit.fnLogCallback = [ &errors ]( bool error, const std::string& text )
        {
            if ( error )
            {
                ++errors;
                fprintf( stderr, "%s", text.c_str() );
            }
        };
    }
    vhInit( true );

    if ( !g_vhDevice )
    {
        fprintf( stderr, "FATAL: vhInit failed\n" );
        return 1;
    }

    const nvrhi::GraphicsAPI expectedApi = metal ? nvrhi::GraphicsAPI::METAL : nvrhi::GraphicsAPI::VULKAN;
    if ( g_vhDevice->getGraphicsAPI() != expectedApi )
    {
        fprintf( stderr, "FATAL: The selected backend was not initialised\n" );
        vhShutdown();
        return 5;
    }
    printf( "Package backend: %s\n", metal ? "Metal" : "Vulkan" );
    if ( metal && !TestMetalDispatch( argv[2] ) )
    {
        fprintf( stderr, "FATAL: Metal package dispatch failed\n" );
        vhShutdown();
        return 5;
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

    // The Metal dispatch already creates and destroys a shader.
    vhShader shader = VRHI_INVALID_HANDLE;
    if ( !metal )
    {
        shader = vhAllocShader();
        if ( shader == VRHI_INVALID_HANDLE )
        {
            fprintf( stderr, "FATAL: Shader allocation failed\n" );
            vhDestroyTexture( texture );
            vhDestroyBuffer( buffer );
            vhShutdown();
            return 4;
        }
    }

    // Cleanup
    if ( shader != VRHI_INVALID_HANDLE ) vhDestroyShader( shader );
    vhDestroyTexture( texture );
    vhDestroyBuffer( buffer );
    vhFlush( );

    vhShutdown( );
    if ( errors.load() != 0 )
    {
        fprintf( stderr, "FATAL: Metal reported %u errors\n", errors.load() );
        return 5;
    }

    // Success
    return 0;
}

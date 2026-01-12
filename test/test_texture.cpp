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

#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "utest.h"
#include "test.h"
#include <vrhi.h>
#include <vrhi_internal.h>

extern bool g_testInit;
extern bool g_testInitQuiet;
extern std::atomic<int32_t> g_vhErrorCounter;

struct Texture {};

UTEST_F_SETUP( Texture )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    if ( !g_captureActive )
    {
        vhCaptureStart();
        g_captureActive = true;
    }
    
    vhBeginMarker( utest_test_name );
}

UTEST_F_TEARDOWN( Texture )
{
    vhEndMarker();
}

UTEST_F( Texture, CreateDestroyError )
{

    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    // Create a texture with INVALID dimensions (-1 implies invalid)
    vhCreateTexture(
        tex,
        nvrhi::TextureDimension::Texture2D,
        glm::ivec3( -1, -5, 1 ), // Invalid size
        1, 1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB,
        nullptr
    );

    vhFlush();

    // Verify error was incremented
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors + 1 );
    vhDestroyTexture( tex );
}

UTEST_F( Texture, CreateHelpers )
{


    int32_t startErrors = g_vhErrorCounter.load();

    // 2D
    vhTexture tex2D = vhAllocTexture();
    vhCreateTexture2D( tex2D, glm::ivec2( 128, 128 ), 1, nvrhi::Format::RGBA8_UNORM );

    // 3D
    vhTexture tex3D = vhAllocTexture();
    vhCreateTexture3D( tex3D, glm::ivec3( 32, 32, 32 ), 1, nvrhi::Format::RGBA8_UNORM );

    // Cube
    vhTexture texCube = vhAllocTexture();
    vhCreateTextureCube( texCube, 128, 1, nvrhi::Format::RGBA8_UNORM );

    // 2D Array
    vhTexture tex2DArray = vhAllocTexture();
    vhCreateTexture2DArray( tex2DArray, glm::ivec2( 128, 128 ), 4, 1, nvrhi::Format::RGBA8_UNORM );

    // Cube Array
    vhTexture texCubeArray = vhAllocTexture();
    vhCreateTextureCubeArray( texCubeArray, 128, 12, 1, nvrhi::Format::RGBA8_UNORM );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyTexture( tex2D );
    vhDestroyTexture( tex3D );
    vhDestroyTexture( texCube );
    vhDestroyTexture( tex2DArray );
    vhDestroyTexture( texCubeArray );
}


UTEST_F( Texture, CreateDestroy )
{


    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    vhCreateTexture(
        tex,
        nvrhi::TextureDimension::Texture2D,
        glm::ivec3( 256, 256, 1 ),
        1, 1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB, // Some default flag
        nullptr // No data
    );

    vhDestroyTexture( tex );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

UTEST_F( Texture, CreateDestroyStressTest )
{


    int32_t startErrors = g_vhErrorCounter.load();

    // Create a predictable RNG for determinism
    std::srand( 12345 );

    const int kNumTextures = 127;
    std::vector<vhTexture> textures;

    // Allocate & Create
    for ( int i = 0; i < kNumTextures; ++i )
    {
        vhTexture tex = vhAllocTexture();
        EXPECT_NE( tex, VRHI_INVALID_HANDLE );
        textures.push_back( tex );

        // Random 8..1024
        int w = 8 + ( std::rand() % 1017 );
        int h = 8 + ( std::rand() % 1017 );

        vhCreateTexture(
            tex,
            nvrhi::TextureDimension::Texture2D,
            glm::ivec3( w, h, 1 ),
            1, 1,
            nvrhi::Format::RGBA8_UNORM,
            VRHI_TEXTURE_SRGB,
            nullptr
        );
    }

    // Destroy in random order ideally, but linear is fine for basic stress
    for ( vhTexture t : textures )
    {
        vhDestroyTexture( t );
    }

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

UTEST_F( Texture, Update )
{


    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    const int width = 64;
    const int height = 64;
    const size_t dataSize = width * height * 4; // RGBA8
    auto initialData = vhAllocMem( dataSize );

    // Fill with gibberish
    for ( size_t i = 0; i < dataSize; ++i ) ( *initialData )[i] = ( uint8_t ) ( rand() % 256 );

    vhCreateTexture2D(
        tex,
        glm::ivec2( width, height ),
        1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB,
        initialData
    );
    vhFinish();

    // Test 3 updates
    for ( int i = 0; i < 3; ++i )
    {
        // New gibberish
        auto updateData = vhAllocMem( dataSize );
        for ( size_t k = 0; k < dataSize; ++k ) ( *updateData )[k] = ( uint8_t ) ( rand() % 256 );

        // Full update
        vhUpdateTexture(
            tex,
            0, 0, // start mip, start layer
            1, 1, // num mips, num layers
            updateData
        );

        // Process
        vhFinish();
    }

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyTexture( tex );
    vhFinish();
}

UTEST_F( Texture, Readback )
{


    int32_t startErrors = g_vhErrorCounter.load();

    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );

    const int width = 32;
    const int height = 32;
    const size_t dataSize = width * height * 4; // RGBA8
    auto initialData = vhAllocMem( dataSize );

    // Fill with known pattern
    for ( size_t i = 0; i < dataSize; ++i ) ( *initialData )[i] = ( uint8_t ) ( i % 255 );

    vhCreateTexture2D(
        tex,
        glm::ivec2( width, height ),
        1,
        nvrhi::Format::RGBA8_UNORM,
        VRHI_TEXTURE_SRGB,
        initialData
    );

    // Copy reference data before backend consumes it (needed for verification)

    std::vector<uint8_t> refData = *initialData; // Copy for verification

    // Flush to ensure creation happens
    vhFlush();

    // Read back
    vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );

    // Finish ensures GPU is done and readback is complete
    vhFinish();

    // Compare
    EXPECT_EQ( readData.size(), dataSize );
    if ( readData.size() == dataSize )
    {
        for ( size_t i = 0; i < dataSize; ++i )
        {
            EXPECT_EQ( readData[i], refData[i] );
            if ( readData[i] != refData[i] ) break; // Fail fast
        }
    }

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyTexture( tex );
}


UTEST_F( Texture, Allocation )
{


    vhTexture t1 = vhAllocTexture();
    vhTexture t2 = vhAllocTexture();
    vhTexture t3 = vhAllocTexture();

    EXPECT_NE( t1, VRHI_INVALID_HANDLE );
    EXPECT_NE( t2, VRHI_INVALID_HANDLE );
    EXPECT_NE( t3, VRHI_INVALID_HANDLE );

    EXPECT_NE( t1, t2 );
    EXPECT_NE( t2, t3 );
    EXPECT_NE( t1, t3 );

    vhDestroyTexture( t1 );
    vhDestroyTexture( t2 );
    vhDestroyTexture( t3 );
    vhFlush();
}


UTEST_F( Texture, BlitConnectivity )
{


    const int width = 64;
    const int height = 64;
    const size_t dataSize = width * height * 4;

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Source: All 255
    vhMem* whiteData = vhAllocMem( dataSize );
    std::fill( whiteData->begin(), whiteData->end(), 255 );
    vhCreateTexture2D( src, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, whiteData );

    // Destination: All 0
    vhMem* blackData = vhAllocMem( dataSize );
    std::fill( blackData->begin(), blackData->end(), 0 );
    vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, blackData );

    vhFinish();

    // Call blit
    vhBlitTexture( dst, src );
    vhFinish();

    // Readback and verify
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), dataSize );
    for ( size_t i = 0; i < dataSize; ++i )
    {
        EXPECT_EQ( readData[i], 255 );
        if ( readData[i] != 255 ) break;
    }

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST_F( Texture, BlitMipToMip )
{


    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Src: 128x128 with 4 mips (Mip 1 is 64x64)
    vhCreateTexture2D( src, glm::ivec2( 128, 128 ), 4, nvrhi::Format::RGBA8_UNORM );
    // Dst: 64x64 with 1 mip
    vhCreateTexture2D( dst, glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );

    // Fill Src Mip 1 with 128
    const size_t mip1DataSize = 64 * 64 * 4;
    vhMem* mipData = vhAllocMem( mip1DataSize );
    std::fill( mipData->begin(), mipData->end(), 128 );
    vhUpdateTexture( src, 1, 0, 1, 1, mipData );
    vhFinish();

    // Blit Src Mip 1 to Dst Mip 0
    vhBlitTexture( dst, src, 0, 1 );
    vhFinish();

    // Readback and verify
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), mip1DataSize );
    for ( size_t i = 0; i < mip1DataSize; ++i )
    {
        EXPECT_EQ( readData[i], 128 );
        if ( readData[i] != 128 ) break;
    }

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST_F( Texture, BlitPartialRegion )
{


    const int width = 64;
    const int height = 64;
    const size_t dataSize = width * height * 4;

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Source: 200
    vhMem* srcData = vhAllocMem( dataSize );
    std::fill( srcData->begin(), srcData->end(), 200 );
    vhCreateTexture2D( src, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, srcData );

    // Dest: 50
    vhMem* dstData = vhAllocMem( dataSize );
    std::fill( dstData->begin(), dstData->end(), 50 );
    vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, dstData );

    vhFinish();

    // Copy 32x32 region from Src(16,16) to Dst(8,8)
    glm::ivec3 extent( 32, 32, 1 );
    glm::ivec3 srcOffset( 16, 16, 0 );
    glm::ivec3 dstOffset( 8, 8, 0 );
    vhBlitTexture( dst, src, 0, 0, 0, 0, dstOffset, srcOffset, extent );
    vhFinish();

    // Readback and verify
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), dataSize );
    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            uint8_t val = readData[( y * width + x ) * 4];
            if ( x >= 8 && x < 8 + 32 && y >= 8 && y < 8 + 32 )
            {
                EXPECT_EQ( val, 200 );
            }
            else
            {
                EXPECT_EQ( val, 50 );
            }
        }
    }

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST( Sampler, MaskNonOverlap )
{
    // Each mask should be non-overlapping with all others
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK & VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOUR_MASK & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOUR_MASK & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );

    EXPECT_EQ( VRHI_SAMPLER_SAMPLE_STENCIL & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
}

UTEST( Sampler, ValuesWithinMask )
{
    // Address U
    EXPECT_EQ( VRHI_SAMPLER_U_WRAP & ~VRHI_SAMPLER_U_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_MIRROR & ~VRHI_SAMPLER_U_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_CLAMP & ~VRHI_SAMPLER_U_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_U_BORDER & ~VRHI_SAMPLER_U_MASK, 0u );

    // Address V
    EXPECT_EQ( VRHI_SAMPLER_V_WRAP & ~VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_MIRROR & ~VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_CLAMP & ~VRHI_SAMPLER_V_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_V_BORDER & ~VRHI_SAMPLER_V_MASK, 0u );

    // Address W
    EXPECT_EQ( VRHI_SAMPLER_W_WRAP & ~VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_MIRROR & ~VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_CLAMP & ~VRHI_SAMPLER_W_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_W_BORDER & ~VRHI_SAMPLER_W_MASK, 0u );

    // Min Filter
    EXPECT_EQ( VRHI_SAMPLER_MIN_LINEAR & ~VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_POINT & ~VRHI_SAMPLER_MIN_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIN_ANISOTROPIC & ~VRHI_SAMPLER_MIN_MASK, 0u );

    // Mag Filter
    EXPECT_EQ( VRHI_SAMPLER_MAG_LINEAR & ~VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_POINT & ~VRHI_SAMPLER_MAG_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MAG_ANISOTROPIC & ~VRHI_SAMPLER_MAG_MASK, 0u );

    // Mip Filter
    EXPECT_EQ( VRHI_SAMPLER_MIP_LINEAR & ~VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_POINT & ~VRHI_SAMPLER_MIP_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIP_NONE & ~VRHI_SAMPLER_MIP_MASK, 0u );

    // Compare Function
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_LESS & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_LEQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_EQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_GEQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_GREATER & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_NOTEQUAL & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_NEVER & ~VRHI_SAMPLER_COMPARE_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_ALWAYS & ~VRHI_SAMPLER_COMPARE_MASK, 0u );

    // Sample Stencil (single bit flag)
    EXPECT_NE( VRHI_SAMPLER_SAMPLE_STENCIL, 0u );

    // Anisotropy Levels
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_1 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_2 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_4 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_8 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_ANISOTROPY_16 & ~VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0u );
}

UTEST( Sampler, ShiftAlignment )
{
    // U: bits 0-1
    EXPECT_EQ( VRHI_SAMPLER_U_SHIFT, 0 );
    EXPECT_EQ( VRHI_SAMPLER_U_MASK, 0x3u << VRHI_SAMPLER_U_SHIFT );

    // V: bits 2-3
    EXPECT_EQ( VRHI_SAMPLER_V_SHIFT, 2 );
    EXPECT_EQ( VRHI_SAMPLER_V_MASK, 0x3u << VRHI_SAMPLER_V_SHIFT );

    // W: bits 4-5
    EXPECT_EQ( VRHI_SAMPLER_W_SHIFT, 4 );
    EXPECT_EQ( VRHI_SAMPLER_W_MASK, 0x3u << VRHI_SAMPLER_W_SHIFT );

    // Min: bits 6-7
    EXPECT_EQ( VRHI_SAMPLER_MIN_SHIFT, 6 );
    EXPECT_EQ( VRHI_SAMPLER_MIN_MASK, 0x3u << VRHI_SAMPLER_MIN_SHIFT );

    // Mag: bits 8-9
    EXPECT_EQ( VRHI_SAMPLER_MAG_SHIFT, 8 );
    EXPECT_EQ( VRHI_SAMPLER_MAG_MASK, 0x3u << VRHI_SAMPLER_MAG_SHIFT );

    // Mip: bits 10-11
    EXPECT_EQ( VRHI_SAMPLER_MIP_SHIFT, 10 );
    EXPECT_EQ( VRHI_SAMPLER_MIP_MASK, 0x3u << VRHI_SAMPLER_MIP_SHIFT );

    // Compare: bits 12-15
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_SHIFT, 12 );
    EXPECT_EQ( VRHI_SAMPLER_COMPARE_MASK, 0xFu << VRHI_SAMPLER_COMPARE_SHIFT );

    // MipBias: bits 16-23
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_SHIFT, 16 );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS_MASK, 0xFFu << VRHI_SAMPLER_MIPBIAS_SHIFT );

    // Border Colour: bits 24-27
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOUR_SHIFT, 24 );
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOUR_MASK, 0xFu << VRHI_SAMPLER_BORDER_COLOUR_SHIFT );

    // Sample Stencil: bit 28
    EXPECT_EQ( VRHI_SAMPLER_SAMPLE_STENCIL, 1u << 28 );

    // Max Anisotropy: bits 29-31
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT, 29 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY_MASK, 0x7u << VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT );
}

UTEST( Sampler, ValueUniqueness )
{
    // Address U values are distinct
    EXPECT_NE( VRHI_SAMPLER_U_WRAP, VRHI_SAMPLER_U_MIRROR );
    EXPECT_NE( VRHI_SAMPLER_U_WRAP, VRHI_SAMPLER_U_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_U_WRAP, VRHI_SAMPLER_U_BORDER );
    EXPECT_NE( VRHI_SAMPLER_U_MIRROR, VRHI_SAMPLER_U_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_U_MIRROR, VRHI_SAMPLER_U_BORDER );
    EXPECT_NE( VRHI_SAMPLER_U_CLAMP, VRHI_SAMPLER_U_BORDER );

    // Address V values are distinct
    EXPECT_NE( VRHI_SAMPLER_V_WRAP, VRHI_SAMPLER_V_MIRROR );
    EXPECT_NE( VRHI_SAMPLER_V_WRAP, VRHI_SAMPLER_V_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_V_WRAP, VRHI_SAMPLER_V_BORDER );
    EXPECT_NE( VRHI_SAMPLER_V_MIRROR, VRHI_SAMPLER_V_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_V_MIRROR, VRHI_SAMPLER_V_BORDER );
    EXPECT_NE( VRHI_SAMPLER_V_CLAMP, VRHI_SAMPLER_V_BORDER );

    // Address W values are distinct
    EXPECT_NE( VRHI_SAMPLER_W_WRAP, VRHI_SAMPLER_W_MIRROR );
    EXPECT_NE( VRHI_SAMPLER_W_WRAP, VRHI_SAMPLER_W_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_W_WRAP, VRHI_SAMPLER_W_BORDER );
    EXPECT_NE( VRHI_SAMPLER_W_MIRROR, VRHI_SAMPLER_W_CLAMP );
    EXPECT_NE( VRHI_SAMPLER_W_MIRROR, VRHI_SAMPLER_W_BORDER );
    EXPECT_NE( VRHI_SAMPLER_W_CLAMP, VRHI_SAMPLER_W_BORDER );

    // Min filter values are distinct
    EXPECT_NE( VRHI_SAMPLER_MIN_LINEAR, VRHI_SAMPLER_MIN_POINT );
    EXPECT_NE( VRHI_SAMPLER_MIN_LINEAR, VRHI_SAMPLER_MIN_ANISOTROPIC );
    EXPECT_NE( VRHI_SAMPLER_MIN_POINT, VRHI_SAMPLER_MIN_ANISOTROPIC );

    // Mag filter values are distinct
    EXPECT_NE( VRHI_SAMPLER_MAG_LINEAR, VRHI_SAMPLER_MAG_POINT );
    EXPECT_NE( VRHI_SAMPLER_MAG_LINEAR, VRHI_SAMPLER_MAG_ANISOTROPIC );
    EXPECT_NE( VRHI_SAMPLER_MAG_POINT, VRHI_SAMPLER_MAG_ANISOTROPIC );

    // Mip filter values are distinct
    EXPECT_NE( VRHI_SAMPLER_MIP_LINEAR, VRHI_SAMPLER_MIP_POINT );
    EXPECT_NE( VRHI_SAMPLER_MIP_LINEAR, VRHI_SAMPLER_MIP_NONE );
    EXPECT_NE( VRHI_SAMPLER_MIP_POINT, VRHI_SAMPLER_MIP_NONE );

    // Compare values are distinct
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_LEQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_EQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_GEQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_GREATER );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_NOTEQUAL );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_NEVER );
    EXPECT_NE( VRHI_SAMPLER_COMPARE_LESS, VRHI_SAMPLER_COMPARE_ALWAYS );

    // Anisotropy level values are distinct
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_2 );
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_4 );
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_8 );
    EXPECT_NE( VRHI_SAMPLER_ANISOTROPY_1, VRHI_SAMPLER_ANISOTROPY_16 );
}

UTEST( Sampler, CompositeMacros )
{
    // VRHI_SAMPLER_POINT combines min, mag, mip point
    EXPECT_EQ( VRHI_SAMPLER_POINT,
        VRHI_SAMPLER_MIN_POINT | VRHI_SAMPLER_MAG_POINT | VRHI_SAMPLER_MIP_POINT );

// UVW convenience macros
    EXPECT_EQ( VRHI_SAMPLER_UVW_WRAP,
        VRHI_SAMPLER_U_WRAP | VRHI_SAMPLER_V_WRAP | VRHI_SAMPLER_W_WRAP );
    EXPECT_EQ( VRHI_SAMPLER_UVW_MIRROR,
        VRHI_SAMPLER_U_MIRROR | VRHI_SAMPLER_V_MIRROR | VRHI_SAMPLER_W_MIRROR );
    EXPECT_EQ( VRHI_SAMPLER_UVW_CLAMP,
        VRHI_SAMPLER_U_CLAMP | VRHI_SAMPLER_V_CLAMP | VRHI_SAMPLER_W_CLAMP );
    EXPECT_EQ( VRHI_SAMPLER_UVW_BORDER,
        VRHI_SAMPLER_U_BORDER | VRHI_SAMPLER_V_BORDER | VRHI_SAMPLER_W_BORDER );

// Verify VRHI_SAMPLER_NONE is 0
    EXPECT_EQ( VRHI_SAMPLER_NONE, 0u );
}

UTEST( Sampler, MipBiasMacro )
{
    // Zero bias
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS( 0.0f ) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 0u );

    // Positive bias: 1.0 * 16 = 16
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS( 1.0f ) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 16u );

    // Positive bias: 0.5 * 16 = 8
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS( 0.5f ) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 8u );

    // Positive bias: 2.0 * 16 = 32
    EXPECT_EQ( ( VRHI_SAMPLER_MIPBIAS( 2.0f ) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu, 32u );

    // Verify result fits within mask
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS( 1.0f ) & ~VRHI_SAMPLER_MIPBIAS_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_MIPBIAS( 7.9f ) & ~VRHI_SAMPLER_MIPBIAS_MASK, 0u );

    // Negative bias: -1.0 * 16 = -16 (two's complement, expect 0xF0 or 240 in 8-bit)
    uint32_t rawBias = ( VRHI_SAMPLER_MIPBIAS( -1.0f ) >> VRHI_SAMPLER_MIPBIAS_SHIFT ) & 0xFFu;
    int8_t negBias = ( int8_t ) rawBias;
    EXPECT_EQ( negBias, -16 );
}

UTEST( Sampler, BorderColourMacro )
{
    // Colour index 0
    EXPECT_EQ( ( VRHI_SAMPLER_BORDER_COLOUR( 0 ) >> VRHI_SAMPLER_BORDER_COLOUR_SHIFT ), 0u );

    // Colour index 1
    EXPECT_EQ( ( VRHI_SAMPLER_BORDER_COLOUR( 1 ) >> VRHI_SAMPLER_BORDER_COLOUR_SHIFT ), 1u );

    // Max valid colour index (15)
    EXPECT_EQ( ( VRHI_SAMPLER_BORDER_COLOUR( 15 ) >> VRHI_SAMPLER_BORDER_COLOUR_SHIFT ), 15u );

    // Values fit within mask
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOUR( 0 ) & ~VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
    EXPECT_EQ( VRHI_SAMPLER_BORDER_COLOUR( 15 ) & ~VRHI_SAMPLER_BORDER_COLOUR_MASK, 0u );
}

UTEST( Sampler, MaxAnisotropyMacro )
{
    // Direct macro values
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY( 0 ), VRHI_SAMPLER_ANISOTROPY_1 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY( 1 ), VRHI_SAMPLER_ANISOTROPY_2 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY( 2 ), VRHI_SAMPLER_ANISOTROPY_4 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY( 3 ), VRHI_SAMPLER_ANISOTROPY_8 );
    EXPECT_EQ( VRHI_SAMPLER_MAX_ANISOTROPY( 4 ), VRHI_SAMPLER_ANISOTROPY_16 );

    // Extraction test
    uint32_t flags = VRHI_SAMPLER_ANISOTROPY_8;
    uint32_t anisoIndex = ( flags & VRHI_SAMPLER_MAX_ANISOTROPY_MASK ) >> VRHI_SAMPLER_MAX_ANISOTROPY_SHIFT;
    EXPECT_EQ( anisoIndex, 3u ); // 8x = index 3
}

UTEST( Sampler, BitsMaskCoverage )
{
    uint32_t allMasks =
        VRHI_SAMPLER_U_MASK |
        VRHI_SAMPLER_V_MASK |
        VRHI_SAMPLER_W_MASK |
        VRHI_SAMPLER_MIN_MASK |
        VRHI_SAMPLER_MAG_MASK |
        VRHI_SAMPLER_MIP_MASK |
        VRHI_SAMPLER_COMPARE_MASK |
        VRHI_SAMPLER_MIPBIAS_MASK |
        VRHI_SAMPLER_BORDER_COLOUR_MASK |
        VRHI_SAMPLER_SAMPLE_STENCIL |
        VRHI_SAMPLER_MAX_ANISOTROPY_MASK;

    EXPECT_EQ( VRHI_SAMPLER_BITS_MASK, allMasks );

    // Verify full 32-bit coverage
    EXPECT_EQ( VRHI_SAMPLER_BITS_MASK, 0xFFFFFFFFu );
}

UTEST( Sampler, CombinedFlagExtraction )
{
    // Create a complex sampler configuration
    uint32_t samplerFlags =
        VRHI_SAMPLER_U_CLAMP |
        VRHI_SAMPLER_V_MIRROR |
        VRHI_SAMPLER_W_BORDER |
        VRHI_SAMPLER_MIN_ANISOTROPIC |
        VRHI_SAMPLER_MAG_LINEAR |
        VRHI_SAMPLER_MIP_POINT |
        VRHI_SAMPLER_COMPARE_LEQUAL |
        VRHI_SAMPLER_MIPBIAS( 1.5f ) |
        VRHI_SAMPLER_BORDER_COLOUR( 5 ) |
        VRHI_SAMPLER_SAMPLE_STENCIL |
        VRHI_SAMPLER_ANISOTROPY_8;

    // Extract and verify each field
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_U_MASK, VRHI_SAMPLER_U_CLAMP );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_V_MASK, VRHI_SAMPLER_V_MIRROR );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_W_MASK, VRHI_SAMPLER_W_BORDER );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MIN_MASK, VRHI_SAMPLER_MIN_ANISOTROPIC );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MAG_MASK, VRHI_SAMPLER_MAG_LINEAR );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MIP_MASK, VRHI_SAMPLER_MIP_POINT );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_COMPARE_MASK, VRHI_SAMPLER_COMPARE_LEQUAL );
    EXPECT_EQ( ( samplerFlags & VRHI_SAMPLER_BORDER_COLOUR_MASK ) >> VRHI_SAMPLER_BORDER_COLOUR_SHIFT, 5u );
    EXPECT_NE( samplerFlags & VRHI_SAMPLER_SAMPLE_STENCIL, 0u );
    EXPECT_EQ( samplerFlags & VRHI_SAMPLER_MAX_ANISOTROPY_MASK, VRHI_SAMPLER_ANISOTROPY_8 );
}


UTEST_F( Texture, BlitFunctional )
{


    const int width = 32;
    const int height = 32;
    const size_t dataSize = width * height * 4;

    vhTexture src = vhAllocTexture();
    vhTexture dst = vhAllocTexture();

    // Source: All White (255)
    vhMem* whiteData = vhAllocMem( dataSize );
    std::fill( whiteData->begin(), whiteData->end(), 255 );
    vhCreateTexture2D( src, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, whiteData );

    // Destination: All Black (0)
    vhMem* blackData = vhAllocMem( dataSize );
    std::fill( blackData->begin(), blackData->end(), 0 );
    vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_NONE, blackData );

    vhFinish();

    // Attempt Blit (Src -> Dst)
    vhBlitTexture( dst, src );
    vhFinish();

    // Readback Dst
    vhMem readData;
    vhReadTextureSlow( dst, 0, 0, &readData );
    vhFinish();

    // Functional check
    bool match = true;
    if ( readData.size() == dataSize )
    {
        for ( size_t i = 0; i < dataSize; ++i )
        {
            if ( readData[i] != 255 )
            {
                match = false;
                break;
            }
        }
    }
    else
    {
        match = false;
    }
    EXPECT_TRUE( match );

    vhDestroyTexture( src );
    vhDestroyTexture( dst );
    vhFlush();
}

UTEST_F( Texture, BlitStress )
{


    struct FormatInfo {
        nvrhi::Format format;
        std::string name;
    };

    std::vector<FormatInfo> formats = {
        { nvrhi::Format::RGBA8_UNORM, "RGBA8_UNORM" },
        { nvrhi::Format::R8_UNORM, "R8_UNORM" },
        { nvrhi::Format::RG8_UNORM, "RG8_UNORM" },
        { nvrhi::Format::R8_UINT, "R8_UINT" },
        { nvrhi::Format::RGBA32_FLOAT, "RGBA32_FLOAT" },
        { nvrhi::Format::R32_FLOAT, "R32_FLOAT" }
    };

    const int width = 64;
    const int height = 64;

    for ( const auto& fmt : formats )
    {
        vhFormatInfo info = vhGetFormat( fmt.format );
        const int pixelSize = info.elementSize;
        const size_t dataSize = ( size_t ) width * height * pixelSize;

        vhTexture src = vhAllocTexture();
        vhTexture dst = vhAllocTexture();

        // Background Colour (all 0x55)
        vhMem* bgData = vhAllocMem( dataSize );
        std::fill( bgData->begin(), bgData->end(), 0x55 );
        vhCreateTexture2D( dst, glm::ivec2( width, height ), 1, fmt.format, VRHI_TEXTURE_NONE, bgData );

        // Foreground Colour (all 0xAA)
        vhMem* fgData = vhAllocMem( dataSize );
        std::fill( fgData->begin(), fgData->end(), 0xAA );
        vhCreateTexture2D( src, glm::ivec2( width, height ), 1, fmt.format, VRHI_TEXTURE_NONE, fgData );

        vhFinish();

        // Action 1: Full Blit
        vhBlitTexture( dst, src );
        vhFinish();

        vhMem readData;
        vhReadTextureSlow( dst, 0, 0, &readData );
        vhFinish();

        ASSERT_EQ( readData.size(), dataSize );
        for ( size_t i = 0; i < dataSize; ++i )
        {
            EXPECT_EQ( readData[i], 0xAA );
            if ( readData[i] != 0xAA ) break;
        }

        // Action 2: Region Blit
        // Reset dst to background
        vhMem* bgData2 = vhAllocMem( dataSize );
        std::fill( bgData2->begin(), bgData2->end(), 0x55 );
        vhUpdateTexture( dst, 0, 0, 1, 1, bgData2 );
        vhFinish();

        // Blit 16x16 at 8,8
        glm::ivec3 extent( 16, 16, 1 );
        glm::ivec3 offset( 8, 8, 0 );
        vhBlitTexture( dst, src, 0, 0, 0, 0, offset, offset, extent );
        vhFinish();

        readData.clear();
        vhReadTextureSlow( dst, 0, 0, &readData );
        vhFinish();

        ASSERT_EQ( readData.size(), dataSize );
        for ( int y = 0; y < height; ++y )
        {
            for ( int x = 0; x < width; ++x )
            {
                size_t pixelOffset = ( size_t ) ( y * width + x ) * pixelSize;
                bool inRegion = ( x >= 8 && x < 8 + 16 && y >= 8 && y < 8 + 16 );
                uint8_t expected = inRegion ? 0xAA : 0x55;
                for ( int c = 0; c < pixelSize; ++c )
                {
                    EXPECT_EQ( readData[pixelOffset + c], expected );
                    if ( readData[pixelOffset + c] != expected ) break;
                }
            }
        }

        vhDestroyTexture( src );
        vhDestroyTexture( dst );
        vhFlush();
    }
}

UTEST_F( Texture, RegionDataSize_SimpleRGBA8 )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 32, 32, 1 ), 0 );
    EXPECT_EQ( size, 4096 );
}

UTEST_F( Texture, RegionDataSize_ZeroExtent )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 0, 0, 0 ), 0 );
    EXPECT_EQ( size, 0 );
}

UTEST_F( Texture, RegionDataSize_NegativeExtent )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( -1, -1, -1 ), 0 );
    EXPECT_EQ( size, 0 );
}

UTEST_F( Texture, RegionDataSize_3DExtent )
{
    auto info = vhGetFormat( nvrhi::Format::RGBA8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 16, 16, 4 ), 0 );
    EXPECT_EQ( size, 4096 );
}

UTEST_F( Texture, RegionDataSize_CompressedBC1 )
{
    auto info = vhGetFormat( nvrhi::Format::BC1_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 64, 64, 1 ), 0 );
    EXPECT_EQ( size, 2048 );
}

UTEST_F( Texture, RegionDataSize_CompressedNonAligned )
{
    auto info = vhGetFormat( nvrhi::Format::BC1_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 17, 17, 1 ), 0 );
    // (ceil(17/4) * ceil(17/4) * 8) = 5 * 5 * 8 = 200
    EXPECT_EQ( size, 200 );
}

UTEST_F( Texture, RegionDataSize_R8 )
{
    auto info = vhGetFormat( nvrhi::Format::R8_UNORM );
    int64_t size = vhGetRegionDataSize( info, glm::ivec3( 100, 100, 1 ), 0 );
    EXPECT_EQ( size, 10000 );
}


UTEST_F( Texture, Type_2DArray )
{


    const int width = 32;
    const int height = 32;
    const int layers = 4;
    const size_t layerSize = width * height; // R8_UINT
    const size_t totalSize = layerSize * layers;

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2DArray( tex, glm::ivec2( width, height ), layers, 1, nvrhi::Format::R8_UINT );

    // Action 1: Update all 4 layers in one call
    auto fullData = vhAllocMem( totalSize );
    for ( int l = 0; l < layers; ++l )
    {
        std::fill( fullData->begin() + l * layerSize, fullData->begin() + ( l + 1 ) * layerSize, ( uint8_t ) l );
    }
    vhUpdateTexture( tex, 0, 0, 1, layers, fullData );
    vhFinish();

    // Verify 1
    for ( int l = 0; l < layers; ++l )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, l, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), layerSize );
        for ( size_t i = 0; i < layerSize; ++i )
        {
            EXPECT_EQ( readData[i], ( uint8_t ) l );
            if ( readData[i] != ( uint8_t ) l ) break;
        }
    }

    // Action 2: Update only Layer 2 (Middle) with 0xFF
    auto midData = vhAllocMem( layerSize );
    std::fill( midData->begin(), midData->end(), 0xFF );
    vhUpdateTexture( tex, 0, 2, 1, 1, midData );
    vhFinish();

    // Verify 2
    for ( int l = 0; l < layers; ++l )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, l, &readData );
        vhFinish();
        uint8_t expected = ( l == 2 ) ? 0xFF : ( uint8_t ) l;
        for ( size_t i = 0; i < layerSize; ++i )
        {
            EXPECT_EQ( readData[i], expected );
            if ( readData[i] != expected ) break;
        }
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST_F( Texture, Type_Cube )
{


    const int dim = 32;
    const int faces = 6;
    const size_t faceSize = dim * dim; // R8_UINT
    const size_t totalSize = faceSize * faces;

    vhTexture tex = vhAllocTexture();
    vhCreateTextureCube( tex, dim, 1, nvrhi::Format::R8_UINT );

    // Action 1: Update all 6 faces
    auto fullData = vhAllocMem( totalSize );
    for ( int f = 0; f < faces; ++f )
    {
        std::fill( fullData->begin() + f * faceSize, fullData->begin() + ( f + 1 ) * faceSize, ( uint8_t ) ( f + 10 ) );
    }
    vhUpdateTexture( tex, 0, 0, 1, faces, fullData );
    vhFinish();

    // Verify 1
    for ( int f = 0; f < faces; ++f )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, f, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), faceSize );
        for ( size_t i = 0; i < faceSize; ++i )
        {
            EXPECT_EQ( readData[i], ( uint8_t ) ( f + 10 ) );
            if ( readData[i] != ( uint8_t ) ( f + 10 ) ) break;
        }
    }

    // Action 2: Update Face 3 (-Y) only with 0xAA
    auto faceData = vhAllocMem( faceSize );
    std::fill( faceData->begin(), faceData->end(), 0xAA );
    vhUpdateTexture( tex, 0, 3, 1, 1, faceData );
    vhFinish();

    // Verify 2
    for ( int f = 0; f < faces; ++f )
    {
        vhMem readData;
        vhReadTextureSlow( tex, 0, f, &readData );
        vhFinish();
        uint8_t expected = ( f == 3 ) ? 0xAA : ( uint8_t ) ( f + 10 );
        for ( size_t i = 0; i < faceSize; ++i )
        {
            EXPECT_EQ( readData[i], expected );
            if ( readData[i] != expected ) break;
        }
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST_F( Texture, Type_3D )
{


    const int w = 32, h = 32, d = 4;
    const size_t totalSize = w * h * d; // R8_UINT

    vhTexture tex = vhAllocTexture();
    vhCreateTexture3D( tex, glm::ivec3( w, h, d ), 1, nvrhi::Format::R8_UINT );

    // Action: Update full volume with gradient
    auto data = vhAllocMem( totalSize );
    for ( size_t i = 0; i < totalSize; ++i ) ( *data )[i] = ( uint8_t ) ( i % 256 );
    vhUpdateTexture( tex, 0, 0, 1, 1, data );
    vhFinish();

    // Verify: Readback (Slow path reads depth slices as array layers for 3D)
    // 3D texture readback not supported yet. This test doesnt work.
    /*vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), totalSize );
    for ( size_t i = 0; i < totalSize; ++i )
    {
        EXPECT_EQ( readData[i], ( uint8_t )( i % 256 ) );
        if ( readData[i] != ( uint8_t )( i % 256 ) ) break;
    }*/

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST_F( Texture, MipChain )
{


    const int dim = 32;
    const int mips = 4; // 32, 16, 8, 4
    const nvrhi::Format fmt = nvrhi::Format::R8_UINT;

    // Calculate total size
    size_t totalSize = 0;
    std::vector<size_t> mipSizes;
    for ( int i = 0; i < mips; ++i )
    {
        int mDim = std::max( 1, dim >> i );
        size_t s = ( size_t ) mDim * mDim;
        mipSizes.push_back( s );
        totalSize += s;
    }

    vhTexture tex = vhAllocTexture();
    vhCreateTexture2D( tex, glm::ivec2( dim, dim ), mips, fmt );

    // Action 1: Update all mips in one call
    auto fullData = vhAllocMem( totalSize );
    size_t offset = 0;
    for ( int i = 0; i < mips; ++i )
    {
        std::fill( fullData->begin() + offset, fullData->begin() + offset + mipSizes[i], ( uint8_t ) ( i + 1 ) );
        offset += mipSizes[i];
    }
    vhUpdateTexture( tex, 0, 0, mips, 1, fullData );
    vhFinish();

    // Verify 1
    for ( int i = 0; i < mips; ++i )
    {
        vhMem readData;
        vhReadTextureSlow( tex, i, 0, &readData );
        vhFinish();
        ASSERT_EQ( readData.size(), mipSizes[i] );
        for ( size_t j = 0; j < mipSizes[i]; ++j )
        {
            EXPECT_EQ( readData[j], ( uint8_t ) ( i + 1 ) );
            if ( readData[j] != ( uint8_t ) ( i + 1 ) ) break;
        }
    }

    // Action 2: Update only Mip 2 with 0x77
    auto mip2Data = vhAllocMem( mipSizes[2] );
    std::fill( mip2Data->begin(), mip2Data->end(), 0x77 );
    vhUpdateTexture( tex, 2, 0, 1, 1, mip2Data );
    vhFinish();

    // Verify 2
    for ( int i = 0; i < mips; ++i )
    {
        vhMem readData;
        vhReadTextureSlow( tex, i, 0, &readData );
        vhFinish();
        uint8_t expected = ( i == 2 ) ? 0x77 : ( uint8_t ) ( i + 1 );
        for ( size_t j = 0; j < mipSizes[i]; ++j )
        {
            EXPECT_EQ( readData[j], expected );
            if ( readData[j] != expected ) break;
        }
    }

    vhDestroyTexture( tex );
    vhFlush();
}

UTEST_F( Texture, Type_1D )
{


    const int width = 256;
    vhTexture tex = vhAllocTexture();
    vhCreateTexture( tex, nvrhi::TextureDimension::Texture1D, glm::ivec3( width, 1, 1 ), 1, 1, nvrhi::Format::R8_UINT );

    auto data = vhAllocMem( width );
    for ( int i = 0; i < width; ++i ) ( *data )[i] = ( uint8_t ) i;
    vhUpdateTexture( tex, 0, 0, 1, 1, data );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( tex, 0, 0, &readData );
    vhFinish();

    ASSERT_EQ( readData.size(), ( size_t ) width );
    for ( int i = 0; i < width; ++i )
    {
        EXPECT_EQ( readData[i], ( uint8_t ) i );
        if ( readData[i] != ( uint8_t ) i ) break;
    }

    vhDestroyTexture( tex );
    vhFlush();
}


UTEST( Sampler, GetSamplerDesc )
{
    // Case 1: Default/Zero Flags
    {
        uint64_t flags = 0;
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );

        EXPECT_TRUE( desc.minFilter );
        EXPECT_TRUE( desc.magFilter );
        EXPECT_TRUE( desc.mipFilter );
        EXPECT_EQ( desc.addressU, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_EQ( desc.addressV, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_EQ( desc.addressW, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_NEAR( desc.maxAnisotropy, 1.0f, 1e-5f );
        EXPECT_NEAR( desc.mipBias, 0.0f, 1e-5f );
        EXPECT_NEAR( desc.borderColor.r, 0.0f, 1e-5f );
        EXPECT_NEAR( desc.borderColor.a, 0.0f, 1e-5f );
        EXPECT_EQ( desc.reductionType, nvrhi::SamplerReductionType::Standard );
    }

    // Case 2: Point Sampling & Clamp
    {
        uint64_t flags = VRHI_SAMPLER_POINT | VRHI_SAMPLER_UVW_CLAMP;
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );

        EXPECT_FALSE( desc.minFilter );
        EXPECT_FALSE( desc.magFilter );
        EXPECT_FALSE( desc.mipFilter );
        EXPECT_EQ( desc.addressU, nvrhi::SamplerAddressMode::Clamp );
        EXPECT_EQ( desc.addressV, nvrhi::SamplerAddressMode::Clamp );
        EXPECT_EQ( desc.addressW, nvrhi::SamplerAddressMode::Clamp );
    }

    // Case 3: Anisotropy & MipBias
    {
        uint64_t flags = VRHI_SAMPLER_ANISOTROPY_16 | VRHI_SAMPLER_MIPBIAS( 2.5f );
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );

        EXPECT_NEAR( desc.maxAnisotropy, 16.0f, 1e-5f );
        EXPECT_NEAR( desc.mipBias, 2.5f, 0.01f );
    }

    // Case 4: Complex Mixed State
    {
        uint64_t flags = VRHI_SAMPLER_U_MIRROR | VRHI_SAMPLER_V_BORDER | VRHI_SAMPLER_MAG_POINT | VRHI_SAMPLER_MIPBIAS( -0.5f );
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );

        EXPECT_EQ( desc.addressU, nvrhi::SamplerAddressMode::Mirror );
        EXPECT_EQ( desc.addressV, nvrhi::SamplerAddressMode::Border );
        EXPECT_EQ( desc.addressW, nvrhi::SamplerAddressMode::Wrap );
        EXPECT_FALSE( desc.magFilter );
        EXPECT_TRUE( desc.minFilter );
        EXPECT_NEAR( desc.mipBias, -0.5f, 0.01f );
    }

    // Case 5: Reduction/Compare
    {
        uint64_t flags = VRHI_SAMPLER_COMPARE_LESS;
        nvrhi::SamplerDesc desc = vhGetSamplerDesc( flags );

        EXPECT_EQ( desc.reductionType, nvrhi::SamplerReductionType::Comparison );
    }
}

UTEST( ResourceQueries, Texture )
{


    vhTexture tex = vhAllocTexture();
    glm::ivec2 dims( 128, 64 );
    nvrhi::Format fmt = nvrhi::Format::RGBA8_UNORM;
    vhCreateTexture2D( tex, dims, 1, fmt );
    vhFlush();

    std::vector< vhTextureMipInfo > mipInfo;
    vhTexInfo info = vhGetTextureInfo( tex, &mipInfo );

    EXPECT_EQ( info.dimensions, glm::ivec3( dims, 1 ) );
    EXPECT_EQ( info.format, fmt );
    EXPECT_EQ( mipInfo.size(), 1 );
    EXPECT_EQ( mipInfo[0].dimensions, glm::ivec3( dims, 1 ) );

    void* handle = vhGetTextureNvrhiHandle( tex );
    EXPECT_NE( handle, nullptr );

    vhDestroyTexture( tex );
    vhFlush();

    // Query after destruction should return null/default
    info = vhGetTextureInfo( tex );
    EXPECT_EQ( info.format, nvrhi::Format::UNKNOWN );
    EXPECT_EQ( vhGetTextureNvrhiHandle( tex ), nullptr );
}

UTEST( Sampler, Hashing )
{
    nvrhi::SamplerDesc d1;
    d1.addressU = nvrhi::SamplerAddressMode::Clamp;
    
    nvrhi::SamplerDesc d2;
    d2.addressU = nvrhi::SamplerAddressMode::Wrap;

    nvrhi::SamplerDesc d3;
    d3.addressU = nvrhi::SamplerAddressMode::Clamp;

    uint64_t h1 = vhHashSamplerDesc( d1 );
    uint64_t h2 = vhHashSamplerDesc( d2 );
    uint64_t h3 = vhHashSamplerDesc( d3 );

    EXPECT_NE( h1, h2 );
    EXPECT_EQ( h1, h3 );
}

UTEST( Sampler, HandleCaching )
{
    if ( !g_testInit )
    {
        vhInit( g_testInitQuiet );
        g_testInit = true;
    }

    // Same flags should return same handle (pointer equality)
    nvrhi::SamplerHandle s1 = vhGetSamplerHandle( VRHI_SAMPLER_UVW_CLAMP );
    nvrhi::SamplerHandle s2 = vhGetSamplerHandle( VRHI_SAMPLER_UVW_CLAMP );
    
    EXPECT_NE( s1, nullptr );
    EXPECT_EQ( s1, s2 );

    // Different flags should return different handle
    nvrhi::SamplerHandle s3 = vhGetSamplerHandle( VRHI_SAMPLER_UVW_WRAP );
    EXPECT_NE( s1, s3 );
}

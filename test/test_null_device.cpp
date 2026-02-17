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

#include <vrhi_internal.h>
#include "test.h"
#include <vrhi.h>

UTEST( NullDevice, InitShutdown )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    vhInit( true );

    EXPECT_NE( g_vhDevice.Get(), nullptr );
    EXPECT_TRUE( g_vhNullMode );
    EXPECT_TRUE( g_vhInit.headless );
    EXPECT_EQ( g_vhDeviceInfo.name, "NULL Device" );

    vhShutdown( true );

    EXPECT_EQ( g_vhDevice.Get(), nullptr );
    EXPECT_FALSE( g_vhNullMode );
}

UTEST( NullDevice, ResourceCreation )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    vhInit( true );

    // Texture
    vhTexture tex = vhAllocTexture();
    EXPECT_NE( tex, VRHI_INVALID_HANDLE );
    vhCreateTexture2D( tex, "null_tex", glm::ivec2( 64, 64 ), 1, nvrhi::Format::RGBA8_UNORM );
    
    // Buffer
    vhBuffer buf = vhAllocBuffer();
    EXPECT_NE( buf, VRHI_INVALID_HANDLE );
    vhCreateStorageBuffer( buf, "null_buf", nullptr, 1024 );

    vhFlush();

    vhDestroyTexture( tex );
    vhDestroyBuffer( buf );

    vhFlush();

    vhShutdown( true );
}

UTEST( NullDevice, FrameLoop )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    vhInit( true );

    for ( int i = 0; i < 10; i++ )
    {
        bool ok = vhFrame();
        EXPECT_TRUE( ok );
    }

    vhShutdown( true );
}

UTEST( NullDevice, Resize )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 1280, 720 );
    vhInit( true );

    EXPECT_EQ( vhGetWindowSize().x, 1280 );
    EXPECT_EQ( vhGetWindowSize().y, 720 );

    vhResize( 1920, 1080 );
    EXPECT_EQ( vhGetWindowSize().x, 1920 );
    EXPECT_EQ( vhGetWindowSize().y, 1080 );

    vhShutdown( true );
}

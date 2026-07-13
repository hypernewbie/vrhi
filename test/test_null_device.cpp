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
#include <set>
#include <vector>

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

    g_vhInit = g_testInitDefaults;
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

    g_vhInit = g_testInitDefaults;
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

    g_vhInit = g_testInitDefaults;
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

    // Restore defaults so subsequent tests don't inherit nullMode.
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, CreateDestroy )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 1280, 720 );
    vhInit( true );
    size_t textureIDCount = g_vhTextureIDValid.size();

    vhSwapchainID sc1 = vhCreateSwapchain( nullptr, nullptr, 640, 480 );
    EXPECT_NE( sc1, VRHI_INVALID_SWAPCHAIN );

    vhSwapchainID sc2 = vhCreateSwapchain( nullptr, nullptr, 320, 200 );
    EXPECT_NE( sc2, VRHI_INVALID_SWAPCHAIN );
    EXPECT_NE( sc1, sc2 );
    EXPECT_EQ( g_vhTextureIDValid.size(), textureIDCount + 2 );

    EXPECT_EQ( vhGetSwapchainSize( sc1 ).x, 640 );
    EXPECT_EQ( vhGetSwapchainSize( sc1 ).y, 480 );
    EXPECT_EQ( vhGetSwapchainSize( sc2 ).x, 320 );
    EXPECT_EQ( vhGetSwapchainSize( sc2 ).y, 200 );

    EXPECT_EQ( vhGetSwapchainSize( g_vhPrimarySwapchain ).x, 1280 );
    EXPECT_EQ( vhGetSwapchainSize( g_vhPrimarySwapchain ).y, 720 );

    // Backbuffer handle exists (headless backbuffer is non-invalid in null mode per swapchain).
    EXPECT_NE( vhGetSwapchainBackbuffer( sc1 ), VRHI_INVALID_HANDLE );
    EXPECT_NE( vhGetSwapchainBackbuffer( sc2 ), VRHI_INVALID_HANDLE );

    // Resize one, confirm only that one changed.
    vhResizeSwapchain( sc1, 1024, 768 );
    EXPECT_EQ( vhGetSwapchainSize( sc1 ).x, 1024 );
    EXPECT_EQ( vhGetSwapchainSize( sc2 ).x, 320 );

    // Minimized state.
    vhResizeSwapchain( sc2, 0, 0 );
    EXPECT_EQ( vhGetSwapchainBackbuffer( sc2 ), VRHI_INVALID_HANDLE );

    // Present is a no-op on null/headless/minimized swapchains and returns true.
    EXPECT_TRUE( vhPresentSwapchain( sc1 ) );
    EXPECT_TRUE( vhPresentSwapchain( sc2 ) );

    // Primary swapchain refuses to be destroyed.
    vhDestroySwapchain( g_vhPrimarySwapchain );
    EXPECT_EQ( vhGetSwapchainSize( g_vhPrimarySwapchain ).x, 1280 );

    vhDestroySwapchain( sc1 );
    vhDestroySwapchain( sc2 );
    EXPECT_EQ( g_vhTextureIDValid.size(), textureIDCount );
    EXPECT_EQ( vhGetSwapchainSize( sc1 ).x, 0 );

    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, IDsAreUniqueAndMonotonic )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 640, 480 );
    vhInit( true );

    std::set< vhSwapchainID > ids;
    for ( int i = 0; i < 100; ++i )
    {
        vhSwapchainID id = vhCreateSwapchain( nullptr, nullptr, 16, 16 );
        EXPECT_NE( id, VRHI_INVALID_SWAPCHAIN );
        EXPECT_GT( id, g_vhPrimarySwapchain );
        ids.insert( id );
    }
    EXPECT_EQ( ( int ) ids.size(), 100 );

    for ( auto id : ids )
        vhDestroySwapchain( id );
    EXPECT_EQ( ( int ) g_vhSwapchains.size(), 1 );

    vhSwapchainID recycled = vhCreateSwapchain( nullptr, nullptr, 16, 16 );
    EXPECT_GT( recycled, *ids.rbegin() );

    vhDestroySwapchain( recycled );
    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, InvalidIDAndNonExistentAreSafe )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 320, 200 );
    vhInit( true );

    size_t sizeBefore = g_vhSwapchains.size();

    EXPECT_EQ( vhGetSwapchainSize( VRHI_INVALID_SWAPCHAIN ).x, 0 );
    EXPECT_EQ( vhGetSwapchainSize( VRHI_INVALID_SWAPCHAIN ).y, 0 );
    EXPECT_EQ( vhGetSwapchainSize( 99999 ).x, 0 );

    EXPECT_EQ( vhGetSwapchainBackbuffer( VRHI_INVALID_SWAPCHAIN ), VRHI_INVALID_HANDLE );
    EXPECT_EQ( vhGetSwapchainBackbuffer( 99999 ), VRHI_INVALID_HANDLE );

    EXPECT_FALSE( vhPresentSwapchain( VRHI_INVALID_SWAPCHAIN ) );
    EXPECT_FALSE( vhPresentSwapchain( 99999 ) );

    vhResizeSwapchain( VRHI_INVALID_SWAPCHAIN, 100, 100 );
    vhResizeSwapchain( 99999, 100, 100 );
    vhDestroySwapchain( VRHI_INVALID_SWAPCHAIN );
    vhDestroySwapchain( 99999 );

    EXPECT_EQ( g_vhSwapchains.size(), sizeBefore );

    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, ReInit )
{
    TestEnsureShutdown();

    for ( int i = 0; i < 2; ++i )
    {
        g_vhInit = vhInitData{};
        g_vhInit.nullMode = true;
        g_vhInit.resolution = glm::ivec2( 800, 600 );
        vhInit( true );

        EXPECT_NE( g_vhPrimarySwapchain, VRHI_INVALID_SWAPCHAIN );
        EXPECT_EQ( vhGetSwapchainSize( g_vhPrimarySwapchain ).x, 800 );
        EXPECT_NE( vhGetSwapchainBackbuffer( g_vhPrimarySwapchain ), VRHI_INVALID_HANDLE );
        EXPECT_EQ( ( int ) g_vhSwapchains.size(), 1 );

        vhShutdown( true );
    }

    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, MinimizedRoundTrip )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 320, 200 );
    vhInit( true );

    vhSwapchainID sc = vhCreateSwapchain( nullptr, nullptr, 0, 0 );
    EXPECT_NE( sc, VRHI_INVALID_SWAPCHAIN );
    EXPECT_EQ( vhGetSwapchainSize( sc ).x, 0 );
    EXPECT_TRUE( vhPresentSwapchain( sc ) );

    vhResizeSwapchain( sc, 800, 600 );
    EXPECT_EQ( vhGetSwapchainSize( sc ).x, 800 );
    EXPECT_EQ( vhGetSwapchainSize( sc ).y, 600 );
    EXPECT_TRUE( vhPresentSwapchain( sc ) );

    vhResizeSwapchain( sc, 0, 0 );
    EXPECT_EQ( vhGetSwapchainSize( sc ).x, 0 );
    EXPECT_TRUE( vhPresentSwapchain( sc ) );

    vhDestroySwapchain( sc );
    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, PresentIndependentOfPrimaryPacing )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 320, 200 );
    vhInit( true );

    uint64_t frameCountBefore = g_vhFrameCount.load();

    vhSwapchainID sc = vhCreateSwapchain( nullptr, nullptr, 128, 128 );

    for ( int i = 0; i < 5; ++i )
        EXPECT_TRUE( vhPresentSwapchain( sc ) );

    EXPECT_EQ( g_vhFrameCount.load(), frameCountBefore );

    for ( int i = 0; i < 3; ++i )
        EXPECT_TRUE( vhFrame() );
    EXPECT_EQ( g_vhFrameCount.load(), frameCountBefore + 3 );

    vhDestroySwapchain( sc );
    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, UseAfterReInit )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 320, 200 );
    vhInit( true );

    vhSwapchainID stale = vhCreateSwapchain( nullptr, nullptr, 64, 64 );
    ASSERT_NE( stale, VRHI_INVALID_SWAPCHAIN );

    vhShutdown( true );

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 640, 480 );
    vhInit( true );

    EXPECT_EQ( g_vhSwapchains.size(), ( size_t ) 1 );
    EXPECT_NE( g_vhPrimarySwapchain, VRHI_INVALID_SWAPCHAIN );

    EXPECT_EQ( vhGetSwapchainSize( stale ).x, 0 );
    EXPECT_EQ( vhGetSwapchainBackbuffer( stale ), VRHI_INVALID_HANDLE );
    EXPECT_FALSE( vhPresentSwapchain( stale ) );
    vhResizeSwapchain( stale, 100, 100 );
    EXPECT_EQ( g_vhSwapchains.size(), ( size_t ) 1 );

    vhSwapchainID fresh = vhCreateSwapchain( nullptr, nullptr, 100, 100 );
    EXPECT_NE( fresh, VRHI_INVALID_SWAPCHAIN );
    EXPECT_NE( fresh, stale );

    vhDestroySwapchain( fresh );
    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, QueryOnEveryInvalidPathDoesNotInsert )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 320, 200 );
    vhInit( true );

    size_t sizeBefore = g_vhSwapchains.size();

    vhSwapchainID bogus[] = { VRHI_INVALID_SWAPCHAIN, 0, 99999, ( vhSwapchainID ) -1, UINT64_MAX - 1 };
    for ( vhSwapchainID id : bogus )
    {
        EXPECT_EQ( vhGetSwapchainSize( id ).x, 0 );
        EXPECT_EQ( vhGetSwapchainSize( id ).y, 0 );
        EXPECT_EQ( vhGetSwapchainBackbuffer( id ), VRHI_INVALID_HANDLE );
        EXPECT_FALSE( vhPresentSwapchain( id ) );
        vhResizeSwapchain( id, 100, 100 );
        vhDestroySwapchain( id );
    }

    EXPECT_EQ( g_vhSwapchains.size(), sizeBefore );

    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

UTEST( Swapchain, ManySecondariesPerFrameSlot )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.nullMode = true;
    g_vhInit.resolution = glm::ivec2( 320, 200 );
    vhInit( true );

    uint64_t frameCountBefore = g_vhFrameCount.load();

    std::vector< vhSwapchainID > scs;
    for ( int i = 0; i < 8; ++i )
    {
        vhSwapchainID id = vhCreateSwapchain( nullptr, nullptr, 64 + i, 64 + i );
        ASSERT_NE( id, VRHI_INVALID_SWAPCHAIN );
        scs.push_back( id );
    }
    EXPECT_EQ( ( int ) g_vhSwapchains.size(), 9 );

    for ( int f = 0; f < 4; ++f )
    {
        for ( vhSwapchainID id : scs )
            EXPECT_TRUE( vhPresentSwapchain( id ) );
        EXPECT_TRUE( vhFrame() );
    }

    EXPECT_EQ( g_vhFrameCount.load(), frameCountBefore + 4 );
    EXPECT_EQ( ( int ) g_vhSwapchains.size(), 9 );

    for ( vhSwapchainID id : scs )
        vhDestroySwapchain( id );
    EXPECT_EQ( ( int ) g_vhSwapchains.size(), 1 );

    vhShutdown( true );
    g_vhInit = g_testInitDefaults;
}

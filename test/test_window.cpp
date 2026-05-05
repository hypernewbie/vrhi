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

#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif

#define RGFW_IMPLEMENTATION

#if defined(__linux__)
    #include <X11/Xlib.h>
    #include <X11/Xresource.h>
#endif

#include "RGFW.h"

#if defined(__linux__)
    // Undefine X11 macros that conflict with nvrhi enums
    #undef None
    #undef Always
    #undef TileShape
#endif

#include "test.h"
#include <vrhi.h>
#include <vrhi_internal.h>

UTEST( Window, SwapchainClear )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Window tests not supported in null/headless mode" );
    }
#if defined(__linux__)
    {
        const char* display = std::getenv( "DISPLAY" );
        if ( display == nullptr || display[0] == '\0' )
        {
            UTEST_SKIP( "No DISPLAY detected, skipping window test" );
        }
    }
#endif

#if defined(__APPLE__)
    // On macOS in terminal/CI environments, window creation may hang or fail
    if ( std::getenv( "CI" ) != nullptr || std::getenv( "GITHUB_ACTIONS" ) != nullptr )
    {
        UTEST_SKIP( "CI environment detected, skipping window test" );
    }
#endif

    // Create RGFW Window
    RGFW_window* win = RGFW_createWindow( "VRHI Test", 128, 128, 128, 128, RGFW_windowNoResize );
    ASSERT_TRUE( win != nullptr );

    // Get Native Handles
    void* nativeWindow = nullptr;
    void* nativeDisplay = nullptr;

#if defined(_WIN32)
    nativeWindow = RGFW_window_getHWND( win );
#elif defined(__linux__)
    nativeWindow = ( void* ) RGFW_window_getWindow_X11( win );
    nativeDisplay = RGFW_getDisplay_X11();
#elif defined(__APPLE__)
    void* layer = RGFW_getLayer_OSX();
    RGFW_window_setLayer_OSX( win, layer );
    nativeWindow = layer;
#endif

    // Re-init VRHI with window
    TestEnsureShutdown();
    g_vhInit.resolution = { 128, 128 };
    g_vhInit.headless = false;
    g_vhInit.windowHandle = nativeWindow;
    g_vhInit.displayHandle = nativeDisplay;
    g_vhInit.vsync = true;

    vhInit( false );
    g_testInit = true; // Mark as initialized so TestEnsureShutdown cleans it up

    // Verify
    vhTexture backBuffer = vhGetBackbuffer();
    ASSERT_TRUE( backBuffer != VRHI_INVALID_HANDLE );

    glm::uvec2 size = vhGetWindowSize();
    ASSERT_EQ( size.x, 128 );
    ASSERT_EQ( size.y, 128 );

    // Fill with Magenta loop
    // In a real test we might grab a screenshot, but for now we just verify it runs and presents.
    for ( int i = 0; i < 3; ++i ) 
    {
        // Poll events
        RGFW_event event;
        RGFW_window_checkEvent( win, &event );
        if ( RGFW_window_shouldClose( win ) )
            break;

        // Prepare Frame
        backBuffer = vhGetBackbuffer();

        // Clear
        vhBeginMarker( "Clear" );
        vhState state;
        state.SetColourAttachment( 0, backBuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
            .SetViewRect( glm::vec4( 0, 0, 128, 128 ) )
            .SetViewScissor( glm::vec4( 0, 0, 128, 128 ) )
            .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 1.0f, 0.0f, 1.0f, 1.0f ) );
        
        vhSetState( 0, state ); 
        
        vhClear( 0, VRHI_CLEAR_COLOR );
        vhEndMarker();

        if ( !vhFrame() ) break;
    }

    // Reset state ID 0 to avoid polluting other tests
    vhState s;
    vhSetState( 0, s.DirtyAll() );

    // Shut down VRHI (and hence Vulkan surface/swapchain) before destroying the
    // X11 window. Otherwise the next test's TestEnsureShutdown() will try to
    // destroy a Vulkan surface that references an already-freed Window, which
    // triggers a buffer overflow inside mesa WSI / libxcb during cleanup.
    TestEnsureShutdown();

    RGFW_window_close( win );
}

UTEST( Window, ResizeSwapchain )
{
    if ( g_vhInit.nullMode )
    {
        UTEST_SKIP( "Window tests not supported in null/headless mode" );
    }
#if defined(__linux__)
    {
        const char* display = std::getenv( "DISPLAY" );
        if ( display == nullptr || display[0] == '\0' )
        {
            UTEST_SKIP( "No DISPLAY detected, skipping window test" );
        }
    }
#endif

#if defined(__APPLE__)
    // On macOS in terminal/CI environments, window creation may hang or fail
    if ( std::getenv( "CI" ) != nullptr || std::getenv( "GITHUB_ACTIONS" ) != nullptr )
    {
        UTEST_SKIP( "CI environment detected, skipping window test" );
    }
#endif

    // Create RGFW Window with allow resize flag
    RGFW_window* win = RGFW_createWindow( "VRHI Test Resize", 128, 128, 128, 128, 0 );
    ASSERT_TRUE( win != nullptr );

    // Get Native Handles
    void* nativeWindow = nullptr;
    void* nativeDisplay = nullptr;

#if defined(_WIN32)
    nativeWindow = RGFW_window_getHWND( win );
#elif defined(__linux__)
    nativeWindow = ( void* ) RGFW_window_getWindow_X11( win );
    nativeDisplay = RGFW_getDisplay_X11();
#elif defined(__APPLE__)
    void* layer = RGFW_getLayer_OSX();
    RGFW_window_setLayer_OSX( win, layer );
    nativeWindow = layer;
#endif

    // Re-init VRHI with window
    TestEnsureShutdown();
    g_vhInit.resolution = { 128, 128 };
    g_vhInit.headless = false;
    g_vhInit.windowHandle = nativeWindow;
    g_vhInit.displayHandle = nativeDisplay;
    g_vhInit.vsync = true;

    vhInit( false );
    g_testInit = true;

    // Render a few frames
    for ( int i = 0; i < 3; ++i )
    {
        RGFW_event event;
        RGFW_window_checkEvent( win, &event );
        if ( RGFW_window_shouldClose( win ) )
            break;

        vhTexture backBuffer = vhGetBackbuffer();
        vhState state;
        state.SetColourAttachment( 0, backBuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
            .SetViewRect( glm::vec4( 0, 0, 128, 128 ) )
            .SetViewScissor( glm::vec4( 0, 0, 128, 128 ) )
            .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0.0f, 1.0f, 0.0f, 1.0f ) );

        vhSetState( 0, state );
        vhClear( 0, VRHI_CLEAR_COLOR );

        if ( !vhFrame() ) break;
    }

    // Resize OS window to 256x256. On X11 the resize is async; pump events and query
    // the surface capabilities until they reflect the new size before calling vhResize.
    RGFW_window_resize( win, 256, 256 );
#if defined(__linux__)
    {
        Display* dpy = (Display*)nativeDisplay;
        XSync( dpy, False );
        // Poll until VkSurfaceCapabilitiesKHR.currentExtent matches the new size, up to 1 second.
        VkSurfaceCapabilitiesKHR caps{};
        for ( int retry = 0; retry < 100; ++retry )
        {
            RGFW_event evt;
            RGFW_window_checkEvent( win, &evt );
            XSync( dpy, False );
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR( g_vulkanPhysicalDevice, g_vhSurface, &caps );
            if ( caps.currentExtent.width == 256 && caps.currentExtent.height == 256 )
                break;
            usleep( 10000 ); // 10ms
        }
    }
#endif

    // Resize to 256x256
    vhResize( 256, 256 );

    // Verify size changed
    glm::uvec2 newSize = vhGetWindowSize();
    ASSERT_EQ( newSize.x, 256 );
    ASSERT_EQ( newSize.y, 256 );

    // Verify vhGetWindowSize returns same
    glm::uvec2 windowSize = vhGetWindowSize();
    ASSERT_EQ( windowSize.x, 256 );
    ASSERT_EQ( windowSize.y, 256 );

    // Render a few more frames with new size
    for ( int i = 0; i < 3; ++i )
    {
        RGFW_event event;
        RGFW_window_checkEvent( win, &event );
        if ( RGFW_window_shouldClose( win ) )
            break;

        vhTexture backBuffer = vhGetBackbuffer();
        vhState state;
        state.SetColourAttachment( 0, backBuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
            .SetViewRect( glm::vec4( 0, 0, 256, 256 ) )
            .SetViewScissor( glm::vec4( 0, 0, 256, 256 ) )
            .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0.0f, 0.0f, 1.0f, 1.0f ) );

        vhSetState( 0, state );
        vhClear( 0, VRHI_CLEAR_COLOR );

        if ( !vhFrame() ) break;
    }

    // Resize OS window back to 128x128 before resizing swapchain.
    RGFW_window_resize( win, 128, 128 );
#if defined(__linux__)
    {
        Display* dpy = (Display*)nativeDisplay;
        XSync( dpy, False );
        VkSurfaceCapabilitiesKHR caps{};
        for ( int retry = 0; retry < 100; ++retry )
        {
            RGFW_event evt;
            RGFW_window_checkEvent( win, &evt );
            XSync( dpy, False );
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR( g_vulkanPhysicalDevice, g_vhSurface, &caps );
            if ( caps.currentExtent.width == 128 && caps.currentExtent.height == 128 )
                break;
            usleep( 10000 );
        }
    }
#endif

    // Resize back to original
    vhResize( 128, 128 );

    // Verify size restored
    newSize = vhGetWindowSize();
    ASSERT_EQ( newSize.x, 128 );
    ASSERT_EQ( newSize.y, 128 );

    // Render a final frame
    vhTexture backBuffer = vhGetBackbuffer();
    vhState state;
    state.SetColourAttachment( 0, backBuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
        .SetViewRect( glm::vec4( 0, 0, 128, 128 ) )
        .SetViewScissor( glm::vec4( 0, 0, 128, 128 ) )
        .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 1.0f, 1.0f, 0.0f, 1.0f ) );

    vhSetState( 0, state );
    vhClear( 0, VRHI_CLEAR_COLOR );
    vhFrame();

    // Reset state ID 0 to avoid polluting other tests
    vhState s;
    vhSetState( 0, s.DirtyAll() );

    // See SwapchainClear: shut down VRHI before destroying the window so the
    // Vulkan surface doesn't outlive the X11 Window it was created against.
    TestEnsureShutdown();

    RGFW_window_close( win );
}

UTEST( Window, FrameNumber )
{
    // Ensure clean state
    TestEnsureShutdown();
    g_vhInit.headless = true;
    vhInit( false );
    g_testInit = true;
 
    ASSERT_EQ( vhGetFrameNumber(), 0u );
 
    for ( uint64_t i = 0; i < 10; ++i )
    {
        EXPECT_EQ( vhGetFrameNumber(), i );
        vhFrame();
        EXPECT_EQ( vhGetFrameNumber(), i + 1 );
    }
}

UTEST( Window, HeadlessBackbuffer )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.headless = true;
    g_vhInit.nullMode = false;
    g_vhInit.resolution = glm::ivec2( 1280, 720 );
    vhInit( true );

    EXPECT_NE( vhGetBackbuffer(), VRHI_INVALID_HANDLE );
    EXPECT_EQ( vhGetWindowSize(), glm::uvec2( 1280, 720 ) );

    vhResize( 800, 600 );
    EXPECT_NE( vhGetBackbuffer(), VRHI_INVALID_HANDLE );
    EXPECT_EQ( vhGetWindowSize(), glm::uvec2( 800, 600 ) );

    vhShutdown( true );
}

UTEST( Window, HeadlessBackbufferRender )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.headless = true;
    g_vhInit.nullMode = false;
    g_vhInit.resolution = glm::ivec2( 640, 480 );
    vhInit( true );

    vhTexture backbuffer = vhGetBackbuffer();
    EXPECT_NE( backbuffer, VRHI_INVALID_HANDLE );

    vhState state;
    state.SetColourAttachment( 0, backbuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
        .SetViewRect( glm::vec4( 0, 0, 640, 480 ) )
        .SetViewScissor( glm::vec4( 0, 0, 640, 480 ) )
        .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 1.0f, 0.0f, 1.0f, 1.0f ) );

    bool ok = vhSetState( 0, state );
    EXPECT_TRUE( ok );

    vhClear( 0, VRHI_CLEAR_COLOR );
    vhFlush();

    vhShutdown( true );
}

UTEST( Window, HeadlessBackbufferBlit )
{
    TestEnsureShutdown();

    g_vhInit = vhInitData{};
    g_vhInit.headless = true;
    g_vhInit.nullMode = false;
    g_vhInit.resolution = glm::ivec2( 64, 64 );
    vhInit( true );
    g_testInit = true;

    vhTexture backbuffer = vhGetBackbuffer();
    ASSERT_NE( backbuffer, VRHI_INVALID_HANDLE );

    // Render a distinctive colour into the backbuffer (registered via RegisterInternalTexture).
    vhState state;
    state.SetColourAttachment( 0, backbuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
        .SetViewRect( glm::vec4( 0, 0, 64, 64 ) )
        .SetViewScissor( glm::vec4( 0, 0, 64, 64 ) )
        .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 1.0f, 0.0f, 0.0f, 1.0f ) );
    EXPECT_TRUE( vhSetState( 0, state ) );
    vhClear( 0, VRHI_CLEAR_COLOR );
    vhFinish();

    // Blit the backbuffer into an owned BGRA8 staging texture, then read that back. Direct
    // readback of the backbuffer is not supported (state-tracking caveat); blit-then-read is.
    vhTexture staging = vhAllocTexture();
    vhCreateTexture2D( staging, "BackbufferStaging", glm::ivec2( 64, 64 ), 1, nvrhi::Format::BGRA8_UNORM, VRHI_TEXTURE_NONE );
    vhBlitTexture( staging, backbuffer );
    vhFinish();

    vhMem readData;
    vhReadTextureSlow( staging, 0, 0, &readData );
    vhFinish();

    ASSERT_GE( readData.size(), ( size_t ) ( 64 * 64 * 4 ) );
    int32_t offset = ( 32 * 64 + 32 ) * 4; // BGRA byte order: B=0, G=0, R=255, A=255
    EXPECT_EQ( readData[offset + 0], 0 );
    EXPECT_EQ( readData[offset + 1], 0 );
    EXPECT_EQ( readData[offset + 2], 255 );
    EXPECT_EQ( readData[offset + 3], 255 );

    vhDestroyTexture( staging );
    vhFinish();

    // Reset state ID 0 to avoid polluting other tests
    vhState s;
    vhSetState( 0, s.DirtyAll() );
}

UTEST( Window, HeadlessBackbufferInitShutdownChurn )
{
    for ( int i = 0; i < 8; ++i )
    {
        TestEnsureShutdown();

        g_vhInit = vhInitData{};
        g_vhInit.headless = true;
        g_vhInit.nullMode = false;
        g_vhInit.resolution = glm::ivec2( 320 + ( i * 32 ), 240 + ( i * 16 ) );
        vhInit( true );

        vhTexture backbuffer = vhGetBackbuffer();
        EXPECT_NE( backbuffer, VRHI_INVALID_HANDLE );
        EXPECT_EQ( vhGetWindowSize(), glm::uvec2( g_vhInit.resolution ) );

        vhState state;
        state.SetColourAttachment( 0, backbuffer, 0, 0, nvrhi::Format::UNKNOWN, false )
            .SetViewRect( glm::vec4( 0, 0, g_vhInit.resolution.x, g_vhInit.resolution.y ) )
            .SetViewScissor( glm::vec4( 0, 0, g_vhInit.resolution.x, g_vhInit.resolution.y ) )
            .SetViewClear( VRHI_CLEAR_COLOR, glm::vec4( 0.0f, 0.25f * i, 1.0f, 1.0f ) );

        bool ok = vhSetState( 0, state );
        EXPECT_TRUE( ok );

        vhClear( 0, VRHI_CLEAR_COLOR );
        vhFlush();
        vhShutdown( true );
    }
}

UTEST( Window, ResizeDuringDraws )
{
#ifdef __APPLE__
    UTEST_SKIP( "vhResize during active command buffers triggers Metal device lost on MoltenVK" );
#endif
    TestEnsureShutdown();
    g_vhInit = vhInitData{};
    g_vhInit.headless = true;
    g_vhInit.resolution = glm::ivec2( 128, 128 );
    vhInit( true );
    g_testInit = true;

    vhTexture rt = vhAllocTexture();
    vhCreateTexture2D( rt, "ResizeTex", glm::ivec2(32,32), 1, nvrhi::Format::RGBA8_UNORM, VRHI_TEXTURE_RT );
    vhFinish();

    // Draw something, then resize mid-stream, then draw again — must not crash.
    for ( int i = 0; i < 3; i++ )
    {
        vhState state;
        state.SetColourAttachment(0, rt).SetViewRect(glm::vec4(0,0,32,32))
             .SetViewClear(VRHI_CLEAR_COLOR, glm::vec4(0));
        vhSetState(100, state);
        vhClear(100, VRHI_CLEAR_COLOR);
        vhFlush();

        glm::ivec2 newSize(64 + i * 32, 64 + i * 32);
        vhResize(newSize.x, newSize.y);
        EXPECT_EQ( vhGetWindowSize(), glm::uvec2(newSize) );
    }

    vhDestroyTexture(rt);
    vhFinish();
    vhShutdown(true);
    g_testInit = false;
}

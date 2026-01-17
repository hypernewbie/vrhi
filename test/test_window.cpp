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

UTEST( Window, SwapchainClear )
{
#if defined(__linux__)
    if ( std::getenv( "DISPLAY" ) == nullptr )
    {
        UTEST_SKIP( "No DISPLAY detected, skipping window test" );
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

    RGFW_window_close( win );
}

UTEST( Window, ResizeSwapchain )
{
#if defined(__linux__)
    if ( std::getenv( "DISPLAY" ) == nullptr )
    {
        UTEST_SKIP( "No DISPLAY detected, skipping window test" );
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

    // Resize OS window to 256x256 before resizing swapchain
    RGFW_window_resize( win, 256, 256 );

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

    // Resize OS window back to 128x128 before resizing swapchain
    RGFW_window_resize( win, 128, 128 );

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

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

#define RGFW_IMPLEMENTATION
#include "RGFW.h"

#include "test.h"
#include <vrhi.h>

UTEST( Window, SwapchainClear )
{
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
    nativeWindow = RGFW_window_getView_OSX( win );
    RGFW_window_setLayer_OSX( win, RGFW_getLayer_OSX() );
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

    glm::uvec2 size = vhGetBackbufferSize();
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

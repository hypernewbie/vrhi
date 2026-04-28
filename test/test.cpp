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

#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>
#include <random>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>
#endif // _WIN32
#include "test.h"
#include <vrhi.h>

#ifdef _WIN32
// Make assert/abort fail-fast in CI and headless runs instead of popping a blocking dialog.
static void vhTestSilenceWin32Dialogs()
{
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX );
    _set_abort_behavior( 0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
    _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
    _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR );
    _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE );
    _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );
    _CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE );
    _CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR );
}
#endif

UTEST( Vrhi, Dummy )
{
    ASSERT_TRUE( true );
}

bool g_testInit = false;
bool g_testInitQuiet = true;
bool g_captureActive = false;

void TestEnsureShutdown()
{
    if ( g_captureActive )
    {
        vhCaptureEnd();
        g_captureActive = false;
    }
    
    if ( g_testInit )
    {
        vhShutdown( g_testInitQuiet );
        g_testInit = false;
    }
}

UTEST_STATE();

int main( int argc, const char* const argv[] )
{
#ifdef _WIN32
    vhTestSilenceWin32Dialogs();
#endif

    // Parse command line arguments
    for ( int i = 1; i < argc; ++i )
    {
        if ( strcmp( argv[i], "--null" ) == 0 )
        {
            g_vhInit.nullMode = true;
            g_vhInit.headless = true;
        }
        else if ( strcmp( argv[i], "--headless" ) == 0 )
        {
            g_vhInit.headless = true;
        }
    }

#ifndef NDEBUG
    g_vhInit.debug = true;
    g_vhInit.renderdoc = true;
    g_vhInit.markers = true;
    g_vhInit.logBackendCmds = false;
    // g_vhInit.debugBlockWaitForBackend = true; // Enable this to test blocking backend mode.
#endif

    g_vhInit.forceShaderRecompile = true;

#ifdef _WIN32
    g_vhInit.shaderMakePath = "../tools/win_release";
    g_vhInit.shaderMakeSlangPath = "../tools/win_release";
#elif defined(__APPLE__)
    g_vhInit.shaderMakePath = "../tools/mac_release";
    g_vhInit.shaderMakeSlangPath = "../tools/mac_release";
#else
    g_vhInit.shaderMakePath = "../tools/linux_release";
    g_vhInit.shaderMakeSlangPath = "../tools/linux_release";
#endif

    int result = utest_main( argc, argv );
    TestEnsureShutdown();
    return result;
}

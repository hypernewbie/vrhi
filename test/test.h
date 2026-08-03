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

#pragma once

#undef __EXCEPTIONS

#if defined(_WIN32) && !defined(_WINDOWS_)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "utest.h"
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

extern bool g_captureActive;
extern bool g_testInit;
extern struct vhInitData g_testInitDefaults;
void TestEnsureShutdown();

// Returns true when running on a software Vulkan ICD (llvmpipe/lavapipe/SwiftShader/Microsoft Basic).
// Used to skip stress benchmarks that race on software command submission paths.
bool TestIsSoftwareVulkan();

// Captures vrhi log output so negative tests can assert an error was surfaced. Swallows what it
// captures. Backend thread messages land here too, so call vhFinish() before asserting.
struct TestLogCapture
{
    std::function< void( bool, const std::string& ) > previous;
    mutable std::mutex mutex;
    std::vector< std::string > messages;
    int errorCount = 0;

    TestLogCapture();
    ~TestLogCapture();
    TestLogCapture( const TestLogCapture& ) = delete;
    TestLogCapture& operator=( const TestLogCapture& ) = delete;

    bool Contains( const char* substr ) const;
    int ErrorCount() const;
    void Clear();
};

#if defined(__cplusplus) && (__cplusplus >= 201103L)
#ifdef __clang__

// Specialisation for GLM vec3 (int)
template <> struct utest_type_deducer< glm::ivec3, false >
{
    static void _( const glm::ivec3& v )
    {
        UTEST_PRINTF( "(%d, %d, %d)", v.x, v.y, v.z );
    }
};

// Specialisation for GLM vec2 (uint)
template <> struct utest_type_deducer< glm::uvec2, false >
{
    static void _( const glm::uvec2& v )
    {
        UTEST_PRINTF( "(%u, %u)", v.x, v.y );
    }
};

// Specialisation for GLM vec4 (float)
template <> struct utest_type_deducer< glm::vec4, false >
{
    static void _( const glm::vec4& v )
    {
        UTEST_PRINTF( "(%f, %f, %f, %f)", v.x, v.y, v.z, v.w );
    }
};

// Specialisation for GLM mat4
template <> struct utest_type_deducer< glm::mat4, false >
{
    static void _( const glm::mat4& m )
    {
        UTEST_PRINTF( "mat4(...)" );
    }
};

// Partial specialisation for nvrhi::RefCountPtr
template < typename T >
struct utest_type_deducer< nvrhi::RefCountPtr< T >, false >
{
    static void _( const nvrhi::RefCountPtr< T >& ptr )
    {
        UTEST_PRINTF( "%p", static_cast< void* >( ptr.Get() ) );
    }
};

// Specialisation for std::string
template <> struct utest_type_deducer< std::string, false >
{
    static void _( const std::string& s )
    {
        UTEST_PRINTF( "%s", s.c_str() );
    }
};

#endif // __clang__

#endif defined(__cplusplus) && (__cplusplus >= 201103L)

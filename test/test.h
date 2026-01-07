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

#include "utest.h"
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

// Specialisation for GLM vec3 (int)
template <> struct utest_type_deducer< glm::ivec3, false >
{
    static void _( const glm::ivec3& v )
    {
        UTEST_PRINTF( "(%d, %d, %d)", v.x, v.y, v.z );
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

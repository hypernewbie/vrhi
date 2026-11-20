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

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "test.h"
#include <vrhi_internal.h>

extern bool g_testInitQuiet;

struct BindingSetBench{};

UTEST_F_SETUP( BindingSetBench )
{
    if ( g_vhInit.nullMode ) { UTEST_SKIP( "Requires GPU" ); }
    const char* enabled = getenv( "VRHI_BENCH_BINDINGSET" );
    if ( !enabled || strcmp( enabled, "1" ) != 0 ) { UTEST_SKIP( "set VRHI_BENCH_BINDINGSET=1 to run the binding-set benchmark" ); }
    if ( !g_testInit ) { vhInit( g_testInitQuiet ); g_testInit = true; }
    vhFinish();
}

UTEST_F_TEARDOWN( BindingSetBench )
{
    vhFinish();
}

UTEST_F( BindingSetBench, CapacityMiss )
{
    constexpr uint32_t capacity = 4096;
    constexpr uint64_t stride = 256;
    const int32_t startErrors = g_vhErrorCounter.load();

    nvrhi::BufferDesc bufDesc;
    bufDesc.byteSize = ( capacity + 1 ) * stride;
    bufDesc.isConstantBuffer = true;
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.addItem( nvrhi::BindingLayoutItem::ConstantBuffer( 0 ) );
    nvrhi::BufferHandle buffer;
    nvrhi::BindingLayoutHandle layout;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        buffer = g_vhDevice->createBuffer( bufDesc );
        layout = g_vhDevice->createBindingLayout( layoutDesc );
    }
    ASSERT_NE( buffer, nullptr );
    ASSERT_NE( layout, nullptr );

    nvrhi::BindingSetDesc desc;
    desc.addItem( nvrhi::BindingSetItem::ConstantBuffer( 0, buffer.Get(), nvrhi::BufferRange( 0, stride ) ) );
    for ( int pair = 0; pair < 6; pair++ )
    {
        for ( int pass = 0; pass < 2; pass++ )
        {
            vhBindingSetCacheClear();
            const uint32_t count = ( pair + pass ) % 2 == 0 ? capacity - 1 : capacity;
            nvrhi::BindingSetHandle first, recent;
            for ( uint32_t i = 0; i < count; i++ )
            {
                desc.bindings[0].range = nvrhi::BufferRange( i * stride, stride );
                nvrhi::BindingSetHandle set = vhGetBindingSet( desc, layout.Get() );
                ASSERT_NE( set, nullptr );
                // Keep two handles alive to prevent address reuse after eviction.
                if ( i == 0 ) first = set;
                if ( i == capacity - 2 ) recent = set;
            }

            // Both passes time a miss for the same offset.
            desc.bindings[0].range = nvrhi::BufferRange( capacity * stride, stride );
            const auto start = std::chrono::steady_clock::now();
            nvrhi::BindingSetHandle miss = vhGetBindingSet( desc, layout.Get() );
            const auto end = std::chrono::steady_clock::now();
            ASSERT_NE( miss, nullptr );

            desc.bindings[0].range = nvrhi::BufferRange( 0, stride );
            nvrhi::BindingSetHandle firstAfter = vhGetBindingSet( desc, layout.Get() );
            ASSERT_NE( firstAfter, nullptr );
            desc.bindings[0].range = nvrhi::BufferRange( ( capacity - 2 ) * stride, stride );
            nvrhi::BindingSetHandle recentAfter = vhGetBindingSet( desc, layout.Get() );
            ASSERT_NE( recentAfter, nullptr );
            UTEST_PRINTF( "[VRHI_BENCH] BindingSet_CapacityMiss pair=%d seeded=%u miss_us=%.3f first_kept=%d recent_kept=%d\n",
                pair, count, std::chrono::duration< double, std::micro >( end - start ).count(),
                firstAfter.Get() == first.Get(), recentAfter.Get() == recent.Get() );
        }
    }
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
}

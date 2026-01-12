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

struct Buffer {};
UTEST_F_SETUP( Buffer )
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

UTEST_F_TEARDOWN( Buffer )
{
    vhEndMarker();
}

UTEST_F( Buffer, Transient )
{
    vhTransientBuffer tb;
    nvrhi::BufferDesc desc;
    desc.byteSize = 1024;
    desc.debugName = "TransientTest";
    desc.isConstantBuffer = true;

    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        tb.Init_DeviceStateLocked( desc );
    }

    // Verify Initialisation
    EXPECT_EQ( tb.size, 1024 );
    EXPECT_EQ( tb.offset, 0 );
    EXPECT_EQ( tb.frameIdx, 0u );
    for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT; ++i )
    {
        EXPECT_NE( tb.handle[i], nullptr );
    }

    // Test Allocation
    int64_t offset1 = tb.Alloc( 256 );
    EXPECT_EQ( offset1, 0 );
    EXPECT_EQ( tb.offset, 256 );

    int64_t offset2 = tb.Alloc( 256 );
    EXPECT_EQ( offset2, 256 );
    EXPECT_EQ( tb.offset, 512 );

    // Test Allocation Failure (Too large)
    int64_t offsetFail = tb.Alloc( 1000 );
    EXPECT_EQ( offsetFail, -1 );
    EXPECT_EQ( tb.offset, 512 ); // Should not change

    // Test Step
    tb.Step();
    EXPECT_EQ( tb.frameIdx, 1u );
    EXPECT_EQ( tb.offset, 0 );

    // Allocate again on new frame
    int64_t offset3 = tb.Alloc( 100 );
    EXPECT_EQ( offset3, 0 );
    EXPECT_EQ( tb.offset, 100 );

    // Shutdown
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        tb.Shutdown_DeviceStateLocked();
    }

    for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT; ++i )
    {
        EXPECT_EQ( tb.handle[i], nullptr );
    }
}

UTEST_F( Buffer, TransientWrite )
{
    vhTransientBuffer tb;
    nvrhi::BufferDesc desc;
    desc.byteSize = 1024;
    desc.debugName = "TransientWriteTest";
    desc.isConstantBuffer = true;
    desc.cpuAccess = nvrhi::CpuAccessMode::Write;

    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        tb.Init_DeviceStateLocked( desc );
    }

    uint32_t data[4] = { 1, 2, 3, 4 };
    
    // Test Write
    int64_t offset1 = tb.Write( data, sizeof( data ) );
    EXPECT_GE( offset1, 0 );
    EXPECT_EQ( tb.offset, sizeof( data ) );

    // Shutdown
    {
        std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
        tb.Shutdown_DeviceStateLocked();
    }
}


UTEST_F( Buffer, ValidateLayout )
{
    // Valid cases
    EXPECT_TRUE( vhValidateVertexLayout( "float3" ) ); // Implicit 0
    EXPECT_TRUE( vhValidateVertexLayout( "float3 float2" ) ); // Implicit 0, 1
    EXPECT_TRUE( vhValidateVertexLayout( "float3 ATTR5" ) ); // Explicit 5
    EXPECT_TRUE( vhValidateVertexLayout( "float3 ATTR0" ) ); // Explicit 0
    EXPECT_TRUE( vhValidateVertexLayout( "float3 ATTR0 float2" ) ); // Explicit 0, Implicit 1
    EXPECT_FALSE( vhValidateVertexLayout( "ubyte4" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "half2" ) );
    EXPECT_TRUE( vhValidateVertexLayout( "float" ) ); // Scalar

    // Invalid cases - Types
    EXPECT_FALSE( vhValidateVertexLayout( "double3" ) ); // Invalid type
    EXPECT_FALSE( vhValidateVertexLayout( "float5" ) );  // Invalid suffix
    EXPECT_FALSE( vhValidateVertexLayout( "float1" ) );  // Invalid suffix
    EXPECT_FALSE( vhValidateVertexLayout( "vec3" ) );    // Invalid type

    // Invalid cases - Formatting
    EXPECT_FALSE( vhValidateVertexLayout( "float3 ATTR" ) );      // Missing number
    EXPECT_FALSE( vhValidateVertexLayout( "float3 ATTRx" ) );     // Invalid number
    EXPECT_FALSE( vhValidateVertexLayout( "" ) );                 // Empty

    // Invalid cases - Collisions
    EXPECT_FALSE( vhValidateVertexLayout( "float3 float3 ATTR0" ) ); // Implicit 0, Explicit 0
    EXPECT_FALSE( vhValidateVertexLayout( "float3 ATTR5 float2 ATTR5" ) ); // Duplicate 5
}

UTEST_F( Buffer, VertexLayoutInternals )
{
    // Test 1: Simple Logic
    {
        std::vector< vhVertexLayoutDef > defs;
        bool res = vhParseVertexLayoutInternal( "float3 ATTR5", defs );
        EXPECT_TRUE( res );
        EXPECT_EQ( defs.size(), 1 );
        EXPECT_EQ( vhVertexLayoutDefSize( defs ), 12 );

        if ( defs.size() > 0 )
        {
            EXPECT_EQ( defs[0].format, nvrhi::Format::RGB32_FLOAT );
            EXPECT_EQ( defs[0].location, 5 ); // Explicit
            EXPECT_EQ( defs[0].offset, 0 );
            EXPECT_EQ( vhVertexLayoutDefSize( defs[0] ), 12 );
        }
    }

    // Test 2: Complex Logic
    {
        std::vector< vhVertexLayoutDef > defs;
        // float3 (loc 0), float2 (loc 1), short2 ATTR5 (loc 5 via explicit)
        bool res = vhParseVertexLayoutInternal( "float3 float2 short2 ATTR5", defs );
        EXPECT_TRUE( res );
        EXPECT_EQ( defs.size(), 3 );

        // float3 (12 bytes)
        EXPECT_EQ( defs[0].offset, 0 );
        EXPECT_EQ( defs[0].format, nvrhi::Format::RGB32_FLOAT );
        EXPECT_EQ( defs[0].location, 0 );

        // float2 (8 bytes) -> offset 12
        EXPECT_EQ( defs[1].offset, 12 );
        EXPECT_EQ( defs[1].format, nvrhi::Format::RG32_FLOAT );
        EXPECT_EQ( defs[1].location, 1 );

        // short2 (4 bytes) -> offset 20
        EXPECT_EQ( defs[2].offset, 20 );
        EXPECT_EQ( defs[2].format, nvrhi::Format::RG16_SINT ); // short2 -> RG16_SINT
        EXPECT_EQ( defs[2].location, 5 );

        // Total Stride = 24
        EXPECT_EQ( vhVertexLayoutDefSize( defs ), 24 );
    }
}

UTEST_F( Buffer, Allocation )
{
    vhBuffer b1 = vhAllocBuffer();
    vhBuffer b2 = vhAllocBuffer();
    vhBuffer b3 = vhAllocBuffer();

    EXPECT_NE( b1, VRHI_INVALID_HANDLE );
    EXPECT_NE( b2, VRHI_INVALID_HANDLE );
    EXPECT_NE( b3, VRHI_INVALID_HANDLE );

    EXPECT_NE( b1, b2 );
    EXPECT_NE( b2, b3 );
    EXPECT_NE( b1, b3 );

    vhDestroyBuffer( b1 );
    vhDestroyBuffer( b2 );
    vhDestroyBuffer( b3 );
    vhFlush();
}

UTEST_F( Buffer, UpdateSafety )
{

    vhFlush(); // Ensure clean state from previous tests
    int32_t startErrors = g_vhErrorCounter.load();

    // Invalid Handle
    vhUpdateVertexBuffer( VRHI_INVALID_HANDLE, nullptr, 0 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // Non-existent Buffer
    vhUpdateVertexBuffer( 0xDEADC0DE, nullptr, 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );
    startErrors = g_vhErrorCounter.load();

    // Null Data ( should error )
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "NullDataTest", vhAllocMem( 1024 ), "float3" );
    vhUpdateVertexBuffer( buf, nullptr, 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

    // Destroyed Buffer
    vhDestroyBuffer( buf );
    vhFlush();
    vhUpdateVertexBuffer( buf, vhAllocMem( 100 ), 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );
}

UTEST_F( Buffer, DoubleCreation )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "DoubleCreate", vhAllocMem( 1024 ), "float3" );
    vhCreateVertexBuffer( buf, "DoubleCreate2", vhAllocMem( 1024 ), "float3" );
    vhFlush();

    EXPECT_GT( g_vhErrorCounter.load(), startErrors );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST_F( Buffer, UpdateFunctionality )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "UpdateTest", vhAllocMem( 1024 ), "float3" );

    // Basic Update
    vhUpdateVertexBuffer( buf, vhAllocMem( 256 ), 0 );

    // Offset Update (512 bytes = ~42.67 vertices, round to 43 vertices for stride 12)
    vhUpdateVertexBuffer( buf, vhAllocMem( 100 ), 43 );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );
    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST_F( Buffer, Flags_Compute )
{
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer bRead = vhAllocBuffer();
    vhCreateVertexBuffer( bRead, "ComputeRead", vhAllocMem( 1024 ), "float3", 0, VRHI_BUFFER_COMPUTE_READ );

    vhBuffer bWrite = vhAllocBuffer();
    vhCreateVertexBuffer( bWrite, "ComputeWrite", vhAllocMem( 1024 ), "float3", 0, VRHI_BUFFER_COMPUTE_WRITE );

    vhBuffer bReadWrite = vhAllocBuffer();
    vhCreateVertexBuffer( bReadWrite, "ComputeReadWrite", vhAllocMem( 1024 ), "float3", 0, VRHI_BUFFER_COMPUTE_READ_WRITE );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bRead );
    vhDestroyBuffer( bWrite );
    vhDestroyBuffer( bReadWrite );
    vhFlush();
}

UTEST_F( Buffer, Flags_DrawIndirect )
{
    int32_t startErrors = g_vhErrorCounter.load();
    vhBuffer bIndirect = vhAllocBuffer();
    vhCreateVertexBuffer( bIndirect, "DrawIndirect", vhAllocMem( 1024 ), "float3", 0, VRHI_BUFFER_DRAW_INDIRECT );
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bIndirect );
    vhFlush();
}

UTEST_F( Buffer, Flags_Resize )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // Success case: ALLOW_RESIZE
    vhBuffer bResize = vhAllocBuffer();
    vhCreateVertexBuffer( bResize, "AllowResize", vhAllocMem( 64 ), "float3", 0, VRHI_BUFFER_ALLOW_RESIZE );

    // Update with larger data
    vhUpdateVertexBuffer( bResize, vhAllocMem( 128 ), 0 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // Failure case: No ALLOW_RESIZE
    vhBuffer bNoResize = vhAllocBuffer();
    vhCreateVertexBuffer( bNoResize, "NoResize", vhAllocMem( 64 ), "float3", 0, VRHI_BUFFER_NONE );

    // Update with larger data - should trigger error in backend
    vhUpdateVertexBuffer( bNoResize, vhAllocMem( 128 ), 0 );
    vhFlush();
    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bResize );
    vhDestroyBuffer( bNoResize );
    vhFlush();
}

UTEST_F( Buffer, NumVerts_CreateResize )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // Create Uninitialised
    vhBuffer buf = vhAllocBuffer();
    vhCreateVertexBuffer( buf, "UninitCreate", nullptr, "float3", 100, VRHI_BUFFER_ALLOW_RESIZE );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // Resize via numVerts
    vhUpdateVertexBuffer( buf, nullptr, 0, 200 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST_F( Buffer, IndexBuffer_Basic16 )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    EXPECT_NE( buf, VRHI_INVALID_HANDLE );

    const uint64_t kCount = 12;
    std::vector<uint16_t> indices = { 0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7 };
    size_t dataSize = indices.size() * sizeof( uint16_t );

    auto data = vhAllocMem( dataSize );
    memcpy( data->data(), indices.data(), dataSize );

    vhCreateIndexBuffer( buf, "Basic16", data, 0, VRHI_BUFFER_NONE );
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST_F( Buffer, IndexBuffer_Basic32 )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    vhBuffer buf = vhAllocBuffer();
    EXPECT_NE( buf, VRHI_INVALID_HANDLE );

    const uint64_t kCount = 6;
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
    size_t dataSize = indices.size() * sizeof( uint32_t );

    auto data = vhAllocMem( dataSize );
    memcpy( data->data(), indices.data(), dataSize );

    vhCreateIndexBuffer( buf, "Basic32", data, 0, VRHI_BUFFER_INDEX32 );
    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhFlush();
}

UTEST_F( Buffer, IndexBuffer_Flags_Coverage )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // Compute Read
    vhBuffer bCompRead = vhAllocBuffer();
    vhCreateIndexBuffer( bCompRead, "CompRead", nullptr, 100, VRHI_BUFFER_COMPUTE_READ );

    // Compute Write
    vhBuffer bCompWrite = vhAllocBuffer();
    vhCreateIndexBuffer( bCompWrite, "CompWrite", nullptr, 100, VRHI_BUFFER_COMPUTE_WRITE );

    // Draw Indirect
    vhBuffer bIndirect = vhAllocBuffer();
    vhCreateIndexBuffer( bIndirect, "DrawIndirect", nullptr, 100, VRHI_BUFFER_DRAW_INDIRECT );

    vhFlush();

    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( bCompRead );
    vhDestroyBuffer( bCompWrite );
    vhDestroyBuffer( bIndirect );
    vhFlush();
}

UTEST_F( Buffer, IndexBuffer_Resize_And_Uninit )
{
    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // Uninitialised Creation with Resize
    vhBuffer buf = vhAllocBuffer();
    vhCreateIndexBuffer( buf, "ResizeTest", nullptr, 100, VRHI_BUFFER_ALLOW_RESIZE | VRHI_BUFFER_INDEX32 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // Resize via Update
    // Increasing size to 200 indices (32-bit)
    vhUpdateIndexBuffer( buf, nullptr, 0, 200 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // Validation: Resize without flag
    vhBuffer bufFixed = vhAllocBuffer();
    vhCreateIndexBuffer( bufFixed, "FixedTest", nullptr, 100, VRHI_BUFFER_INDEX32 ); // No resize flag
    vhFlush();

    // Attempt resize - should fail
    vhUpdateIndexBuffer( bufFixed, nullptr, 0, 200 );
    vhFlush();

    EXPECT_GT( g_vhErrorCounter.load(), startErrors );

    vhDestroyBuffer( buf );
    vhDestroyBuffer( bufFixed );
    vhFlush();
}

UTEST_F( Buffer, UniformAlignment )
{
    vhFlush();
    int32_t baseline = g_vhErrorCounter.load();

    // Step A: Creation Stress
    vhBuffer b1 = vhAllocBuffer();
    vhCreateUniformBuffer( b1, "AutoAlignCreate255", nullptr, 255 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    // Step B: Update Stress
    vhMem* data255 = vhAllocMem( 255 );
    vhUpdateUniformBuffer( b1, data255, 0, 255 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    vhDestroyBuffer( b1 );
    vhFlush();
}

UTEST_F( Buffer, StorageAlignment )
{

    vhFlush();
    int32_t baseline = g_vhErrorCounter.load();

    // Step A: Creation Stress
    vhBuffer b1 = vhAllocBuffer();
    vhCreateStorageBuffer( b1, "AutoAlignCreate15", nullptr, 15 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    // Step B: Update Stress
    vhMem* data15 = vhAllocMem( 15 );
    vhUpdateStorageBuffer( b1, data15, 0, 15 );
    vhFlush();

    // Un-aligned creation/access (e.g. 255 bytes) is supported by NVRHI. Expect Success.
    EXPECT_EQ( g_vhErrorCounter.load(), baseline );

    vhDestroyBuffer( b1 );
    vhFlush();
}

UTEST_F( Buffer, ResourceQueries )
{


    vhBuffer buf = vhAllocBuffer();
    uint64_t size = 1024;
    uint16_t flags = VRHI_BUFFER_COMPUTE_WRITE;
    vhCreateUniformBuffer( buf, "TestBuffer", nullptr, size, flags );
    vhFlush();

    uint32_t stride = 0;
    uint64_t qFlags = 0;
    uint64_t qSize = vhGetBufferInfo( buf, &stride, &qFlags );

    EXPECT_EQ( qSize, size );
    EXPECT_EQ( stride, 1 ); // Uniform buffer stride is 1
    EXPECT_EQ( qFlags, flags );

    void* handle = vhGetBufferNvrhiHandle( buf );
    EXPECT_NE( handle, nullptr );

    vhDestroyBuffer( buf );
    vhFlush();

    // Query after destruction should return 0/null
    EXPECT_EQ( vhGetBufferInfo( buf ), 0 );
    EXPECT_EQ( vhGetBufferNvrhiHandle( buf ), nullptr );
}

UTEST_F( Buffer, StorageBufferCreation )
{

    vhFlush();
    int32_t startErrors = g_vhErrorCounter.load();

    // 1. Structured Buffer
    vhBuffer bStruct = vhAllocBuffer();
    // Create a structured buffer with stride 64
    vhCreateStorageStructuredBuffer( bStruct, "StructBuffer", nullptr, 64 * 10, 64 );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    // Verify NVRHI desc
    {
        nvrhi::IBuffer* buf = (nvrhi::IBuffer*)vhGetBufferNvrhiHandle( bStruct );
        ASSERT_TRUE( buf );
        const nvrhi::BufferDesc& desc = buf->getDesc();
        EXPECT_EQ( desc.byteSize, 640 );
        EXPECT_EQ( desc.structStride, 64 );
        EXPECT_EQ( desc.format, nvrhi::Format::UNKNOWN );
        EXPECT_TRUE( desc.canHaveUAVs );
        EXPECT_TRUE( desc.canHaveRawViews );
    }

    // 2. Typed Buffer
    vhBuffer bTyped = vhAllocBuffer();
    vhCreateStorageTypedBuffer( bTyped, "TypedBuffer", nullptr, 4 * 100, nvrhi::Format::R32_FLOAT );
    vhFlush();
    EXPECT_EQ( g_vhErrorCounter.load(), startErrors );

    {
        nvrhi::IBuffer* buf = (nvrhi::IBuffer*)vhGetBufferNvrhiHandle( bTyped );
        ASSERT_TRUE( buf );
        const nvrhi::BufferDesc& desc = buf->getDesc();
        EXPECT_EQ( desc.byteSize, 400 );
        EXPECT_EQ( desc.structStride, 0 );
        EXPECT_EQ( desc.format, nvrhi::Format::R32_FLOAT );
        EXPECT_TRUE( desc.canHaveTypedViews );
        EXPECT_TRUE( desc.canHaveRawViews );
    }

    vhDestroyBuffer( bStruct );
    vhDestroyBuffer( bTyped );
    vhFlush();
}

UTEST_F( Buffer, TransientRingAllocator )
{
    // --------------------------------------------------------------------------
    // Ring Allocator Logic
    // --------------------------------------------------------------------------
    vhTransientAllocator ringAlloc;
    vhBuffer buffers[3] = { (vhBuffer)1, (vhBuffer)2, (vhBuffer)3 };
    ringAlloc.Init( buffers, 1024, 256 );

    // Frame 0
    EXPECT_EQ( ringAlloc.GetFrameIndex(), 0 );
    EXPECT_EQ( ringAlloc.GetBuffer(), (vhBuffer)1 );
    EXPECT_EQ( ringAlloc.Alloc( 100 ), 0 );

    // Frame 1
    ringAlloc.Step();
    EXPECT_EQ( ringAlloc.GetFrameIndex(), 1 );
    EXPECT_EQ( ringAlloc.GetBuffer(), (vhBuffer)2 );

    // Important: Step() does NOT reset. User must call Reset().
    ringAlloc.Reset();
    EXPECT_EQ( ringAlloc.Alloc( 100 ), 0 );

    // Frame 2
    ringAlloc.Step();
    ringAlloc.Reset();
    EXPECT_EQ( ringAlloc.GetFrameIndex(), 2 );
    EXPECT_EQ( ringAlloc.GetBuffer(), (vhBuffer)3 );
    EXPECT_EQ( ringAlloc.Alloc( 100 ), 0 );

    // Frame 0 (Loop)
    ringAlloc.Step();
    ringAlloc.Reset();
    EXPECT_EQ( ringAlloc.GetFrameIndex(), 0 );
    EXPECT_EQ( ringAlloc.GetBuffer(), (vhBuffer)1 );
}

UTEST_F( Buffer, SubAllocator )
{
    // --------------------------------------------------------------------------
    // Sub-Allocator with Deferred Freeing
    // --------------------------------------------------------------------------
    vhSubAllocator subAlloc;
    vhBuffer testBuffer = (vhBuffer)12345;
    
    // Initialise with 1MB buffer, 256 byte alignment
    subAlloc.Init( testBuffer, 1024 * 1024, 256 );
    
    EXPECT_EQ( subAlloc.GetBuffer(), testBuffer );
    EXPECT_EQ( subAlloc.GetAlignment(), 256 );
    
    // Test basic allocation
    int64_t offset1 = subAlloc.Alloc( 512 );
    EXPECT_GE( offset1, 0 );
    EXPECT_EQ( offset1 % 256, 0 ); // Should be aligned
    
    int64_t offset2 = subAlloc.Alloc( 1024 );
    EXPECT_GE( offset2, 0 );
    EXPECT_EQ( offset2 % 256, 0 );
    
    // Test that allocations don't overlap
    EXPECT_NE( offset1, offset2 );
    
    // Test free with deferred freeing
    subAlloc.Free( offset1 );
    
    // Offset1 should not be immediately available (deferred freeing)
    int64_t offset3 = subAlloc.Alloc( 512 );
    EXPECT_GE( offset3, 0 );
    EXPECT_NE( offset3, offset1 ); // Should not get same offset back yet
    
    // Step through 3 frames to release deferred frees
    subAlloc.Step(); // Frame 1
    subAlloc.Step(); // Frame 2
    subAlloc.Step(); // Frame 3 - offset1 should now be released
    
    // After 3 frames, we should be able to allocate again
    // (though not necessarily the same offset due to fragmentation)
    uint64_t usedBefore = subAlloc.GetUsedSpace();
    uint64_t availableBefore = subAlloc.GetAvailableSpace();
    
    int64_t offset4 = subAlloc.Alloc( 512 );
    EXPECT_GE( offset4, 0 );
    
    // Test allocation failure when out of space
    // First reset to have clean state
    subAlloc.Reset();
    
    // Allocate until we run out of space
    std::vector< int64_t > largeAllocations;
    while ( true )
    {
        int64_t offset = subAlloc.Alloc( 256 * 1024 ); // Try to allocate 256KB (smaller to fit multiple)
        if ( offset < 0 )
        {
            break; // Out of space
        }
        largeAllocations.push_back( offset );
    }
    
    EXPECT_GT( largeAllocations.size(), 0 ); // Should have allocated at least one
    
    // Free all large allocations
    for ( int64_t offset : largeAllocations )
    {
        subAlloc.Free( offset );
    }
    
    // Step through frames to release them
    for ( int i = 0; i < 3; i++ )
    {
        subAlloc.Step();
    }
    
    // Test reset
    subAlloc.Reset();
    
    // After reset, should be able to allocate from beginning
    int64_t offsetAfterReset = subAlloc.Alloc( 512 );
    EXPECT_GE( offsetAfterReset, 0 );
    
    // Test alignment
    int64_t smallOffset = subAlloc.Alloc( 1 ); // Request 1 byte
    EXPECT_GE( smallOffset, 0 );
    EXPECT_EQ( smallOffset % 256, 0 ); // Should be aligned to 256 bytes
    
    // Test invalid free (these will print error messages but should not crash)
    subAlloc.Free( -1 ); // Should not crash
    subAlloc.Free( 999999 ); // Unknown offset, should not crash
    
    // Test GetUsedSpace and GetAvailableSpace
    uint64_t used = subAlloc.GetUsedSpace();
    uint64_t available = subAlloc.GetAvailableSpace();
    EXPECT_LE( used, 1024 * 1024 );
    EXPECT_LE( available, 1024 * 1024 );
    EXPECT_EQ( used + available, 1024 * 1024 );
}

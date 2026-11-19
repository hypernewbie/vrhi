/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <nvrhi/nvrhi.h>

namespace nvrhi
{
    namespace ObjectTypes
    {
        constexpr ObjectType Metal_Device         = 0x00040001;
        constexpr ObjectType Metal_CommandQueue   = 0x00040002;
        constexpr ObjectType Metal_CommandBuffer  = 0x00040003;
        constexpr ObjectType Metal_Buffer         = 0x00040004;
        constexpr ObjectType Metal_Texture        = 0x00040005;
        constexpr ObjectType Metal_Sampler        = 0x00040006;
        constexpr ObjectType Metal_ShaderLibrary  = 0x00040007;
        constexpr ObjectType Metal_Heap           = 0x00040008;
    };
}

namespace nvrhi::metal
{
    class IDevice : public nvrhi::IDevice
    {
    public:
        // Borrowed native objects. The caller must not release them.
        virtual Object getNativeDevice() = 0;
        virtual Object getNativeCommandQueue() = 0;

        // [UAA] Returns the device's recommended working set size, the closest Metal
        // equivalent of the adapter memory the Vulkan backend reports.
        virtual uint64_t getNativeDeviceMemorySize() = 0;
        virtual uint64_t getNativeAllocatedSize() = 0;

        // [UAA] - START - mapped buffer write flush
        // Publishes host writes in a mapped buffer range to the device.
        // The buffer must be NVRHI-owned, non-virtual, and mapped for CPU write access.
        // The offset and the size must be multiples of getMappedBufferFlushAlignment().
        virtual uint64_t getMappedBufferFlushAlignment() = 0;
        virtual bool flushMappedBuffer(IBuffer* buffer, uint64_t offset, uint64_t size) = 0;
        // [UAA] - END
    };

    typedef RefCountPtr<IDevice> DeviceHandle;

    struct DeviceDesc
    {
        IMessageCallback* errorCB = nullptr;

        // id<MTLDevice>. When null, the default system device is used.
        Object device = nullptr;
    };

    NVRHI_API DeviceHandle createDevice(const DeviceDesc& desc);
}

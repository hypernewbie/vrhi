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

#if defined(_WIN32)
    #define VK_USE_PLATFORM_WIN32_KHR
    #ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
        #include <windows.h>
    #endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_win32.h>
#else
    #include <vulkan/vulkan.h>
#endif

#ifndef VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES
#include <vector>
#include <cstring>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <climits>
#include <string>
#include <cstdio>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <concurrentqueue/blockingconcurrentqueue.h>
#endif // VRHI_SKIP_COMMON_DEPENDENCY_INCLUDES

// Required by NVRHI Vulkan backend - defines vk::DispatchLoaderDynamic storage
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "vrhi_impl_fwd.h"

// Forward declarations
// Moved to vrhi_impl_fwd.h

nvrhi::CommandListHandle vhCmdListGet( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics );
void vhCmdListFlush( nvrhi::CommandQueue type = nvrhi::CommandQueue::Graphics );

template< typename T, typename... Args >
T* vhCmdAlloc( Args&&... args )
{
    return new T( std::forward<Args>(args)... );
}

template< typename T >
void vhCmdRelease( T* cmd )
{
    if ( !cmd ) return;
    delete cmd;
}

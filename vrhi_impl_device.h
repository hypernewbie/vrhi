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

#ifndef VRHI_IMPLEMENTATION
#include "vrhi_impl.h"
#endif // VRHI_IMPLEMENTATION
#include "vrhi_utils.h"

#include <nvrhi/validation.h>
#include <vk-bootstrap/VkBootstrap.h>

std::unique_ptr< vkb::Device > g_vulkanBDevice;

class vhVK_MessageCallback : public nvrhi::IMessageCallback
{
public:
    void message( nvrhi::MessageSeverity severity, const char* messageText ) override
    {
        if ( severity >= nvrhi::MessageSeverity::Error )
        {
            VRHI_ERR( "[NVRHI] %s\n", messageText );
        }
        else
        {
            VRHI_LOG( "[NVRHI] %s\n", messageText );
        }
    }
} static g_vhVKMessageCallback;

static VKAPI_ATTR VkBool32 VKAPI_CALL vhVKDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT s,
    VkDebugUtilsMessageTypeFlagsEXT t,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void* pUser )
{
    if ( s >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
    {
        VRHI_LOG( "[VULKAN] %s\n", pData->pMessage );
    }
    else
    {
        VRHI_LOG( "[VULKAN] %s\n", pData->pMessage );
    }
    return VK_FALSE;
}

// -------------------------------------------------------- RHI Device --------------------------------------------------------

void vhInit( bool quiet )
{
    if ( !quiet ) VRHI_LOG( "Initialising Vulkan RHI ...\n" );

    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    if ( g_vhDevice )
    {
        if ( !quiet ) VRHI_LOG( "vhInit() : RHI already initialised!\n" );
        return;
    }

    // 1. Create VkInstance (via vk-bootstrap)

    if ( !quiet ) VRHI_LOG( "    Creating VK Instance (via vk-bootstrap)\n" );
    vkb::InstanceBuilder instBuilder;
    auto instRet = instBuilder.set_app_name( g_vhInit.appName.c_str() )
        .set_engine_name( g_vhInit.engineName.c_str() )
        .require_api_version( 1, 3, 0 )
        .set_headless( true )
        .request_validation_layers( g_vhInit.debug )
        .use_default_debug_messenger()
        .build();

    if ( !instRet )
    {
        VRHI_LOG( "Failed to create Vulkan Instance: %s\n", instRet.error().message().c_str() );
        exit( 1 );
    }

    vkb::Instance vkbInst = instRet.value();
    g_vulkanInstance = vkbInst.instance;
    g_vulkanDebugMessenger = vkbInst.debug_messenger;

    // Initialise vulkan.hpp dynamic dispatcher with instance functions
    if ( !quiet ) VRHI_LOG( "    Initialising vulkan.hpp dynamic dispatcher with instance functions\n" );
    VULKAN_HPP_DEFAULT_DISPATCHER.init( g_vulkanInstance, vkGetInstanceProcAddr );

    // 2. Physical Device Selection (via vk-bootstrap)

    if ( !quiet ) VRHI_LOG( "    Selecting physical device (via vk-bootstrap)\n" );
    vkb::PhysicalDeviceSelector selector( vkbInst );
    
    VkPhysicalDeviceVulkan12Features v12Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12Features.timelineSemaphore = VK_TRUE;
    v12Features.bufferDeviceAddress = VK_TRUE;

    selector.set_minimum_version( 1, 1 )
            .set_required_features_12( v12Features );

    vkb::PhysicalDevice vkbPhys;

    if ( g_vhInit.deviceIndex >= 0 )
    {
        // User selected device.
        auto physRet = selector.select_devices();
        if ( !physRet || g_vhInit.deviceIndex >= ( int ) physRet.value().size() )
        {
            VRHI_LOG( "Failed to select physical device at index %d\n", g_vhInit.deviceIndex );
            exit( 1 );
        }
        vkbPhys = physRet.value()[g_vhInit.deviceIndex];
    }
    else
    {
        // Auto selected device.
        auto physRet = selector.select();
        if ( !physRet )
        {
            VRHI_LOG( "Failed to select suitable physical device: %s\n", physRet.error().message().c_str() );
            exit( 1 );
        }
        vkbPhys = physRet.value();
    }

    g_vulkanPhysicalDevice = vkbPhys.physical_device;
    if ( !quiet ) VRHI_LOG( "    Selected GPU Device: %s\n", vkbPhys.name.c_str() );

    // 3. Device Creation & Queues (via vk-bootstrap)

    bool rtExtEnabled = false;
    if ( g_vhInit.raytracing )
    {
        rtExtEnabled = vkbPhys.enable_extension_if_present( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME ) &&
                       vkbPhys.enable_extension_if_present( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME ) &&
                       vkbPhys.enable_extension_if_present( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
    }

    if ( !quiet ) VRHI_LOG( "    Creating VK Logical Device (via vk-bootstrap)\n" );
    vkb::DeviceBuilder devBuilder( vkbPhys );
    
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };

    if ( rtExtEnabled )
    {
        accelFeatures.accelerationStructure = VK_TRUE;
        rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
        devBuilder.add_pNext( &accelFeatures );
        devBuilder.add_pNext( &rtPipelineFeatures );
        if ( !quiet ) VRHI_LOG( "    Ray Tracing extensions enabled.\n" );
    }
    else
    {
        if ( !quiet ) VRHI_LOG( "    Ray Tracing extensions missing. RT features disabled.\n" );
    }

    auto devRet = devBuilder.build();
    if ( !devRet )
    {
        VRHI_LOG( "Failed to create Vulkan Device: %s\n", devRet.error().message().c_str() );
        exit( 1 );
    }

    g_vulkanBDevice = std::make_unique<vkb::Device>( devRet.value() );
    g_vulkanDevice = g_vulkanBDevice->device;

    // Verify RT enablement via function pointers
    if ( rtExtEnabled )
    {
        auto fp = vkGetDeviceProcAddr( g_vulkanDevice, "vkCreateAccelerationStructureKHR" );
        if ( !fp )
        {
            rtExtEnabled = false;
            if ( !quiet ) VRHI_LOG( "    WARNING: RT extensions requested but vkCreateAccelerationStructureKHR not found. Disabling RT.\n" );
        }
    }
    g_vhRayTracingEnabled = rtExtEnabled;

    // Get Queues
    auto graphicsQueueRet = g_vulkanBDevice->get_queue( vkb::QueueType::graphics );
    if ( !graphicsQueueRet )
    {
        VRHI_LOG( "Failed to get graphics queue: %s\n", graphicsQueueRet.error().message().c_str() );
        exit( 1 );
    }
    g_vulkanGraphicsQueue = graphicsQueueRet.value();
    g_QueueFamilyGraphics = g_vulkanBDevice->get_queue_index( vkb::QueueType::graphics ).value();

    auto computeQueueRet = g_vulkanBDevice->get_queue( vkb::QueueType::compute );
    if ( computeQueueRet )
    {
        g_vulkanComputeQueue = computeQueueRet.value();
        g_QueueFamilyCompute = g_vulkanBDevice->get_queue_index( vkb::QueueType::compute ).value();
    }
    else
    {
        g_vulkanComputeQueue = g_vulkanGraphicsQueue;
        g_QueueFamilyCompute = g_QueueFamilyGraphics;
    }

    auto transferQueueRet = g_vulkanBDevice->get_queue( vkb::QueueType::transfer );
    if ( transferQueueRet )
    {
        g_vulkanTransferQueue = transferQueueRet.value();
        g_QueueFamilyTransfer = g_vulkanBDevice->get_queue_index( vkb::QueueType::transfer ).value();
    }
    else
    {
        g_vulkanTransferQueue = g_vulkanComputeQueue;
        g_QueueFamilyTransfer = g_QueueFamilyCompute;
    }

    static std::vector<std::string> s_enabledExtensions;
    s_enabledExtensions.clear();
    if ( g_vhRayTracingEnabled )
    {
        s_enabledExtensions.push_back( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME );
        s_enabledExtensions.push_back( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
        s_enabledExtensions.push_back( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
    }

    g_vulkanEnabledExtensionCount = ( uint32_t ) s_enabledExtensions.size();
    
    static std::vector<const char*> s_enabledExtensionPointers;
    s_enabledExtensionPointers.clear();
    for ( const auto& ext : s_enabledExtensions ) s_enabledExtensionPointers.push_back( ext.c_str() );

    if ( !quiet ) VRHI_LOG( "    Selected VK Queues: Graphics %d, Compute %d, Transfer %d\n", g_QueueFamilyGraphics, g_QueueFamilyCompute, g_QueueFamilyTransfer );
    if ( !quiet ) VRHI_LOG( "    Created VK Logical Device.\n" );

    // 4. NVRHI Handover

    if ( !quiet ) VRHI_LOG( "    Linking to nvRHI .... \n" );

    // Required by NVRHI Vulkan backend - initializes vk::DispatchLoaderDynamic for function pointers.
    VULKAN_HPP_DEFAULT_DISPATCHER.init( g_vulkanInstance, vkGetInstanceProcAddr, g_vulkanDevice, vkGetDeviceProcAddr );

    nvrhi::vulkan::DeviceDesc nvrhiDesc;
    nvrhiDesc.errorCB = &g_vhVKMessageCallback;
    nvrhiDesc.instance = g_vulkanInstance;
    nvrhiDesc.physicalDevice = g_vulkanPhysicalDevice;
    nvrhiDesc.device = g_vulkanDevice;
    
    nvrhiDesc.graphicsQueue = g_vulkanGraphicsQueue;
    nvrhiDesc.graphicsQueueIndex = g_QueueFamilyGraphics;
    nvrhiDesc.computeQueue = g_vulkanComputeQueue;
    nvrhiDesc.computeQueueIndex = g_QueueFamilyCompute;
    nvrhiDesc.transferQueue = g_vulkanTransferQueue;
    nvrhiDesc.transferQueueIndex = g_QueueFamilyTransfer;

    nvrhiDesc.deviceExtensions = s_enabledExtensionPointers.data();
    nvrhiDesc.numDeviceExtensions = ( uint32_t ) s_enabledExtensionPointers.size();

    g_vhDevice = nvrhi::vulkan::createDevice( nvrhiDesc );
    if ( !g_vhDevice )
    {
        VRHI_LOG( "Failed to create NVRHI device!\n" );
        exit(1);
    }
    if ( g_vhInit.debug )
    {
        // Wrap with validation layer in debug builds - catches state tracking errors
        if ( !quiet ) VRHI_LOG( "    Wrapping nvrhi device with validation layer...\n" );
        g_vhDevice = nvrhi::validation::createValidationLayer( g_vhDevice );
    }

    // 5. Create RHI Command Buffer Thread
    if ( !quiet ) VRHI_LOG( "    Creating RHI Thread...\n" );

    vhBackendInit();
    g_vhCmdsQuit = false;
    g_vhCmdThreadReady = false;
    g_vhCmdThread = std::thread( vhBackendThreadEntry, g_vhInit.fnThreadInitCallback );
    while ( !g_vhCmdThreadReady ) { std::this_thread::yield(); }
}

void vhShutdown( bool quiet )
{
    if ( !quiet ) VRHI_LOG( "Shutdown Vulkan RHI ...\n" );

    // Join RHI Command Buffer Thread
    if ( !quiet ) VRHI_LOG( "    Joining RHI Thread...\n" );
    g_vhCmdsQuit = true;
    g_vhCmdThread.join();
    g_vhCmdThreadReady = false;
    vhBackendShutdown();
    g_vhDevice->runGarbageCollection();
    vhCmdListFlushAll();

    if ( g_vulkanDevice != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Allowing Vulkan Device to finish...\n" );
        vkDeviceWaitIdle( g_vulkanDevice );
    }

    if ( !quiet ) VRHI_LOG( "    Destroying NVRHI Device...\n" );
    g_vhDevice = nullptr; // RefCountPtr handles the release()

    // Clear resources
    if ( !quiet ) VRHI_LOG( "    Clearing resources...\n" );
    g_vhTextureIDList.purge();

    if ( g_vulkanDevice != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Destroying Vulkan Device...\n" );
        vkDestroyDevice( g_vulkanDevice, nullptr );
        g_vulkanDevice = VK_NULL_HANDLE;
    }

    if ( g_vulkanDebugMessenger != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Destroying Vulkan Debug Messenger...\n" );
        auto func = ( PFN_vkDestroyDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( g_vulkanInstance, "vkDestroyDebugUtilsMessengerEXT" );
        if ( func ) func( g_vulkanInstance, g_vulkanDebugMessenger, nullptr );
        g_vulkanDebugMessenger = VK_NULL_HANDLE;
    }

    if ( g_vulkanInstance != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Destroying Vulkan Instance...\n" );
        vkDestroyInstance( g_vulkanInstance, nullptr );
        g_vulkanInstance = VK_NULL_HANDLE;
    }
}

std::string vhGetDeviceInfo()
{
    if ( !g_vhDevice )
    {
        return "RHI not initialized";
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties( g_vulkanPhysicalDevice, &props );

    const char* typeStr = "Unknown";
    switch ( props.deviceType )
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: typeStr = "Discrete GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: typeStr = "CPU"; break;
        default: typeStr = "Other"; break;
    }

    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
        "Device: %s Vulkan: %d.%d.%d Type: %s Queues: Gfx=%d Comp=%d Trans=%d NVRHI: Active Extensions: %d",
        props.deviceName,
        VK_API_VERSION_MAJOR( props.apiVersion ),
        VK_API_VERSION_MINOR( props.apiVersion ),
        VK_API_VERSION_PATCH( props.apiVersion ),
        typeStr,
        g_QueueFamilyGraphics,
        g_QueueFamilyCompute,
        g_QueueFamilyTransfer,
        g_vulkanEnabledExtensionCount
    );
    return std::string(buffer);
}

void vhDispatch( vhStateId stateID, glm::uvec3 workGroupCount )
{
    VIDL_vhDispatch* cmd = vhCmdAlloc<VIDL_vhDispatch>( stateID, workGroupCount );
    vhCmdEnqueue( cmd );
}

void vhDispatchIndirect( vhStateId stateID, vhBuffer indirectBuffer, uint64_t byteOffset )
{
    if ( byteOffset % 4 != 0 )
    {
        VRHI_ERR( "vhDispatchIndirect() : byteOffset %llu must be 4-byte aligned!\n", byteOffset );
        return;
    }
    VIDL_vhDispatchIndirect* cmd = vhCmdAlloc<VIDL_vhDispatchIndirect>( stateID, indirectBuffer, byteOffset );
    vhCmdEnqueue( cmd );
}

void vhBlitBuffer( vhBuffer dst, vhBuffer src, uint64_t dstOffset, uint64_t srcOffset, uint64_t size )
{
    VIDL_vhBlitBuffer* cmd = vhCmdAlloc<VIDL_vhBlitBuffer>( dst, src, dstOffset, srcOffset, size );
    vhCmdEnqueue( cmd );
}

void vhFlushInternal( std::atomic<bool>* fence, bool waitForGPU )
{
    // Fence memory must be valid until signaled! 
    // Usually stack memory of the caller waiting on it.
    VIDL_vhFlushInternal* cmd = vhCmdAlloc<VIDL_vhFlushInternal>( fence, waitForGPU );
    vhCmdEnqueue( cmd );
}

void vhFlush()
{
    std::atomic<bool> fence = false;
    vhFlushInternal( &fence, false );

    // Wait for fence to be signaled
    while ( !fence.load() )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
}

void vhFinish()
{
    std::atomic<bool> fence = false;
    vhFlushInternal( &fence, true );

    // Wait for fence to be signaled
    while ( !fence.load() )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
}
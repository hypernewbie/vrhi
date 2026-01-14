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

#include "vrhi_internal.h"
#include "vrhi_utils.h"
#include "renderdoc_app.h"

#include <nvrhi/validation.h>
#include <vk-bootstrap/VkBootstrap.h>
#include <komihash/komihash.h>

std::unique_ptr< vkb::Device > g_vulkanBDevice;
void vhPSOCacheShutdown();

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
        VRHI_ERR( "[VULKAN] %s\n", pData->pMessage );
    }
    else
    {
        VRHI_LOG( "[VULKAN] %s\n", pData->pMessage );
    }
    return VK_FALSE;
}

RENDERDOC_API_1_1_2* g_vhRenderDoc = nullptr;

void vhEnableRenderDoc()
{
    if ( g_vhRenderDoc ) return;

#ifdef _WIN32
    HMODULE mod = GetModuleHandleA( "renderdoc.dll" );
    if ( !mod ) mod = LoadLibraryA( "renderdoc.dll" );

    if ( !mod )
    {
        VRHI_LOG( "    No renderdoc.dll module. This is OK.\n" );
        return;
    }

    pRENDERDOC_GetAPI RENDERDOC_GetAPI = ( pRENDERDOC_GetAPI ) GetProcAddress( mod, "RENDERDOC_GetAPI" );
    if ( !RENDERDOC_GetAPI )
    {
        VRHI_LOG( "    Failed to get RENDERDOC_GetAPI address.\n" );
        return;
    }

    int ret = RENDERDOC_GetAPI( eRENDERDOC_API_Version_1_1_2, ( void** ) &g_vhRenderDoc );
    if ( ret != 1 )
    {
        VRHI_LOG( "    Failed to initialise RenderDoc API.\n" );
        return;
    }
    VRHI_LOG( "    RenderDoc API loaded successfully.\n" );
    g_vhRenderDoc->SetCaptureOptionU32( eRENDERDOC_Option_APIValidation, 1 );
    g_vhRenderDoc->SetCaptureOptionU32( eRENDERDOC_Option_CaptureAllCmdLists, 1 );
#else
    VRHI_LOG( "RenderDoc support implementation missing for this platform.\n" );
#endif
}

// -------------------------------------------------------- RHI Device --------------------------------------------------------

void vhInit( bool quiet )
{
    if ( !quiet ) VRHI_LOG( "Initialising Vulkan RHI ...\n" );
    g_vhErrorCounter = 0;
    g_vhPSOCompileCounter = 0;

    if ( g_vhInit.renderdoc )
    {
        if ( !quiet ) VRHI_LOG( "    Enabling RenderDoc support...\n" );
        vhEnableRenderDoc();
    }

    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    if ( g_vhDevice )
    {
        if ( !quiet ) VRHI_LOG( "vhInit() : RHI already initialised!\n" );
        return;
    }

    // Create VkInstance (via vk-bootstrap)

    if ( !quiet ) VRHI_LOG( "    Creating VK Instance (via vk-bootstrap)\n" );
    vkb::InstanceBuilder instBuilder;
    instBuilder.set_app_name( g_vhInit.appName.c_str() )
        .set_engine_name( g_vhInit.engineName.c_str() )
        .require_api_version( 1, 3, 0 )
        .set_headless( true )
        .request_validation_layers( g_vhInit.debug )
        .set_debug_callback( vhVKDebugCallback );
    if ( g_vhInit.renderdoc ) instBuilder.enable_extension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

    auto instRet = instBuilder.build();
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

    // Physical Device Selection (via vk-bootstrap)

    if ( !quiet ) VRHI_LOG( "    Selecting physical device (via vk-bootstrap)\n" );
    vkb::PhysicalDeviceSelector selector( vkbInst );

    VkPhysicalDeviceVulkan11Features v11Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features v12Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12Features.timelineSemaphore = VK_TRUE;
    v12Features.bufferDeviceAddress = VK_TRUE;
    
    VkPhysicalDeviceVulkan13Features v13Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    v13Features.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures features = { .robustBufferAccess = g_vhInit.robust ? VK_TRUE : VK_FALSE };
    features.independentBlend = VK_TRUE;
    features.fillModeNonSolid = VK_TRUE;
    features.samplerAnisotropy = VK_TRUE;
    features.depthClamp = VK_TRUE;

    selector.set_minimum_version( 1, 3 )
        .set_required_features_11( v11Features )
        .set_required_features_12( v12Features )
        .set_required_features_13( v13Features )
        .set_required_features( features );

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

    VkPhysicalDeviceDriverProperties driverProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props2.pNext = &driverProps;
    vkGetPhysicalDeviceProperties2( g_vulkanPhysicalDevice, &props2 );
    VRHI_LOG( "    Vulkan Driver: %s (%s)\n", driverProps.driverName, driverProps.driverInfo );

    // Device Creation & Queues (via vk-bootstrap)

    bool rtExtEnabled = false;
    if ( g_vhInit.raytracing )
    {
        rtExtEnabled = vkbPhys.enable_extension_if_present( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME ) &&
            vkbPhys.enable_extension_if_present( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME ) &&
            vkbPhys.enable_extension_if_present( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
    }

    bool robustness2Enabled = false;
    if ( g_vhInit.robust )
    {
        robustness2Enabled = vkbPhys.enable_extension_if_present( VK_EXT_ROBUSTNESS_2_EXTENSION_NAME );
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

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    if ( robustness2Enabled )
    {
        // Query supported features first!
        VkPhysicalDeviceRobustness2FeaturesEXT supported = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
        VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        features2.pNext = &supported;
        vkGetPhysicalDeviceFeatures2( vkbPhys.physical_device, &features2 );

        if ( supported.nullDescriptor ) robustness2Features.nullDescriptor = VK_TRUE;
        if ( supported.robustBufferAccess2 ) robustness2Features.robustBufferAccess2 = VK_TRUE;
        if ( supported.robustImageAccess2 ) robustness2Features.robustImageAccess2 = VK_TRUE;

        devBuilder.add_pNext( &robustness2Features );
        if ( !quiet ) VRHI_LOG( "    Robustness2 extension enabled.\n" );
    }
    else
    {
        if ( !quiet && g_vhInit.robust ) VRHI_LOG( "    Robustness2 extension missing or disabled.\n" );
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
    if ( robustness2Enabled )
    {
        s_enabledExtensions.push_back( VK_EXT_ROBUSTNESS_2_EXTENSION_NAME );
    }

    g_vulkanEnabledExtensionCount = ( uint32_t ) s_enabledExtensions.size();

    static std::vector<const char*> s_enabledExtensionPointers;
    s_enabledExtensionPointers.clear();
    for ( const auto& ext : s_enabledExtensions ) s_enabledExtensionPointers.push_back( ext.c_str() );

    if ( !quiet ) VRHI_LOG( "    Selected VK Queues: Graphics %d, Compute %d, Transfer %d\n", g_QueueFamilyGraphics, g_QueueFamilyCompute, g_QueueFamilyTransfer );
    if ( !quiet ) VRHI_LOG( "    Created VK Logical Device.\n" );

    // NVRHI Handover

    if ( !quiet ) VRHI_LOG( "    Linking to nvRHI .... \n" );

    // Required by NVRHI Vulkan backend - initialises vk::DispatchLoaderDynamic for function pointers.
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

    std::vector<const char*> instanceExtensions;
    if ( g_vhInit.renderdoc ) instanceExtensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

    nvrhiDesc.deviceExtensions = s_enabledExtensionPointers.data();
    nvrhiDesc.numDeviceExtensions = ( uint32_t ) s_enabledExtensionPointers.size();
    nvrhiDesc.instanceExtensions = instanceExtensions.data();
    nvrhiDesc.numInstanceExtensions = ( uint32_t ) instanceExtensions.size();

    g_vhDevice = nvrhi::vulkan::createDevice( nvrhiDesc );
    if ( !g_vhDevice )
    {
        VRHI_LOG( "Failed to create NVRHI device!\n" );
        exit( 1 );
    }
    if ( g_vhInit.debug )
    {
        // Wrap with validation layer in debug builds - catches state tracking errors
        if ( !quiet ) VRHI_LOG( "    Wrapping nvrhi device with validation layer...\n" );
        g_vhDevice = nvrhi::validation::createValidationLayer( g_vhDevice );
    }

    vhInitDummyResources();

    // Create RHI Command Buffer Thread
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
    vhFinish();
    vhShutdownDummyResources();

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
    vhPSOCacheShutdown();
    vhSamplerCacheShutdown();
    vhBindingSetCacheClear();
    vhFBOCacheReset();

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
        return "RHI not initialised";
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
    snprintf( buffer, sizeof( buffer ),
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
    return std::string( buffer );
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

void vhDraw( vhStateId state, uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation )
{
    vhDrawCommonInternal(
        state,
        0, // flags
        vertexCount,
        instanceCount,
        startVertexLocation,
        0, // startIndexLocation
        startInstanceLocation,
        0  // drawCount
    );
}

void vhDrawIndexed( vhStateId state, uint32_t indexCount, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation )
{
    vhDrawCommonInternal(
        state,
        VRHI_DRAW_INDEXED,
        indexCount, // vertexCount acts as indexCount for indexed draws
        instanceCount,
        ( uint32_t ) baseVertexLocation, // startVertexLocation acts as baseVertexLocation for indexed
        startIndexLocation,
        startInstanceLocation,
        0 // drawCount
    );
}


void vhDrawIndirect( vhStateId state, uint32_t drawCount )
{
    vhDrawCommonInternal(
        state,
        VRHI_DRAW_INDIRECT,
        0, 0, 0, 0, 0, // unused direct args
        drawCount
    );
}

void vhDrawIndexedIndirect( vhStateId state, uint32_t drawCount )
{
    vhDrawCommonInternal(
        state,
        VRHI_DRAW_INDEXED | VRHI_DRAW_INDIRECT,
        0, 0, 0, 0, 0, // unused direct args
        drawCount
    );
}

void vhClear( vhStateId state, uint16_t clearFlags )
{
    VIDL_vhClear* cmd = vhCmdAlloc<VIDL_vhClear>( state, clearFlags );
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

void vhFlush( bool wait )
{
    std::atomic<bool> fence = false;
    vhFlushInternal( wait ? &fence : nullptr, false );

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

void vhBeginMarker( const std::string& name )
{
    if ( !g_vhInit.markers ) return;
    VIDL_vhBeginMarker* cmd = vhCmdAlloc<VIDL_vhBeginMarker>( name );
    vhCmdEnqueue( cmd );
}

void vhEndMarker()
{
    if ( !g_vhInit.markers ) return;
    VIDL_vhEndMarker* cmd = vhCmdAlloc<VIDL_vhEndMarker>();
    vhCmdEnqueue( cmd );
}

void vhCaptureStart()
{
    if ( !g_vhInit.renderdoc ) return;
    VIDL_vhCaptureStart* cmd = vhCmdAlloc<VIDL_vhCaptureStart>();
    vhCmdEnqueue( cmd );
}

void vhCaptureEnd()
{
    if ( !g_vhInit.renderdoc ) return;
    VIDL_vhCaptureEnd* cmd = vhCmdAlloc<VIDL_vhCaptureEnd>();
    vhCmdEnqueue( cmd );
}

void vhResizeCleanup()
{
    VIDL_vhResizeCleanup* cmd = vhCmdAlloc<VIDL_vhResizeCleanup>();
    vhCmdEnqueue( cmd );
}

void vhBeginTimerQuery( vhTimerID timerID )
{
    VIDL_vhBeginTimerQuery* cmd = vhCmdAlloc<VIDL_vhBeginTimerQuery>( timerID );
    vhCmdEnqueue( cmd );
}

void vhEndTimerQuery( vhTimerID timerID )
{
    VIDL_vhEndTimerQuery* cmd = vhCmdAlloc<VIDL_vhEndTimerQuery>( timerID );
    vhCmdEnqueue( cmd );
}

float vhGetTimerQueryTime( vhTimerID timerID )
{
    return vhBackendQueryTimer( timerID );
}

// -------------------------------------------------------- Dummy Resources --------------------------------------------------------

static nvrhi::BufferHandle s_vhDummyOmniBuffer = nullptr;
static nvrhi::SamplerHandle s_vhDummySampler = nullptr;
static nvrhi::TextureHandle s_vhDummyTextures[10][3] = { 0 }; // [Dim][Float/UInt/SInt] - 10 texture dimensions

void vhInitDummyResources()
{
    // Ensure we have a device
    if ( !g_vhDevice ) return;

    // Check if already initialised
    if ( s_vhDummyOmniBuffer ) return;

    VRHI_LOG( "    Initialising dummy resources...\n" );

    nvrhi::CommandListHandle cl = g_vhDevice->createCommandList();
    cl->open();

    // Create The Omni-Buffer
    nvrhi::BufferDesc bDesc;
    bDesc.byteSize = 4096;
    bDesc.structStride = 4;
    bDesc.debugName = "DummyOmniBuffer";
    bDesc.isConstantBuffer = true;
    bDesc.isVolatile = false;
    bDesc.canHaveUAVs = true;
    bDesc.canHaveTypedViews = true;
    bDesc.canHaveRawViews = true;
    bDesc.format = nvrhi::Format::R32_FLOAT;
    bDesc.initialState = nvrhi::ResourceStates::Common;
    bDesc.keepInitialState = true;

    s_vhDummyOmniBuffer = g_vhDevice->createBuffer( bDesc );
    cl->clearBufferUInt( s_vhDummyOmniBuffer, 0 );

    // Create Texture Permutations
    std::vector< nvrhi::TextureDimension > dims =
    {
        nvrhi::TextureDimension::Texture2D,
        nvrhi::TextureDimension::Texture2DArray,
        nvrhi::TextureDimension::Texture3D,
        nvrhi::TextureDimension::TextureCube,
        nvrhi::TextureDimension::TextureCubeArray
    };

    for ( auto dim : dims )
    {
        for ( int i = 0; i < 3; ++i )
        {
            nvrhi::TextureDesc tDesc;
            tDesc.dimension = dim;
            tDesc.width = 1;
            tDesc.height = 1;
            tDesc.depth = ( dim == nvrhi::TextureDimension::Texture3D ) ? 1 : 1;
            tDesc.arraySize = ( dim == nvrhi::TextureDimension::TextureCube || dim == nvrhi::TextureDimension::TextureCubeArray ) ? 6 : 1;
            tDesc.mipLevels = 1;
            tDesc.isShaderResource = true;
            tDesc.isUAV = true;
            tDesc.enableAutomaticStateTracking( nvrhi::ResourceStates::ShaderResource );
            tDesc.debugName = "DummyTexture";

            if ( i == 0 ) tDesc.format = nvrhi::Format::RGBA8_UNORM;      // Float
            else if ( i == 1 ) tDesc.format = nvrhi::Format::R8_UINT;     // UInt
            else if ( i == 2 ) tDesc.format = nvrhi::Format::R8_SINT;     // SInt

            nvrhi::TextureHandle handle = g_vhDevice->createTexture( tDesc );
            s_vhDummyTextures[( int ) dim][i] = handle;

            if ( i == 0 )
            {
                nvrhi::Color clearColour( 0.f );
                cl->clearTextureFloat( handle, nvrhi::AllSubresources, clearColour );
            }
            else
            {
                cl->clearTextureUInt( handle, nvrhi::AllSubresources, 0 );
            }
        }
    }

    // Create Sampler
    nvrhi::SamplerDesc sDesc;
    sDesc.addressU = sDesc.addressV = sDesc.addressW = nvrhi::SamplerAddressMode::Clamp;
    s_vhDummySampler = g_vhDevice->createSampler( sDesc );

    cl->close();
    g_vhDevice->executeCommandList( cl );
}

void vhShutdownDummyResources()
{
    s_vhDummyOmniBuffer = nullptr;
    s_vhDummySampler = nullptr;
    for ( int i = 0; i < 10; ++i )
    {
        for ( int j = 0; j < 3; ++j )
        {
            s_vhDummyTextures[i][j] = nullptr;
        }
    }
}

nvrhi::BindingSetItem vhGetDummyBindingItem( const nvrhi::BindingLayoutItem& layoutItem, nvrhi::Format expectedFormat, nvrhi::TextureDimension expectedDim )
{
    using namespace nvrhi;

    // Buffer Fallback (OmniBuffer covers all)
    if ( layoutItem.type == ResourceType::ConstantBuffer || layoutItem.type == ResourceType::VolatileConstantBuffer )
        return BindingSetItem::ConstantBuffer( layoutItem.slot, s_vhDummyOmniBuffer );

    if ( layoutItem.type == ResourceType::StructuredBuffer_SRV )
        return BindingSetItem::StructuredBuffer_SRV( layoutItem.slot, s_vhDummyOmniBuffer );
    if ( layoutItem.type == ResourceType::StructuredBuffer_UAV )
        return BindingSetItem::StructuredBuffer_UAV( layoutItem.slot, s_vhDummyOmniBuffer );
    if ( layoutItem.type == ResourceType::RawBuffer_SRV )
        return BindingSetItem::RawBuffer_SRV( layoutItem.slot, s_vhDummyOmniBuffer );
    if ( layoutItem.type == ResourceType::RawBuffer_UAV )
        return BindingSetItem::RawBuffer_UAV( layoutItem.slot, s_vhDummyOmniBuffer );

    // Use expectedFormat so the View is created with the correct Type
    if ( layoutItem.type == ResourceType::TypedBuffer_SRV )
        return BindingSetItem::TypedBuffer_SRV( layoutItem.slot, s_vhDummyOmniBuffer, expectedFormat );
    if ( layoutItem.type == ResourceType::TypedBuffer_UAV )
        return BindingSetItem::TypedBuffer_UAV( layoutItem.slot, s_vhDummyOmniBuffer, expectedFormat );

    // Sampler Fallback
    if ( layoutItem.type == ResourceType::Sampler )
        return BindingSetItem::Sampler( layoutItem.slot, s_vhDummySampler );

    // Texture Fallback
    if ( layoutItem.type == ResourceType::Texture_SRV || layoutItem.type == ResourceType::Texture_UAV )
    {
        // Determine Format Mode
        int mode = 0; // Float
        const auto& fmtInfo = getFormatInfo( expectedFormat );
        if ( fmtInfo.kind == FormatKind::Integer )
        {
            mode = fmtInfo.isSigned ? 2 : 1; // SInt : UInt
        }

        // Look up the texture
        if ( expectedDim == TextureDimension::Unknown ) expectedDim = TextureDimension::Texture2D;
        nvrhi::TextureHandle tex = s_vhDummyTextures[( int ) expectedDim][mode];
        if ( !tex ) tex = s_vhDummyTextures[( int ) TextureDimension::Texture2D][mode]; // Fallback

        if ( layoutItem.type == ResourceType::Texture_SRV )
            return BindingSetItem::Texture_SRV( layoutItem.slot, tex, expectedFormat );
        else
            return BindingSetItem::Texture_UAV( layoutItem.slot, tex, expectedFormat );
    }

    return BindingSetItem::None( layoutItem.slot );
}

// -------------------------------------------------------- PSO Cache --------------------------------------------------------

uint64_t vhHashBindingLayout( const nvrhi::BindingLayoutDesc& desc )
{
    // static_assert( sizeof( nvrhi::BindingLayoutDesc ) == 64, "nvrhi::BindingLayoutDesc size mismatch" );
    // static_assert( sizeof( nvrhi::BindingLayoutItem ) == 8, "nvrhi::BindingLayoutItem size mismatch" );

    uint64_t h = 0;
    h = komihash( &desc.visibility, sizeof( desc.visibility ), h );
    h = komihash( &desc.registerSpace, sizeof( desc.registerSpace ), h );
    h = komihash( &desc.registerSpaceIsDescriptorSet, sizeof( desc.registerSpaceIsDescriptorSet ), h );

    for ( const auto& binding : desc.bindings )
    {
        uint32_t slot = binding.slot;
        h = komihash( &slot, sizeof( slot ), h );

        nvrhi::ResourceType type = binding.type;
        h = komihash( &type, sizeof( type ), h );

        uint16_t sz = binding.size;
        h = komihash( &sz, sizeof( sz ), h );
    }
    return h;
}

uint64_t vhHashShaderDebugName( nvrhi::ShaderHandle shader )
{
    if ( !shader ) return 0;
    const std::string& debugName = shader->getDesc().debugName;
    if ( !debugName.empty() )
    {
        return komihash( debugName.data(), debugName.size(), 0 );
    }
    return 0;
}

uint64_t vhHashShaderSPIRV( const std::vector< uint32_t >& spirv )
{
    auto sz = spirv.size();
    uint64_t h = 0;
    h = komihash( &sz, sizeof( sz ), h );
    h = komihash( spirv.data(), sz * sizeof( uint32_t ), h );
    return h;
}

uint64_t vhHashInputLayout( nvrhi::InputLayoutHandle layout )
{
    // static_assert( sizeof( nvrhi::VertexAttributeDesc ) == 64, "nvrhi::VertexAttributeDesc size mismatch" );
    if ( !layout ) return 0;
    uint64_t h = 0;
    uint32_t count = layout->getNumAttributes();

    for ( uint32_t i = 0; i < count; ++i )
    {
        const nvrhi::VertexAttributeDesc* attr = layout->getAttributeDesc( i );
        if ( attr )
        {
            h = komihash( attr->name.data(), attr->name.size(), h );
            h = komihash( &attr->format, sizeof( attr->format ), h );
            h = komihash( &attr->arraySize, sizeof( attr->arraySize ), h );
            h = komihash( &attr->bufferIndex, sizeof( attr->bufferIndex ), h );
            h = komihash( &attr->offset, sizeof( attr->offset ), h );
            h = komihash( &attr->elementStride, sizeof( attr->elementStride ), h );
            h = komihash( &attr->isInstanced, sizeof( attr->isInstanced ), h );
        }
    }
    return h;
}

static uint64_t vhHashRenderState( const nvrhi::RenderState& rs )
{
    static_assert( sizeof( nvrhi::RenderState ) == 144, "nvrhi::RenderState size mismatch" );
    uint64_t h = 0;

    // Blend State

    h = komihash( &rs.blendState.alphaToCoverageEnable, sizeof( rs.blendState.alphaToCoverageEnable ), h );
    for ( const auto& rt : rs.blendState.targets )
    {
        h = komihash( &rt.blendEnable, sizeof( rt.blendEnable ), h );
        h = komihash( &rt.srcBlend, sizeof( rt.srcBlend ), h );
        h = komihash( &rt.destBlend, sizeof( rt.destBlend ), h );
        h = komihash( &rt.blendOp, sizeof( rt.blendOp ), h );
        h = komihash( &rt.srcBlendAlpha, sizeof( rt.srcBlendAlpha ), h );
        h = komihash( &rt.destBlendAlpha, sizeof( rt.destBlendAlpha ), h );
        h = komihash( &rt.blendOpAlpha, sizeof( rt.blendOpAlpha ), h );
        h = komihash( &rt.colorWriteMask, sizeof( rt.colorWriteMask ), h );
    }

    // Depth Stencil State

    const auto& dss = rs.depthStencilState;
    h = komihash( &dss.depthTestEnable, sizeof( dss.depthTestEnable ), h );
    h = komihash( &dss.depthWriteEnable, sizeof( dss.depthWriteEnable ), h );
    h = komihash( &dss.depthFunc, sizeof( dss.depthFunc ), h );
    h = komihash( &dss.stencilEnable, sizeof( dss.stencilEnable ), h );
    h = komihash( &dss.stencilReadMask, sizeof( dss.stencilReadMask ), h );
    h = komihash( &dss.stencilWriteMask, sizeof( dss.stencilWriteMask ), h );
    h = komihash( &dss.stencilRefValue, sizeof( dss.stencilRefValue ), h );
    h = komihash( &dss.dynamicStencilRef, sizeof( dss.dynamicStencilRef ), h );

    h = komihash( &dss.frontFaceStencil.failOp, sizeof( dss.frontFaceStencil.failOp ), h );
    h = komihash( &dss.frontFaceStencil.depthFailOp, sizeof( dss.frontFaceStencil.depthFailOp ), h );
    h = komihash( &dss.frontFaceStencil.passOp, sizeof( dss.frontFaceStencil.passOp ), h );
    h = komihash( &dss.frontFaceStencil.stencilFunc, sizeof( dss.frontFaceStencil.stencilFunc ), h );

    h = komihash( &dss.backFaceStencil.failOp, sizeof( dss.backFaceStencil.failOp ), h );
    h = komihash( &dss.backFaceStencil.depthFailOp, sizeof( dss.backFaceStencil.depthFailOp ), h );
    h = komihash( &dss.backFaceStencil.passOp, sizeof( dss.backFaceStencil.passOp ), h );
    h = komihash( &dss.backFaceStencil.stencilFunc, sizeof( dss.backFaceStencil.stencilFunc ), h );

    // Raster State

    const auto& ras = rs.rasterState;
    h = komihash( &ras.fillMode, sizeof( ras.fillMode ), h );
    h = komihash( &ras.cullMode, sizeof( ras.cullMode ), h );
    h = komihash( &ras.frontCounterClockwise, sizeof( ras.frontCounterClockwise ), h );
    h = komihash( &ras.depthClipEnable, sizeof( ras.depthClipEnable ), h );
    h = komihash( &ras.scissorEnable, sizeof( ras.scissorEnable ), h );
    h = komihash( &ras.multisampleEnable, sizeof( ras.multisampleEnable ), h );
    h = komihash( &ras.antialiasedLineEnable, sizeof( ras.antialiasedLineEnable ), h );
    h = komihash( &ras.depthBias, sizeof( ras.depthBias ), h );
    h = komihash( &ras.depthBiasClamp, sizeof( ras.depthBiasClamp ), h );
    h = komihash( &ras.slopeScaledDepthBias, sizeof( ras.slopeScaledDepthBias ), h );
    h = komihash( &ras.forcedSampleCount, sizeof( ras.forcedSampleCount ), h );
    h = komihash( &ras.programmableSamplePositionsEnable, sizeof( ras.programmableSamplePositionsEnable ), h );
    h = komihash( &ras.conservativeRasterEnable, sizeof( ras.conservativeRasterEnable ), h );
    h = komihash( &ras.quadFillEnable, sizeof( ras.quadFillEnable ), h );

    for ( int i = 0; i < 16; ++i )
    {
        h = komihash( &ras.samplePositionsX[i], sizeof( ras.samplePositionsX[i] ), h );
        h = komihash( &ras.samplePositionsY[i], sizeof( ras.samplePositionsY[i] ), h );
    }

    // Single Pass Stereo

    h = komihash( &rs.singlePassStereo.enabled, sizeof( rs.singlePassStereo.enabled ), h );
    h = komihash( &rs.singlePassStereo.independentViewportMask, sizeof( rs.singlePassStereo.independentViewportMask ), h );
    h = komihash( &rs.singlePassStereo.renderTargetIndexOffset, sizeof( rs.singlePassStereo.renderTargetIndexOffset ), h );

    return h;
}

static uint64_t vhHashFramebufferInfo( const nvrhi::FramebufferInfo& fb )
{
    static_assert( sizeof( nvrhi::FramebufferInfo ) == 32, "nvrhi::FramebufferInfo size mismatch" );
    uint64_t h = 0;
    for ( auto fmt : fb.colorFormats ) h = komihash( &fmt, sizeof( fmt ), h );
    h = komihash( &fb.depthFormat, sizeof( fb.depthFormat ), h );
    h = komihash( &fb.sampleCount, sizeof( fb.sampleCount ), h );
    h = komihash( &fb.sampleQuality, sizeof( fb.sampleQuality ), h );
    return h;
}

uint64_t vhHashGraphicsPipeline( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& fbInfo )
{
    static_assert( sizeof( nvrhi::GraphicsPipelineDesc ) == 280, "nvrhi::GraphicsPipelineDesc size mismatch" );
    uint64_t h = 0;

    // Core State

    h = komihash( &desc.primType, sizeof( desc.primType ), h );
    h = komihash( &desc.patchControlPoints, sizeof( desc.patchControlPoints ), h );

    // Input Layout

    uint64_t hInput = vhHashInputLayout( desc.inputLayout );
    h = komihash( &hInput, sizeof( hInput ), h );

    // Shaders

    uint64_t hVS = vhHashShaderDebugName( desc.VS ); h = komihash( &hVS, sizeof( hVS ), h );
    uint64_t hHS = vhHashShaderDebugName( desc.HS ); h = komihash( &hHS, sizeof( hHS ), h );
    uint64_t hDS = vhHashShaderDebugName( desc.DS ); h = komihash( &hDS, sizeof( hDS ), h );
    uint64_t hGS = vhHashShaderDebugName( desc.GS ); h = komihash( &hGS, sizeof( hGS ), h );
    uint64_t hPS = vhHashShaderDebugName( desc.PS ); h = komihash( &hPS, sizeof( hPS ), h );

    // Render State

    uint64_t hRS = vhHashRenderState( desc.renderState );
    h = komihash( &hRS, sizeof( hRS ), h );

    // Variable Rate Shading

    h = komihash( &desc.shadingRateState.enabled, sizeof( desc.shadingRateState.enabled ), h );
    h = komihash( &desc.shadingRateState.shadingRate, sizeof( desc.shadingRateState.shadingRate ), h );
    h = komihash( &desc.shadingRateState.pipelinePrimitiveCombiner, sizeof( desc.shadingRateState.pipelinePrimitiveCombiner ), h );
    h = komihash( &desc.shadingRateState.imageCombiner, sizeof( desc.shadingRateState.imageCombiner ), h );

    // Binding Layouts

    for ( const auto& layoutHandle : desc.bindingLayouts )
    {
        const nvrhi::BindingLayoutDesc* layoutDesc = layoutHandle->getDesc();
        if ( layoutDesc )
        {
            uint64_t hLayout = vhHashBindingLayout( *layoutDesc );
            h = komihash( &hLayout, sizeof( hLayout ), h );
        }
    }

    // Framebuffer Compatibility

    uint64_t hFB = vhHashFramebufferInfo( fbInfo );
    h = komihash( &hFB, sizeof( hFB ), h );

    return h;
}

uint64_t vhHashComputePipeline( const nvrhi::ComputePipelineDesc& desc )
{
    static_assert( sizeof( nvrhi::ComputePipelineDesc ) == 80, "nvrhi::ComputePipelineDesc size mismatch" );
    uint64_t h = 0;

    uint64_t hCS = vhHashShaderDebugName( desc.CS );
    h = komihash( &hCS, sizeof( hCS ), h );

    for ( const auto& layoutHandle : desc.bindingLayouts )
    {
        assert( layoutHandle );
        const nvrhi::BindingLayoutDesc* layoutDesc = layoutHandle->getDesc();
        if ( layoutDesc )
        {
            uint64_t hLayout = vhHashBindingLayout( *layoutDesc );
            h = komihash( &hLayout, sizeof( hLayout ), h );
        }
    }

    return h;
}

static std::unordered_map< uint64_t, nvrhi::ComputePipelineHandle > s_PSOCache_Compute;
static std::unordered_map< uint64_t, nvrhi::GraphicsPipelineHandle > s_PSOCache_Graphics;

void vhPSOCacheShutdown()
{
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    s_PSOCache_Compute.clear();
    s_PSOCache_Graphics.clear();
}

nvrhi::ComputePipelineHandle vhPSOCacheGet( const nvrhi::ComputePipelineDesc& desc )
{
    uint64_t hash = vhHashComputePipeline( desc );

    auto it = s_PSOCache_Compute.find( hash );
    if ( it != s_PSOCache_Compute.end() )
    {
        if ( g_vhInit.logPSOCache ) VRHI_LOG( "vhPSOCacheGet() : Compute PSO hash 0x%llx found in cache.\n", hash );
        return it->second;
    }
    if ( g_vhInit.logPSOCache ) VRHI_LOG( "vhPSOCacheGet() : Compute PSO hash 0x%llx cache missing. Compiling.\n", hash );

    nvrhi::ComputePipelineHandle pso = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        pso = g_vhDevice->createComputePipeline( desc );
        s_PSOCache_Compute[hash] = pso;
        g_vhPSOCompileCounter++;
    }
    return pso;
}

nvrhi::GraphicsPipelineHandle vhPSOCacheGet( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& fbInfo )
{
    uint64_t hash = vhHashGraphicsPipeline( desc, fbInfo );

    auto it = s_PSOCache_Graphics.find( hash );
    if ( it != s_PSOCache_Graphics.end() )
    {
        if ( g_vhInit.logPSOCache ) VRHI_LOG( "vhPSOCacheGet() : Graphics PSO hash 0x%llx found in cache.\n", hash );
        return it->second;
    }
    if ( g_vhInit.logPSOCache ) VRHI_LOG( "vhPSOCacheGet() : Graphics PSO hash 0x%llx cache missing. Compiling.\n", hash );

    nvrhi::GraphicsPipelineHandle pso = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        pso = g_vhDevice->createGraphicsPipeline( desc, fbInfo );
        s_PSOCache_Graphics[hash] = pso;
        g_vhPSOCompileCounter++;
    }
    return pso;
}

uint64_t vhHashBindingSet( const nvrhi::BindingSetDesc& desc, nvrhi::BindingLayoutHandle layout )
{
    static_assert( sizeof( nvrhi::BindingSetItem ) == 40, "nvrhi::BindingSetItem size mismatch" );
    uint64_t h = 0;

    void* pLayout = layout.Get();
    h = komihash( &pLayout, sizeof( pLayout ), h );
    h = komihash( &desc.trackLiveness, sizeof( desc.trackLiveness ), h );

    for ( const auto& item : desc.bindings )
    {
        h = komihash( &item.slot, sizeof( item.slot ), h );
        h = komihash( &item.arrayElement, sizeof( item.arrayElement ), h );

        uint32_t itemType = ( uint32_t ) item.type;
        h = komihash( &itemType, sizeof( itemType ), h );
        h = komihash( &item.resourceHandle, sizeof( item.resourceHandle ), h );

        if ( item.resourceHandle )
        {
            uint32_t itemFormat = ( uint32_t ) item.format;
            uint32_t itemDimension = ( uint32_t ) item.dimension;
            h = komihash( &itemFormat, sizeof( itemFormat ), h );
            h = komihash( &itemDimension, sizeof( itemDimension ), h );
            h = komihash( &item.rawData[0], sizeof( item.rawData[0] ), h );
            h = komihash( &item.rawData[1], sizeof( item.rawData[1] ), h );
        }
    }

    return h;
}

static std::unordered_map< uint64_t, nvrhi::BindingSetHandle > s_bindingSetCache;

void vhBindingSetCacheClear()
{
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    s_bindingSetCache.clear();
}

nvrhi::BindingSetHandle vhGetBindingSet( const nvrhi::BindingSetDesc& desc, nvrhi::BindingLayoutHandle layout )
{
    uint64_t hash = vhHashBindingSet( desc, layout );

    auto it = s_bindingSetCache.find( hash );
    if ( it != s_bindingSetCache.end() )
        return it->second;

    nvrhi::BindingSetHandle bset = nullptr;
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        bset = g_vhDevice->createBindingSet( desc, layout );
        s_bindingSetCache[hash] = bset;
    }
    return bset;
}

uint64_t vhHashSamplerDesc( const nvrhi::SamplerDesc& desc )
{
    static_assert( sizeof( nvrhi::SamplerDesc ) == 32, "nvrhi::SamplerDesc size mismatch" );
    uint64_t h = 0;
    
    h = komihash( &desc.borderColor, sizeof( desc.borderColor ), h );
    h = komihash( &desc.maxAnisotropy, sizeof( desc.maxAnisotropy ), h );
    h = komihash( &desc.mipBias, sizeof( desc.mipBias ), h );
    h = komihash( &desc.minFilter, sizeof( desc.minFilter ), h );
    h = komihash( &desc.magFilter, sizeof( desc.magFilter ), h );
    h = komihash( &desc.mipFilter, sizeof( desc.mipFilter ), h );
    h = komihash( &desc.addressU, sizeof( desc.addressU ), h );
    h = komihash( &desc.addressV, sizeof( desc.addressV ), h );
    h = komihash( &desc.addressW, sizeof( desc.addressW ), h );
    h = komihash( &desc.reductionType, sizeof( desc.reductionType ), h );

    return h;
}

uint64_t vhHashGlobalUniform( const vhGlobalUniform& u )
{
    return komihash( &u, sizeof( u ), 0 );
}

uint64_t vhHashWorldUniform( const vhWorldUniform& u )
{
    return komihash( &u, sizeof( u ), 0 );
}

uint64_t vhHashReflectionMembers( const std::vector< vhReflectionMember >& members )
{
    uint64_t h = 0;

    size_t count = members.size();
    h = komihash( &count, sizeof( count ), h );

    for ( const auto& member : members )
    {
        if ( !member.name.empty() )
        {
            h = komihash( member.name.data(), member.name.size(), h );
        }
        else
        {
            uint32_t marker = 0xdeadbeef;
            h = komihash( &marker, sizeof( marker ), h );
        }
        h = komihash( &member.offset, sizeof( member.offset ), h );
        h = komihash( &member.size, sizeof( member.size ), h );
    }

    return h;
}

uint64_t vhHashFrameBuffer( const nvrhi::FramebufferDesc& desc )
{
    uint64_t h = 0;
    for ( const auto& at : desc.colorAttachments )
    {
        h = komihash( &at.texture, sizeof( at.texture ), h );
        h = komihash( &at.subresources, sizeof( at.subresources ), h );
        h = komihash( &at.format, sizeof( at.format ), h );
        h = komihash( &at.isReadOnly, sizeof( at.isReadOnly ), h );
    }
    h = komihash( &desc.depthAttachment.texture, sizeof( desc.depthAttachment.texture ), h );
    h = komihash( &desc.depthAttachment.subresources, sizeof( desc.depthAttachment.subresources ), h );
    h = komihash( &desc.depthAttachment.format, sizeof( desc.depthAttachment.format ), h );
    h = komihash( &desc.depthAttachment.isReadOnly, sizeof( desc.depthAttachment.isReadOnly ), h );
    
    // Also hash shading rate if needed, though not explicitly requested, good practice to match struct
    h = komihash( &desc.shadingRateAttachment.texture, sizeof( desc.shadingRateAttachment.texture ), h );
    
    return h;
}

static std::unordered_map< uint64_t, nvrhi::FramebufferHandle > s_FBOCache;

void vhFBOCacheReset()
{
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    s_FBOCache.clear();
}

nvrhi::FramebufferHandle vhFBOCacheGet( const nvrhi::FramebufferDesc& desc )
{
    uint64_t hash = vhHashFrameBuffer( desc );
    
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    auto it = s_FBOCache.find( hash );
    if ( it != s_FBOCache.end() )
        return it->second;
        
    nvrhi::FramebufferHandle fb = g_vhDevice->createFramebuffer( desc );
    if ( fb ) s_FBOCache[hash] = fb;
    return fb;
}

void vhDrawCommonInternal(
    vhStateId state,
    uint32_t flags,
    uint32_t vertexCount,
    uint32_t instanceCount,
    uint32_t startVertexLocation,
    uint32_t startIndexLocation,
    uint32_t startInstanceLocation,
    uint32_t drawCount
)
{
    VIDL_vhDrawCommonInternal* cmd = vhCmdAlloc<VIDL_vhDrawCommonInternal>( state, flags, vertexCount, instanceCount, startVertexLocation, startIndexLocation, startInstanceLocation, drawCount );
    vhCmdEnqueue( cmd );
}

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
#include "vrhi_impl_backend.h"
#include "vrhi_utils.h"
#include <nvrhi/validation.h>

struct vhVKDeviceScore
{
    bool isSuitable = false;
    int deviceClass = 0;      // 3=discrete, 2=integrated, 1=virtual, 0=cpu/other
    uint64_t totalScore = 0;  // weighted sum for micro-sort
    
    uint32_t apiVersion = 0;  // packed VK version
    uint64_t vramBytes = 0;   // local heap size
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t pciBus = 0, pciDevice = 0, pciFunction = 0;
    uint8_t deviceUUID[VK_UUID_SIZE] = {0};
    std::string name;
    VkPhysicalDevice handle = VK_NULL_HANDLE;
};

class vhVK_MessageCallback : public nvrhi::IMessageCallback
{
public:
    void message( nvrhi::MessageSeverity severity, const char* messageText ) override
    {
        if ( severity >= nvrhi::MessageSeverity::Error )
        {
            VRHI_LOG( "[NVRHI] %s\n", messageText );
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

static bool vhVKCheckLayerSupport_Internal( const char* layerName )
{
    uint32_t count;
    vkEnumerateInstanceLayerProperties( &count, nullptr );
    std::vector<VkLayerProperties> layers( count );
    vkEnumerateInstanceLayerProperties( &count, layers.data() );
    for ( const auto& l : layers )
    {
        if ( strcmp( layerName, l.layerName ) == 0 ) return true;
    }
    return false;
}

static inline int vhCountSetBits( uint32_t n )
{
    int count = 0;
    while ( n > 0 )
    {
        n &= ( n - 1 );
        count++;
    }
    return count;
}

// Not static here so we can extern it in tests.
int vhVKRatePhysicalDeviceProps_Internal( const VkPhysicalDeviceProperties& props )
{
    // Minimal version for the simple rating function
    if ( props.apiVersion < VK_API_VERSION_1_1 ) return 0;

    int score = 0;
    if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) score = 3;
    else if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) score = 2;
    else if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ) score = 1;
    return score;
}

static vhVKDeviceScore vhVKCalculateDeviceScore( VkPhysicalDevice gpu )
{
    vhVKDeviceScore score = {};
    score.handle = gpu;

    // 1. Enumerate device extensions once
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties( gpu, nullptr, &extCount, nullptr );
    std::vector<VkExtensionProperties> extProps( extCount );
    vkEnumerateDeviceExtensionProperties( gpu, nullptr, &extCount, extProps.data() );
    std::unordered_set<std::string> exts;
    for ( const auto& e : extProps ) exts.insert( e.extensionName );

    // 2. Query Properties2
    VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    VkPhysicalDeviceIDProperties idProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDevicePCIBusInfoPropertiesEXT pciProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT };
    pciProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
    
    props2.pNext = &idProps;
    if ( exts.count( VK_EXT_PCI_BUS_INFO_EXTENSION_NAME ) )
    {
        idProps.pNext = &pciProps;
    }
    vkGetPhysicalDeviceProperties2( gpu, &props2 );

    // 3. Query Features2
    VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan11Features v11Feat = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    v11Feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceVulkan12Features v12Feat = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12Feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features v13Feat = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    v13Feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    
    feat2.pNext = &v11Feat;
    v11Feat.pNext = &v12Feat;
    v12Feat.pNext = &v13Feat;
    vkGetPhysicalDeviceFeatures2( gpu, &feat2 );

    // 4. Query MemoryProperties2
    VkPhysicalDeviceMemoryProperties2 memProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
    memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vkGetPhysicalDeviceMemoryProperties2( gpu, &memProps2 );

    // Fill Basic Info
    score.name = props2.properties.deviceName;
    score.apiVersion = props2.properties.apiVersion;
    score.vendorId = props2.properties.vendorID;
    score.deviceId = props2.properties.deviceID;
    memcpy( score.deviceUUID, idProps.deviceUUID, VK_UUID_SIZE );
    if ( exts.count( VK_EXT_PCI_BUS_INFO_EXTENSION_NAME ) )
    {
        score.pciBus = pciProps.pciBus;
        score.pciDevice = pciProps.pciDevice;
        score.pciFunction = pciProps.pciFunction;
    }

    // High-level class ordering
    switch ( props2.properties.deviceType )
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score.deviceClass = 3; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score.deviceClass = 2; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score.deviceClass = 1; break;
        default:                                     score.deviceClass = 0; break;
    }

    // Suitability check (NVRHI / Engine requirements)
    bool hasSwapchain = exts.count( VK_KHR_SWAPCHAIN_EXTENSION_NAME ) > 0;
    bool hasTimeline = ( score.apiVersion >= VK_API_VERSION_1_2 ) || ( exts.count( VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME ) > 0 );
    bool hasBDA = ( score.apiVersion >= VK_API_VERSION_1_2 ) || ( exts.count( VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME ) > 0 );
    
    score.isSuitable = hasSwapchain && hasTimeline && hasBDA && ( score.apiVersion >= VK_API_VERSION_1_1 );

    // Micro-scoring weights
    uint64_t microScore = 0;
    
    // API Version: 1,000,000 per major, 10,000 per minor, 100 per patch
    microScore += ( uint64_t ) VK_API_VERSION_MAJOR( score.apiVersion ) * 1000000;
    microScore += ( uint64_t ) VK_API_VERSION_MINOR( score.apiVersion ) * 10000;
    microScore += ( uint64_t ) VK_API_VERSION_PATCH( score.apiVersion ) * 100;

    // Features / Extensions: +50,000 for each modern capability
    auto addFeatureScore = [&]( bool coreFeature, const char* extName, uint32_t coreVersion )
    {
        if ( score.apiVersion >= coreVersion || coreFeature || exts.count( extName ) ) {
            microScore += 50000;
        }
    };

    addFeatureScore( v13Feat.synchronization2, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_API_VERSION_1_3 );
    addFeatureScore( v13Feat.dynamicRendering, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_API_VERSION_1_3 );
    addFeatureScore( v12Feat.timelineSemaphore, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, VK_API_VERSION_1_2 );
    addFeatureScore( v12Feat.bufferDeviceAddress, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, VK_API_VERSION_1_2 );
    addFeatureScore( v12Feat.descriptorIndexing, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, VK_API_VERSION_1_2 );
    addFeatureScore( false, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, 0xFFFFFFFF );
    addFeatureScore( v13Feat.maintenance4, VK_KHR_MAINTENANCE_4_EXTENSION_NAME, VK_API_VERSION_1_3 );
    addFeatureScore( v12Feat.shaderFloat16, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME, VK_API_VERSION_1_2 );

    // VRAM weight: 1 point per MB, capped at 32GB (32768 points)
    for ( uint32_t i = 0; i < memProps2.memoryProperties.memoryHeapCount; ++i )
    {
        if ( memProps2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT )
        {
            score.vramBytes = (std::max)( score.vramBytes, memProps2.memoryProperties.memoryHeaps[i].size );
        }
    }
    microScore += (std::min)( (uint64_t)(score.vramBytes / ( 1024 * 1024 )), (uint64_t)32768 );

    // Limits weights: normalized/clamped small points
    microScore += (std::min)( props2.properties.limits.maxImageDimension2D, 16384u ) / 100;
    microScore += (std::min)( props2.properties.limits.maxPerStageDescriptorSamplers, 1024u ) / 10;
    microScore += (std::min)( props2.properties.limits.maxComputeWorkGroupInvocations, 1024u ) / 10;

    score.totalScore = microScore;
    return score;
}

static void vhVKFilterExtensions( VkPhysicalDevice device, std::vector<const char*>& requested, std::vector<const char*>& supported )
{
    uint32_t count;
    vkEnumerateDeviceExtensionProperties( device, nullptr, &count, nullptr );
    std::vector<VkExtensionProperties> available( count );
    vkEnumerateDeviceExtensionProperties( device, nullptr, &count, available.data() );

    for ( const char* req : requested )
    {
        bool found = false;
        for ( const auto& avail : available )
        {
            if ( strcmp( req, avail.extensionName ) == 0 )
            {
                found = true;
                break;
            }
        }

        if ( !found )
        {
            VRHI_LOG( "WARNING: Extension %s not supported, skipping.\n", req );
            continue;
        }

        supported.push_back( req );
    }
}

// Not static so it can be externed for testing
uint32_t vhVKFindDedicatedQueue_Internal( uint32_t qCount, const VkQueueFamilyProperties* qProps, VkQueueFlags required, VkQueueFlags avoid )
{
    uint32_t best = ( uint32_t ) -1;
    int bestExtraFlags = INT_MAX;

    for ( uint32_t i = 0; i < qCount; i++ )
    {
        VkQueueFlags flags = qProps[i].queueFlags;

        if ( ( flags & required ) != required ) continue;
        if ( ( flags & avoid ) != 0 ) continue;

        int extraFlags = vhCountSetBits( flags & ~required );
        if ( extraFlags < bestExtraFlags )
        {
            bestExtraFlags = extraFlags;
            best = i;
        }
    }
    return best;
}

// -------------------------------------------------------- RHI Device --------------------------------------------------------

void vhInit()
{
    VRHI_LOG( "Initialising Vulkan RHI ...\n" );

    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    if ( g_vhDevice ) 
    {
        VRHI_LOG( "vhInit() : RHI already initialised!\n" );
        return;
    }

    // 1. Create VkInstance

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = g_vhInit.appName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
    appInfo.pEngineName = g_vhInit.engineName.c_str();
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector< const char* > instanceExtensions;
    std::vector< const char* > layers;

    instanceExtensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
#if defined(_WIN32)
    instanceExtensions.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#elif defined(__linux__)
    instanceExtensions.push_back( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
#endif

    if ( g_vhInit.debug && vhVKCheckLayerSupport_Internal( "VK_LAYER_KHRONOS_validation" ) )
    {
        VRHI_LOG( "    Enabling VK_LAYER_KHRONOS_validation\n" );
        layers.push_back( "VK_LAYER_KHRONOS_validation" );
        instanceExtensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
    }

    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledLayerCount = ( uint32_t ) layers.size();
    instInfo.ppEnabledLayerNames = layers.data();
    instInfo.enabledExtensionCount = ( uint32_t ) instanceExtensions.size();
    instInfo.ppEnabledExtensionNames = instanceExtensions.data();

    VRHI_LOG( "    Creating VK Instance\n" );
    if ( vkCreateInstance( &instInfo, nullptr, &g_vulkanInstance ) != VK_SUCCESS )
    {
        VRHI_LOG( "Failed to create Vulkan Instance!\n" );
        exit(1);
    }
    assert( g_vulkanInstance );

    // Initialise vulkan.hpp dynamic dispatcher with instance functions
    VRHI_LOG( "    Initialising vulkan.hpp dynamic dispatcher with instance functions\n" );
    VULKAN_HPP_DEFAULT_DISPATCHER.init( g_vulkanInstance, vkGetInstanceProcAddr );

    if ( g_vhInit.debug )
    {
        VRHI_LOG( "    Enabling debug layer\n" );
        VkDebugUtilsMessengerCreateInfoEXT dbgInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgInfo.pfnUserCallback = vhVKDebugCallback;
        auto fnCreateDebugUtilsMessengerEXT = ( PFN_vkCreateDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( g_vulkanInstance, "vkCreateDebugUtilsMessengerEXT" );
        if ( fnCreateDebugUtilsMessengerEXT )
        {
            fnCreateDebugUtilsMessengerEXT( g_vulkanInstance, &dbgInfo, nullptr, &g_vulkanDebugMessenger );
            VRHI_LOG( "    Debug layer enabled successfully via vkCreateDebugUtilsMessengerEXT.\n" );
        }
    }

    // 2. Physical Device Selection

    VRHI_LOG( "    Enumerating physical devices.\n" );
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices( g_vulkanInstance, &gpuCount, nullptr );
    if ( gpuCount == 0 )
    {
        VRHI_LOG( "No GPUs with Vulkan support found!\n" );
        exit(1);
    }
    std::vector< VkPhysicalDevice > gpus( gpuCount );
    vkEnumeratePhysicalDevices( g_vulkanInstance, &gpuCount, gpus.data() );

    std::vector<vhVKDeviceScore> candidates;
    for ( auto gpu : gpus )
    {
        candidates.push_back( vhVKCalculateDeviceScore( gpu ) );
    }

    auto comparator = []( const vhVKDeviceScore& a, const vhVKDeviceScore& b )
    {
        if ( a.isSuitable != b.isSuitable ) return a.isSuitable > b.isSuitable;
        if ( a.deviceClass != b.deviceClass ) return a.deviceClass > b.deviceClass;
        if ( a.totalScore != b.totalScore ) return a.totalScore > b.totalScore;
        
        // Deterministic ties broken by PCI bus ID if available
        if ( a.pciBus != b.pciBus ) return a.pciBus < b.pciBus;
        if ( a.pciDevice != b.pciDevice ) return a.pciDevice < b.pciDevice;
        if ( a.pciFunction != b.pciFunction ) return a.pciFunction < b.pciFunction;
        
        // else by device UUID
        int uuidCmp = memcmp( a.deviceUUID, b.deviceUUID, VK_UUID_SIZE );
        if ( uuidCmp != 0 ) return uuidCmp < 0;
        
        // else by device name
        return a.name < b.name;
    };

    std::sort( candidates.begin(), candidates.end(), comparator );

    VRHI_LOG( "    Ranked Physical Devices:\n" );
    for ( const auto& s : candidates )
    {
        VRHI_LOG( "        [%s] %s | Type: %d | API: %d.%d.%d | VRAM: %llu MB | Score: %llu\n",
            s.isSuitable ? "PASS" : "FAIL",
            s.name.c_str(), s.deviceClass,
            VK_API_VERSION_MAJOR( s.apiVersion ), VK_API_VERSION_MINOR( s.apiVersion ), VK_API_VERSION_PATCH( s.apiVersion ),
            s.vramBytes / ( 1024 * 1024 ),
            s.totalScore );
    }

    if ( g_vhInit.deviceIndex >= 0 && g_vhInit.deviceIndex < ( int ) gpuCount )
    {
        VRHI_LOG( "    Selecting device index %d from original list due to config.\n", g_vhInit.deviceIndex );
        g_vulkanPhysicalDevice = gpus[g_vhInit.deviceIndex];
    }
    else
    {
        for ( const auto& s : candidates )
        {
            if ( s.isSuitable )
            {
                g_vulkanPhysicalDevice = s.handle;
                break;
            }
        }
    }

    if ( !g_vulkanPhysicalDevice )
    {
        VRHI_LOG( "Failed to find suitable Physical VK Device (Vulkan 1.1+ with Timeline Semaphores and BDA required)!\n" );
        exit(1);
    }

    VkPhysicalDeviceProperties gpuProps;
    vkGetPhysicalDeviceProperties( g_vulkanPhysicalDevice, &gpuProps );
    VRHI_LOG( "    Selected GPU Device: %s\n", gpuProps.deviceName );

    // 3. Queue Families Selection

    VRHI_LOG( "    Enumerating VK Queue Families.\n" );
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties( g_vulkanPhysicalDevice, &qCount, nullptr );
    std::vector< VkQueueFamilyProperties > qProps( qCount );
    vkGetPhysicalDeviceQueueFamilyProperties( g_vulkanPhysicalDevice, &qCount, qProps.data() );

    auto fnQueueFlagStr = []( VkQueueFlags flags ) -> std::string
    {
        std::string s;
        if ( flags & VK_QUEUE_GRAPHICS_BIT ) s += "GRAPHICS ";
        if ( flags & VK_QUEUE_COMPUTE_BIT ) { if ( !s.empty() ) s += "| "; s += "COMPUTE "; }
        if ( flags & VK_QUEUE_TRANSFER_BIT ) { if ( !s.empty() ) s += "| "; s += "TRANSFER "; }
        if ( flags & VK_QUEUE_SPARSE_BINDING_BIT ) { if ( !s.empty() ) s += "| "; s += "SPARSE "; }
        if ( flags & VK_QUEUE_PROTECTED_BIT ) { if ( !s.empty() ) s += "| "; s += "PROTECTED "; }
        if ( flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR ) { if ( !s.empty() ) s += "| "; s += "VIDEO_DECODE "; }
        if ( flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR ) { if ( !s.empty() ) s += "| "; s += "VIDEO_ENCODE "; }
        if ( flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV ) { if ( !s.empty() ) s += "| "; s += "OPTICAL_FLOW "; }
        if ( s.empty() ) s = "NONE";
        return s;
    };

    for ( uint32_t i = 0; i < qCount; i++ )
    {
        auto& q = qProps[i];
        VRHI_LOG( "        Queue Family %d: ( %s ), Count %d\n", i, fnQueueFlagStr( q.queueFlags ).c_str(), q.queueCount );
    }

    VRHI_LOG( "    Selecting VK Queues.\n" );

    g_QueueFamilyGraphics = vhVKFindDedicatedQueue_Internal( qCount, qProps.data(), VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0 );
    if ( g_QueueFamilyGraphics == ( uint32_t ) -1 )
    {
        VRHI_LOG( "Failed to find a suitable Graphics + Compute queue family!\n" );
        exit(1);
    }

    g_QueueFamilyCompute = vhVKFindDedicatedQueue_Internal( qCount, qProps.data(), VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT );
    if ( g_QueueFamilyCompute == ( uint32_t ) -1 )
        g_QueueFamilyCompute = g_QueueFamilyGraphics; // Fallback

    g_QueueFamilyTransfer = vhVKFindDedicatedQueue_Internal( qCount, qProps.data(), VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT );
    if ( g_QueueFamilyTransfer == ( uint32_t ) -1 )
        g_QueueFamilyTransfer = g_QueueFamilyCompute; // Fallback to Compute
    if ( g_QueueFamilyTransfer == ( uint32_t ) -1 )
        g_QueueFamilyTransfer = g_QueueFamilyGraphics; // Fallback to Graphics

    VRHI_LOG( "    Selected VK Queues: Graphics %d, Compute %d, Transfer %d\n", g_QueueFamilyGraphics, g_QueueFamilyCompute, g_QueueFamilyTransfer );

    // 4. Logical Device Creation

    std::vector< VkDeviceQueueCreateInfo > queueCreateInfos;
    std::set< uint32_t > uniqueQueueFamilies = { g_QueueFamilyGraphics, g_QueueFamilyCompute, g_QueueFamilyTransfer };
    float queuePriority = 1.0f;

    for ( uint32_t queueFamily : uniqueQueueFamilies )
    {
        VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back( queueInfo );
    }

    VRHI_LOG( "    Creating VK Logical Device.\n" );
    std::vector<const char*> reqExt =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME
    };
    std::vector<const char*> enabledExt;
    vhVKFilterExtensions( g_vulkanPhysicalDevice, reqExt, enabledExt );

    // Check if Ray Tracing extensions are present
    bool rayTracingEnabled = false;
    for ( const char* ext : enabledExt )
    {
        if ( strcmp( ext, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME ) == 0 )
        {
            rayTracingEnabled = true;
            break;
        }
    }

    // 4a. Enable features if supported.

    VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan12Features v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };

    // Query features first to check support for bufferDeviceAddress
    VkPhysicalDeviceFeatures2 queryFeat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan12Features queryV12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    queryFeat2.pNext = &queryV12;
    vkGetPhysicalDeviceFeatures2( g_vulkanPhysicalDevice, &queryFeat2 );

    void** pNextChain = &feat2.pNext;

    if ( queryV12.bufferDeviceAddress )
    {
        v12.bufferDeviceAddress = VK_TRUE;
        v12.timelineSemaphore = VK_TRUE;  // Required by NVRHI
        *pNextChain = &v12;
        pNextChain = &v12.pNext;
    }

    if ( rayTracingEnabled )
    {
        accelFeatures.accelerationStructure = VK_TRUE;
        rtPipelineFeatures.rayTracingPipeline = VK_TRUE;

        *pNextChain = &accelFeatures;
        accelFeatures.pNext = &rtPipelineFeatures;
        pNextChain = &rtPipelineFeatures.pNext;
        
        VRHI_LOG( "    Ray Tracing extensions enabled.\n" );
    }
    else
    {
        VRHI_LOG( "    Ray Tracing extensions missing. RT features disabled.\n" );
    }

    VkDeviceCreateInfo devInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    devInfo.pNext = &feat2;
    devInfo.queueCreateInfoCount = ( uint32_t ) queueCreateInfos.size();
    devInfo.pQueueCreateInfos = queueCreateInfos.data();
    devInfo.enabledExtensionCount = ( uint32_t ) enabledExt.size();
    devInfo.ppEnabledExtensionNames = enabledExt.data();

    if ( vkCreateDevice( g_vulkanPhysicalDevice, &devInfo, nullptr, &g_vulkanDevice ) != VK_SUCCESS )
    {
        VRHI_LOG( "Failed to create Logical Device!\n" );
        exit(1);
    }

    vkGetDeviceQueue( g_vulkanDevice, g_QueueFamilyGraphics, 0, &g_vulkanGraphicsQueue );
    vkGetDeviceQueue( g_vulkanDevice, g_QueueFamilyCompute, 0, &g_vulkanComputeQueue );
    vkGetDeviceQueue( g_vulkanDevice, g_QueueFamilyTransfer, 0, &g_vulkanTransferQueue );

    g_vulkanEnabledExtensionCount = ( uint32_t ) enabledExt.size();

    VRHI_LOG( "    Created VK Logical Device.\n" );

    // 5. NVRHI Handover

    VRHI_LOG( "    Linking to nvRHI .... \n" );

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

    nvrhiDesc.deviceExtensions = enabledExt.data();
    nvrhiDesc.numDeviceExtensions = ( uint32_t ) enabledExt.size();

    g_vhDevice = nvrhi::vulkan::createDevice( nvrhiDesc );
    if ( !g_vhDevice )
    {
        VRHI_LOG( "Failed to create NVRHI device!\n" );
        exit(1);
    }
    if ( g_vhInit.debug )
    {
        // Wrap with validation layer in debug builds - catches state tracking errors
        VRHI_LOG( "    Wrapping nvrhi device with validation layer...\n" );
        g_vhDevice = nvrhi::validation::createValidationLayer( g_vhDevice );
    }

    // 6. Create RHI Command Buffer Thread
    VRHI_LOG( "    Creating RHI Thread...\n" );

    g_vhCmdBackendState.init();
    g_vhCmdsQuit = false;
    g_vhCmdThreadReady = false;
    g_vhCmdThread = std::thread( &vhCmdBackendState::RHIThreadEntry, &g_vhCmdBackendState, nullptr /* TODO: Pass in a callback */ );
    while ( !g_vhCmdThreadReady ) { std::this_thread::yield(); }
}

void vhShutdown()
{
    VRHI_LOG( "Shutdown Vulkan RHI ...\n" );

    // Join RHI Command Buffer Thread
    VRHI_LOG( "    Joining RHI Thread...\n" );
    g_vhCmdsQuit = true;
    g_vhCmdThread.join();
    g_vhCmdThreadReady = false;
    g_vhCmdBackendState.shutdown();
    g_vhDevice->runGarbageCollection();
    vhCmdListFlushAll();

    if ( g_vulkanDevice != VK_NULL_HANDLE )
    {
        VRHI_LOG( "    Allowing Vulkan Device to finish...\n" );
        vkDeviceWaitIdle( g_vulkanDevice );
    }

    VRHI_LOG( "    Destroying NVRHI Device...\n" );
    g_vhDevice = nullptr; // RefCountPtr handles the release()

    // Clear resources
    VRHI_LOG( "    Clearing resources...\n" );
    g_vhTextureIDList.purge();

    if ( g_vulkanDevice != VK_NULL_HANDLE )
    {
        VRHI_LOG( "    Destroying Vulkan Device...\n" );
        vkDestroyDevice( g_vulkanDevice, nullptr );
        g_vulkanDevice = VK_NULL_HANDLE;
    }

    if ( g_vulkanDebugMessenger != VK_NULL_HANDLE )
    {
        VRHI_LOG( "    Destroying Vulkan Debug Messenger...\n" );
        auto func = ( PFN_vkDestroyDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( g_vulkanInstance, "vkDestroyDebugUtilsMessengerEXT" );
        if ( func ) func( g_vulkanInstance, g_vulkanDebugMessenger, nullptr );
        g_vulkanDebugMessenger = VK_NULL_HANDLE;
    }

    if ( g_vulkanInstance != VK_NULL_HANDLE )
    {
        VRHI_LOG( "    Destroying Vulkan Instance...\n" );
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


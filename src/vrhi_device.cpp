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
#include "vrhi_backend.h"
#include "vrhi_null.h"
#include "renderdoc_app.h"

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>
#include <vk-bootstrap/VkBootstrap.h>
#include <komihash/komihash.h>

#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
extern "C" void* vhGetMetalLayerFromNSView( void* viewPtr );
#endif

std::unique_ptr< vkb::Device > g_vulkanBDevice;
void vhPSOCacheShutdown();

// Swapchain Globals
VkSurfaceKHR g_vhSurface = VK_NULL_HANDLE;
VkSwapchainKHR g_vhSwapchain = VK_NULL_HANDLE;
std::vector< VkImage > g_vhSwapchainImages;
std::vector< VkImageView > g_vhSwapchainImageViews;
std::vector< vhTexture > g_vhSwapchainTextures;
std::vector< nvrhi::TextureHandle > g_vhSwapchainNVRHIHandles;
uint32_t g_vhCurrentSwapchainIndex = 0;
glm::uvec2 g_vhWindowSize = glm::uvec2( 0, 0 );

static vhTexture g_vhHeadlessBackbuffer = VRHI_INVALID_HANDLE;
static nvrhi::TextureHandle g_vhHeadlessBackbufferHandle;

bool g_vhMemoryBudgetEnabled = false;
std::atomic<uint64_t> g_vhDrawCallsAccumulator = 0;
std::atomic<uint64_t> g_vhDispatchCallsAccumulator = 0;
std::atomic<uint64_t> g_vhFrameCount = 0;
vhRenderStats g_vhLastFrameStats = {};
vhDeviceInfo g_vhDeviceInfo = {};

static void vhResizeHeadlessBackbuffer( int width, int height );

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
        bool isLoaderNoise = strstr( pData->pMessage, "loader_icd_scan" ) != nullptr || strstr( pData->pMessage, "dlopen" ) != nullptr;
        bool isVertexNoise = strstr( pData->pMessage, "Vertex attribute is not consumed by vertex shader" ) != nullptr;
        if ( isLoaderNoise || isVertexNoise )
            VRHI_LOG( "[VULKAN] %s\n", pData->pMessage );
        else
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

static vkb::PhysicalDevice vhSelectPhysicalDevice_1_3( vkb::Instance& vkbInst, VkSurfaceKHR surface, bool headless, bool robust, bool withRT, int deviceIndex )
{
    vkb::PhysicalDeviceSelector selector( vkbInst );
    selector.require_present( !headless );
    if ( surface ) selector.set_surface( surface );

    VkPhysicalDeviceVulkan12Features v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12.timelineSemaphore = VK_TRUE;
    v12.descriptorIndexing = VK_TRUE;
    v12.runtimeDescriptorArray = VK_TRUE;
    v12.descriptorBindingPartiallyBound = VK_TRUE;
    v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    v12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    if ( withRT )
    {
        v12.bufferDeviceAddress = VK_TRUE;
    }

    VkPhysicalDeviceVulkan13Features v13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures feat = { .robustBufferAccess = robust ? VK_TRUE : VK_FALSE };
    feat.independentBlend = VK_TRUE;
    feat.fillModeNonSolid = VK_TRUE;
    feat.samplerAnisotropy = VK_TRUE;
    feat.depthClamp = VK_TRUE;
    if ( withRT )
    {
        feat.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        feat.shaderStorageImageReadWithoutFormat = VK_TRUE;
    }

    selector.set_minimum_version( 1, 3 ).set_required_features_12( v12 ).set_required_features_13( v13 ).set_required_features( feat );

    if ( deviceIndex >= 0 )
    {
        auto ret = selector.select_devices();
        if ( !ret || deviceIndex >= ( int ) ret.value().size() ) return {};
        return ret.value()[deviceIndex];
    }
    auto ret = selector.select();
    return ret ? ret.value() : vkb::PhysicalDevice{};
}

static vkb::PhysicalDevice vhSelectPhysicalDevice_1_2( vkb::Instance& vkbInst, VkSurfaceKHR surface, bool headless, bool robust, int deviceIndex )
{
    vkb::PhysicalDeviceSelector selector( vkbInst );
    selector.require_present( !headless );
    if ( surface ) selector.set_surface( surface );

    VkPhysicalDeviceVulkan12Features v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12.timelineSemaphore = VK_TRUE;
    v12.descriptorIndexing = VK_TRUE;
    v12.runtimeDescriptorArray = VK_TRUE;
    v12.descriptorBindingPartiallyBound = VK_TRUE;
    v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    v12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRender = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
    dynRender.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
    sync2.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures feat = { .robustBufferAccess = robust ? VK_TRUE : VK_FALSE };
    feat.independentBlend = VK_TRUE;
    feat.fillModeNonSolid = VK_TRUE;
    feat.samplerAnisotropy = VK_TRUE;
    feat.depthClamp = VK_TRUE;

    selector.set_minimum_version( 1, 2 )
        .add_required_extension( VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME ).add_required_extension( VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME )
        .add_required_extension_features( dynRender ).add_required_extension_features( sync2 )
        .set_required_features_12( v12 ).set_required_features( feat );

    if ( deviceIndex >= 0 )
    {
        auto ret = selector.select_devices();
        if ( !ret || deviceIndex >= ( int ) ret.value().size() ) return {};
        return ret.value()[deviceIndex];
    }
    auto ret = selector.select();
    return ret ? ret.value() : vkb::PhysicalDevice{};
}

void vhInit( bool quiet )
{
    if ( !quiet ) VRHI_LOG( "Initialising Vulkan RHI ...\n" );
    g_vhErrorCounter = 0;
    g_vhPSOCompileCounter = 0;
    g_vhFrameCount = 0;
    g_vhFramesInFlight = 2;

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

    g_vhCmdArena.Init();

    // Temporarily disable debugBlockWaitForBackend to avoid deadlock during initialisation
    bool originalDebugBlockWaitForBackend = g_vhInit.debugBlockWaitForBackend;
    g_vhInit.debugBlockWaitForBackend = false;

    g_vhNullMode = g_vhInit.nullMode;
    if ( g_vhNullMode )
    {
        // Force headless - there is no surface or swapchain in null mode
        g_vhInit.headless = true;

        // Create null device as the NVRHI backend
        g_vhDevice = nvrhi::DeviceHandle( new vhNullDevice() );
        g_vhVulkanDevice = nullptr;
        g_vhWindowSize = g_vhInit.resolution;

        // Populate device info for null mode
        g_vhDeviceInfo = {};
        g_vhDeviceInfo.name = "NULL Device";
        g_vhDeviceInfo.driver = "None";
        g_vhDeviceInfo.apiVersion = "N/A";
        g_vhDeviceInfo.summary = "NULL RHI Mode - No GPU";

        // Skip straight to frame state, dummy resources, and backend thread
        goto vrhi_init_post_device;
    }

    // Create VkInstance (via vk-bootstrap)
    {
        if ( !quiet ) VRHI_LOG( "    Creating VK Instance (via vk-bootstrap)\n" );
        vkb::InstanceBuilder instBuilder;
        instBuilder.set_app_name( g_vhInit.appName.c_str() )
            .set_engine_name( g_vhInit.engineName.c_str() )
            .require_api_version( 1, 3, 0 )
            .set_headless( g_vhInit.headless )
            .request_validation_layers( g_vhInit.debug )
            .set_debug_callback( vhVKDebugCallback );
        if ( g_vhInit.renderdoc ) instBuilder.enable_extension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

        instBuilder.enable_extension( VK_KHR_SURFACE_EXTENSION_NAME );
#ifdef __APPLE__
        if ( !g_vhInit.headless )
            instBuilder.enable_extension( VK_EXT_METAL_SURFACE_EXTENSION_NAME );
        instBuilder.enable_extension( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );
#endif

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

        // Surface Creation
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if ( !g_vhInit.headless && g_vhInit.windowHandle )
        {
            if ( !quiet ) VRHI_LOG( "    Creating Surface from Window Handle...\n" );
#if defined(_WIN32)
            VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
            sci.hinstance = GetModuleHandle( NULL );
            sci.hwnd = ( HWND ) g_vhInit.windowHandle;
            auto res = vkCreateWin32SurfaceKHR( g_vulkanInstance, &sci, nullptr, &surface );
            if ( res != VK_SUCCESS )
            {
                VRHI_LOG( "Failed to create Win32 surface: %d\n", res );
                exit( 1 );
            }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
            VkXlibSurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
            sci.dpy = ( Display* ) g_vhInit.displayHandle;
            sci.window = ( Window ) g_vhInit.windowHandle;
            auto res = vkCreateXlibSurfaceKHR( g_vulkanInstance, &sci, nullptr, &surface );
            if ( res != VK_SUCCESS )
            {
                VRHI_LOG( "Failed to create Xlib surface: %d\n", res );
                exit( 1 );
            }
#elif defined(__APPLE__)
            const CAMetalLayer* metalLayer = nullptr;
            
            if ( g_vhInit.macOSWindowIsNSView )
            {
                void* layerPtr = vhGetMetalLayerFromNSView( g_vhInit.windowHandle );
                metalLayer = ( const CAMetalLayer* )layerPtr;
            }
            else
            {
                metalLayer = ( const CAMetalLayer* )g_vhInit.windowHandle;
            }
            
            VkMetalSurfaceCreateInfoEXT sci = { VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
            sci.pLayer = metalLayer;
            auto res = vkCreateMetalSurfaceEXT( g_vulkanInstance, &sci, nullptr, &surface );
            if ( res != VK_SUCCESS )
            {
                VRHI_LOG( "Failed to create Metal surface: %d\n", res );
                exit( 1 );
            }
#endif
        }
        g_vhSurface = surface;

        // Physical Device Selection (via vk-bootstrap)

        if ( !quiet ) VRHI_LOG( "    Selecting physical device (via vk-bootstrap)\n" );

        vkb::PhysicalDevice vkbPhys;
        bool selectedRT = false;
        bool using12Fallback = false;

        if ( !g_vhInit.forceVulkan12 )
        {
            vkbPhys = vhSelectPhysicalDevice_1_3( vkbInst, g_vhSurface, g_vhInit.headless, g_vhInit.robust, g_vhInit.raytracing, g_vhInit.deviceIndex );
            if ( vkbPhys.physical_device )
            {
                selectedRT = g_vhInit.raytracing;
            }
            else if ( g_vhInit.raytracing && g_vhInit.deviceIndex < 0 )
            {
                if ( !quiet ) VRHI_LOG( "    No RT-capable Vulkan 1.3 device found, retrying without RT.\n" );
                vkbPhys = vhSelectPhysicalDevice_1_3( vkbInst, g_vhSurface, g_vhInit.headless, g_vhInit.robust, false, g_vhInit.deviceIndex );
            }
        }

        if ( !vkbPhys.physical_device )
        {
            if ( !quiet ) VRHI_LOG( "    %s, trying Vulkan 1.2 + KHR_dynamic_rendering + KHR_synchronization2.\n",
                g_vhInit.forceVulkan12 ? "forceVulkan12 set" : "No Vulkan 1.3 device found" );
            vkbPhys = vhSelectPhysicalDevice_1_2( vkbInst, g_vhSurface, g_vhInit.headless, g_vhInit.robust, g_vhInit.deviceIndex );
            if ( vkbPhys.physical_device ) using12Fallback = true;
        }

        if ( !vkbPhys.physical_device )
        {
            VRHI_LOG( "Failed to select suitable physical device.\n" );
            exit( 1 );
        }

        g_vulkanPhysicalDevice = vkbPhys.physical_device;
        if ( !quiet ) VRHI_LOG( "    Selected GPU Device: %s\n", vkbPhys.name.c_str() );

        VkPhysicalDeviceVulkan12Properties v12Props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
        VkPhysicalDeviceDriverProperties driverProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        driverProps.pNext = &v12Props;
        VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        props2.pNext = &driverProps;
        VkPhysicalDeviceVulkan12Features supportedV12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        features2.pNext = &supportedV12;
        vkGetPhysicalDeviceProperties2( g_vulkanPhysicalDevice, &props2 );
        vkGetPhysicalDeviceFeatures2( g_vulkanPhysicalDevice, &features2 );
        VRHI_LOG( "    Vulkan Driver: %s (%s)\n", driverProps.driverName, driverProps.driverInfo );

        // Populate device info struct
        const char* deviceTypeStr = "Unknown";
        switch ( props2.properties.deviceType )
        {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   deviceTypeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceTypeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:     deviceTypeStr = "Virtual GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:             deviceTypeStr = "CPU"; break;
            default:                                      deviceTypeStr = "Other"; break;
        }

        g_vhDeviceInfo.name = std::string( props2.properties.deviceName ) + " - " + deviceTypeStr;
        g_vhDeviceInfo.driver = std::string( driverProps.driverName ) + " " + std::string( driverProps.driverInfo );
        g_vhDeviceInfo.apiVersion = std::to_string( VK_API_VERSION_MAJOR( props2.properties.apiVersion ) ) + "." +
            std::to_string( VK_API_VERSION_MINOR( props2.properties.apiVersion ) ) + "." +
            std::to_string( VK_API_VERSION_PATCH( props2.properties.apiVersion ) );
        g_vhDeviceInfo.maxTextureSize = props2.properties.limits.maxImageDimension2D;
        g_vhDeviceInfo.maxColorAttachments = props2.properties.limits.maxColorAttachments;
        g_vhDeviceInfo.maxBindlessSampledImages  = std::min( props2.properties.limits.maxPerStageDescriptorSampledImages,  props2.properties.limits.maxDescriptorSetSampledImages );
        g_vhDeviceInfo.maxBindlessStorageImages  = std::min( props2.properties.limits.maxPerStageDescriptorStorageImages,  props2.properties.limits.maxDescriptorSetStorageImages );
        g_vhDeviceInfo.maxBindlessStorageBuffers = std::min( props2.properties.limits.maxPerStageDescriptorStorageBuffers, props2.properties.limits.maxDescriptorSetStorageBuffers );
        g_vhDeviceInfo.maxBindlessUniformBuffers = std::min( props2.properties.limits.maxPerStageDescriptorUniformBuffers, props2.properties.limits.maxDescriptorSetUniformBuffers );
        g_vhDeviceInfo.maxBindlessSamplers       = std::min( props2.properties.limits.maxPerStageDescriptorSamplers,       props2.properties.limits.maxDescriptorSetSamplers );
        g_vhDeviceInfo.maxBoundDescriptorSets    = props2.properties.limits.maxBoundDescriptorSets;
        g_vhDeviceInfo.maxPerStageResources      = props2.properties.limits.maxPerStageResources;

        if ( supportedV12.descriptorBindingSampledImageUpdateAfterBind ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_SAMPLED_IMAGE;
        if ( supportedV12.descriptorBindingStorageImageUpdateAfterBind ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_STORAGE_IMAGE;
        if ( supportedV12.descriptorBindingStorageBufferUpdateAfterBind ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_STORAGE_BUFFER;
        if ( supportedV12.descriptorBindingUniformBufferUpdateAfterBind ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_UNIFORM_BUFFER;
        if ( supportedV12.descriptorBindingUniformTexelBufferUpdateAfterBind ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_UNIFORM_TEXEL_BUFFER;
        if ( supportedV12.descriptorBindingStorageTexelBufferUpdateAfterBind ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_STORAGE_TEXEL_BUFFER;
        if ( supportedV12.descriptorBindingUpdateUnusedWhilePending ) g_vhDeviceInfo.bindlessUpdateAfterBind |= VRHI_UAB_UPDATE_UNUSED_WHILE_PENDING;

        g_vhDeviceInfo.maxBindlessSampledImagesUAB  = std::min( v12Props.maxPerStageDescriptorUpdateAfterBindSampledImages,  v12Props.maxDescriptorSetUpdateAfterBindSampledImages );
        g_vhDeviceInfo.maxBindlessStorageImagesUAB  = std::min( v12Props.maxPerStageDescriptorUpdateAfterBindStorageImages,  v12Props.maxDescriptorSetUpdateAfterBindStorageImages );
        g_vhDeviceInfo.maxBindlessStorageBuffersUAB = std::min( v12Props.maxPerStageDescriptorUpdateAfterBindStorageBuffers, v12Props.maxDescriptorSetUpdateAfterBindStorageBuffers );
        g_vhDeviceInfo.maxBindlessUniformBuffersUAB = std::min( v12Props.maxPerStageDescriptorUpdateAfterBindUniformBuffers, v12Props.maxDescriptorSetUpdateAfterBindUniformBuffers );
        g_vhDeviceInfo.maxBindlessSamplersUAB       = std::min( v12Props.maxPerStageDescriptorUpdateAfterBindSamplers,       v12Props.maxDescriptorSetUpdateAfterBindSamplers );

        // Get memory heap info
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties( g_vulkanPhysicalDevice, &memProps );
        for ( uint32_t i = 0; i < memProps.memoryHeapCount; ++i )
        {
            if ( memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT )
            {
                g_vhDeviceInfo.totalVRAM += memProps.memoryHeaps[i].size;
            }
        }

        // Device Creation & Queues (via vk-bootstrap)

        bool rtExtEnabled = false;
        bool rayQueryEnabled = false;
        bool nvLssEnabled = false;
        bool maintenance5Enabled = false;
        bool pipelineLibraryEnabled = false;
        if ( g_vhInit.raytracing && selectedRT )
        {
            rtExtEnabled = vkbPhys.enable_extension_if_present( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME ) &&
                vkbPhys.enable_extension_if_present( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME ) &&
                vkbPhys.enable_extension_if_present( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
            if ( rtExtEnabled )
            {
                rayQueryEnabled = vkbPhys.enable_extension_if_present( VK_KHR_RAY_QUERY_EXTENSION_NAME );
                nvLssEnabled = vkbPhys.enable_extension_if_present( VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME );
                maintenance5Enabled = vkbPhys.enable_extension_if_present( VK_KHR_MAINTENANCE_5_EXTENSION_NAME );
                pipelineLibraryEnabled = vkbPhys.enable_extension_if_present( VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME );
            }
        }

        bool robustness2Enabled = false;
        if ( g_vhInit.robust )
        {
            robustness2Enabled = vkbPhys.enable_extension_if_present( VK_EXT_ROBUSTNESS_2_EXTENSION_NAME );
        }
        bool shaderDrawParametersEnabled = vkbPhys.enable_extension_if_present( "VK_KHR_shader_draw_parameters" );
        if ( shaderDrawParametersEnabled && !quiet ) VRHI_LOG( "    Enabled VK_KHR_shader_draw_parameters extension.\n" );

        bool memoryBudgetEnabled = vkbPhys.enable_extension_if_present( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
        if ( memoryBudgetEnabled && !quiet ) VRHI_LOG( "    Enabled VK_EXT_memory_budget extension.\n" );
        g_vhMemoryBudgetEnabled = memoryBudgetEnabled;

        // Enable whichever update-after-bind bits the device supports (free permissions; opt-in per table).
        VkPhysicalDeviceVulkan12Features uabFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        uabFeatures.descriptorBindingSampledImageUpdateAfterBind = supportedV12.descriptorBindingSampledImageUpdateAfterBind;
        uabFeatures.descriptorBindingStorageImageUpdateAfterBind = supportedV12.descriptorBindingStorageImageUpdateAfterBind;
        uabFeatures.descriptorBindingStorageBufferUpdateAfterBind = supportedV12.descriptorBindingStorageBufferUpdateAfterBind;
        uabFeatures.descriptorBindingUniformBufferUpdateAfterBind = supportedV12.descriptorBindingUniformBufferUpdateAfterBind;
        uabFeatures.descriptorBindingUniformTexelBufferUpdateAfterBind = supportedV12.descriptorBindingUniformTexelBufferUpdateAfterBind;
        uabFeatures.descriptorBindingStorageTexelBufferUpdateAfterBind = supportedV12.descriptorBindingStorageTexelBufferUpdateAfterBind;
        uabFeatures.descriptorBindingUpdateUnusedWhilePending = supportedV12.descriptorBindingUpdateUnusedWhilePending;
        vkbPhys.enable_extension_features_if_present( uabFeatures );

        g_vhDeviceInfo.shaderFloat16 = false;
        if ( g_vhInit.shaderFloat16 )
        {
            VkPhysicalDeviceVulkan12Features f16 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            f16.shaderFloat16 = VK_TRUE;
            g_vhDeviceInfo.shaderFloat16 = vkbPhys.enable_extension_features_if_present( f16 );
            if ( !quiet ) VRHI_LOG( g_vhDeviceInfo.shaderFloat16 ? "    Enabled shaderFloat16.\n" : "    shaderFloat16 requested but unsupported; continuing.\n" );
        }
        g_vhDeviceInfo.shaderInt16 = false;
        if ( g_vhInit.shaderInt16 )
        {
            VkPhysicalDeviceFeatures i16 = {};
            i16.shaderInt16 = VK_TRUE;
            g_vhDeviceInfo.shaderInt16 = vkbPhys.enable_features_if_present( i16 );
            if ( !quiet ) VRHI_LOG( g_vhDeviceInfo.shaderInt16 ? "    Enabled shaderInt16.\n" : "    shaderInt16 requested but unsupported; continuing.\n" );
        }

        if ( !g_vhInit.extraDeviceExtensions.empty() )
        {
            std::vector< std::string > alreadyEnabled = vkbPhys.get_extensions();
            for ( const std::string& ext : g_vhInit.extraDeviceExtensions )
            {
                if ( std::find( alreadyEnabled.begin(), alreadyEnabled.end(), ext ) != alreadyEnabled.end() )
                    continue;
                bool ok = vkbPhys.enable_extension_if_present( ext.c_str() );
                if ( ok ) alreadyEnabled.push_back( ext );
                if ( !quiet ) VRHI_LOG( ok ? "    Enabled extra extension: %s\n" : "    Extra extension unavailable, skipping: %s\n", ext.c_str() );
            }
        }

        if ( !quiet ) VRHI_LOG( "    Creating VK Logical Device (via vk-bootstrap)%s\n", using12Fallback ? " [Vulkan 1.2 + KHR extensions]" : "" );
        vkb::DeviceBuilder devBuilder( vkbPhys );

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
        VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV lssFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV };

        if ( rtExtEnabled )
        {
            accelFeatures.accelerationStructure = VK_TRUE;
            rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
            devBuilder.add_pNext( &accelFeatures );
            devBuilder.add_pNext( &rtPipelineFeatures );
            if ( rayQueryEnabled )
            {
                rayQueryFeatures.rayQuery = VK_TRUE;
                devBuilder.add_pNext( &rayQueryFeatures );
            }
            if ( nvLssEnabled )
            {
                lssFeatures.spheres = VK_TRUE;
                lssFeatures.linearSweptSpheres = VK_TRUE;
                devBuilder.add_pNext( &lssFeatures );
            }
            if ( !quiet ) VRHI_LOG( "    Ray Tracing extensions enabled.%s%s%s%s\n", rayQueryEnabled ? " (Ray Query)" : "", nvLssEnabled ? " (LSS)" : "", maintenance5Enabled ? " (Maint5)" : "", pipelineLibraryEnabled ? " (PipelineLib)" : "" );
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

        VkPhysicalDeviceVulkan11Features v11Feat = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        if ( shaderDrawParametersEnabled )
        {
            v11Feat.shaderDrawParameters = VK_TRUE;
            devBuilder.add_pNext( &v11Feat );
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
        if ( using12Fallback )
        {
            s_enabledExtensions.push_back( VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME );
            s_enabledExtensions.push_back( VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME );
        }
        if ( g_vhRayTracingEnabled )
        {
            s_enabledExtensions.push_back( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME );
            s_enabledExtensions.push_back( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
            s_enabledExtensions.push_back( VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
            if ( rayQueryEnabled )
            {
                s_enabledExtensions.push_back( VK_KHR_RAY_QUERY_EXTENSION_NAME );
            }
            if ( nvLssEnabled )
            {
                s_enabledExtensions.push_back( VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME );
            }
            if ( maintenance5Enabled )
            {
                s_enabledExtensions.push_back( VK_KHR_MAINTENANCE_5_EXTENSION_NAME );
            }
            if ( pipelineLibraryEnabled )
            {
                s_enabledExtensions.push_back( VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME );
            }
        }
        if ( robustness2Enabled )
        {
            s_enabledExtensions.push_back( VK_EXT_ROBUSTNESS_2_EXTENSION_NAME );
        }
        if ( shaderDrawParametersEnabled )
        {
            s_enabledExtensions.push_back( VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME );
        }
        if ( memoryBudgetEnabled )
        {
            s_enabledExtensions.push_back( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
        }

        g_vulkanEnabledExtensionCount = ( uint32_t ) s_enabledExtensions.size();

        static std::vector<const char*> s_enabledExtensionPointers;
        s_enabledExtensionPointers.clear();
        for ( const auto& ext : s_enabledExtensions ) s_enabledExtensionPointers.push_back( ext.c_str() );

        if ( !quiet ) VRHI_LOG( "    Selected VK Queues: Graphics %d, Compute %d, Transfer %d\n", g_QueueFamilyGraphics, g_QueueFamilyCompute, g_QueueFamilyTransfer );

        // Populate queues string for device info
        char queuesBuffer[256];
        snprintf( queuesBuffer, sizeof( queuesBuffer ), "Graphics:%d Compute:%d Transfer:%d", g_QueueFamilyGraphics, g_QueueFamilyCompute, g_QueueFamilyTransfer );
        g_vhDeviceInfo.queues = std::string( queuesBuffer );

        // Store features we know from extension checking
        g_vhDeviceInfo.raytracing = g_vhRayTracingEnabled;
        g_vhDeviceInfo.memoryBudget = memoryBudgetEnabled;

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

        nvrhiDesc.bufferDeviceAddressSupported = g_vhRayTracingEnabled;

        if ( !g_vhInit.psoCacheInitialData.empty() )
        {
            nvrhiDesc.pipelineCacheInitialData = g_vhInit.psoCacheInitialData.data();
            nvrhiDesc.pipelineCacheInitialDataSize = g_vhInit.psoCacheInitialData.size();
        }

        g_vhDevice = nvrhi::vulkan::createDevice( nvrhiDesc );
        if ( !g_vhDevice )
        {
            VRHI_LOG( "Failed to create NVRHI device!\n" );
            exit( 1 );
        }
        g_vhVulkanDevice = ( nvrhi::vulkan::IDevice* ) g_vhDevice.Get();

        if ( g_vhInit.debug )
        {
            // Wrap with validation layer in debug builds - catches state tracking errors
            if ( !quiet ) VRHI_LOG( "    Wrapping nvrhi device with validation layer...\n" );
            g_vhDevice = nvrhi::validation::createValidationLayer( g_vhDevice );
        }

        // Auto-detect remaining features
        g_vhDeviceInfo.bindless = supportedV12.descriptorIndexing && supportedV12.runtimeDescriptorArray;
        g_vhDeviceInfo.vrs = vhQueryFeatureSupport_Internal( nvrhi::Feature::VariableRateShading );
        g_vhDeviceInfo.asyncCompute = vhQueryFeatureSupport_Internal( nvrhi::Feature::CopyQueue );

        // Generate summary string for legacy compatibility
        char summaryBuffer[1024];
        snprintf( summaryBuffer, sizeof( summaryBuffer ),
            "Device: %s Vulkan: %s Type: %s Queues: %s NVRHI: Active Extensions: %u",
            props2.properties.deviceName,
            g_vhDeviceInfo.apiVersion.c_str(),
            g_vhDeviceInfo.name.c_str(),
            g_vhDeviceInfo.queues.c_str(),
            g_vulkanEnabledExtensionCount
        );
        g_vhDeviceInfo.summary = std::string( summaryBuffer );
    }

vrhi_init_post_device:
    // Swapchain Creation
    if ( g_vhSurface != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Creating Swapchain...\n" );
        vhSwapchainCreate_Internal( g_vhInit.resolution.x, g_vhInit.resolution.y );
    }

    // Initialise frame instances
    g_vhFrameInstances.clear();
    if ( !g_vhInit.headless )
    {
        g_vhFrameInstances.resize( g_vhFramesInFlight, 0 );
    }

    // Set initial frame index to avoid wait hang on first frame
    g_vhFrameIndex = 0;
    g_vhCurrentSwapchainIndex = 0;

    if ( g_vhSurface == VK_NULL_HANDLE && g_vhInit.headless && !g_vhNullMode )
    {
        vhResizeHeadlessBackbuffer( g_vhInit.resolution.x, g_vhInit.resolution.y );
    }

    vhInitDummyResources();

    // Create RHI Command Buffer Thread
    if ( !quiet ) VRHI_LOG( "    Creating RHI Thread...\n" );

    vhBackendInit();
    g_vhCmdsQuit = false;
    g_vhCmdThreadReady = false;
    g_vhCmdThread = std::thread( vhBackendThreadEntry, g_vhInit.fnThreadInitCallback );
    while ( !g_vhCmdThreadReady ) { std::this_thread::yield(); }

    // Restore debugBlockWaitForBackend now that initialisation is complete
    g_vhInit.debugBlockWaitForBackend = originalDebugBlockWaitForBackend;
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
    vhCmdListFlushAll();
    {
        // Release persistent command list handles now that no more recording can happen.
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        vhCmdListReleaseAll_DeviceStateLocked();
    }
    g_vhCmdArena.Shutdown();

    if ( g_vulkanDevice != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Allowing Vulkan Device to finish...\n" );
        vkDeviceWaitIdle( g_vulkanDevice );
    }

    // Destroy frame synchronisation semaphores
    for ( auto sem : g_vhAcquireSemaphores )
    {
        if ( sem != VK_NULL_HANDLE )
            vkDestroySemaphore( g_vulkanDevice, sem, nullptr );
    }
    for ( auto sem : g_vhPresentSemaphores )
    {
        if ( sem != VK_NULL_HANDLE )
            vkDestroySemaphore( g_vulkanDevice, sem, nullptr );
    }
    g_vhAcquireSemaphores.clear();
    g_vhPresentSemaphores.clear();

    vhPSOCacheShutdown();
    vhSamplerCacheShutdown();
    vhBindingSetCacheClear();
    vhFBOCacheReset();

    // Clear resources
    if ( !quiet ) VRHI_LOG( "    Clearing resources...\n" );
    g_vhTextureIDList.purge();
    g_vhHeapIDList.purge();
    vhShutdownDummyResources();

    if ( g_vhSwapchain != VK_NULL_HANDLE )
    {
        // Release NVRHI handles while device is still valid
        g_vhSwapchainNVRHIHandles.clear();
        g_vhSwapchainTextures.clear();
    }

    g_vhHeadlessBackbufferHandle = nullptr;
    g_vhHeadlessBackbuffer = VRHI_INVALID_HANDLE;

    if ( !quiet ) VRHI_LOG( "    Destroying NVRHI Device...\n" );
    for ( int i = 0; i < VRHI_MAX_FRAMES_INFLIGHT; i++ ) g_vhDevice->runGarbageCollection();
    g_vhDevice = nullptr;
    g_vhVulkanDevice = nullptr;

    if ( g_vulkanDevice != VK_NULL_HANDLE )
    {
        if ( !quiet ) VRHI_LOG( "    Destroying Vulkan Device...\n" );
        if ( g_vhSwapchain != VK_NULL_HANDLE )
        {
            vkDestroySwapchainKHR( g_vulkanDevice, g_vhSwapchain, nullptr );
            for ( auto iv : g_vhSwapchainImageViews ) vkDestroyImageView( g_vulkanDevice, iv, nullptr );
            g_vhSwapchain = VK_NULL_HANDLE;
            g_vhSwapchainImageViews.clear();
        }
        vkDestroyDevice( g_vulkanDevice, nullptr );
        g_vulkanDevice = VK_NULL_HANDLE;
    }

    if ( g_vhSurface != VK_NULL_HANDLE )
    {
        vkDestroySurfaceKHR( g_vulkanInstance, g_vhSurface, nullptr );
        g_vhSurface = VK_NULL_HANDLE;
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

    g_vhNullMode = false;
}

bool vhGetPSOCache( std::vector< uint8_t >& outData )
{
    outData.clear();
    if ( g_vhNullMode || !g_vhVulkanDevice ) return false;
    vhFinish();
    g_vhDevice->waitForIdle();
    return g_vhVulkanDevice->getPipelineCacheData( outData );
}

vhMemoryStats vhStatsMemory()
{
    vhMemoryStats stats = {};
    
    if ( !g_vhDevice || g_vhNullMode )
    {
        if ( !g_vhDevice ) VRHI_ERR( "vhStatsMemory(): Device not initialised.\n" );
        return stats;
    }
    
    // Basic memory properties are always available
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties( g_vulkanPhysicalDevice, &memProps );
    stats.heapCount = memProps.memoryHeapCount;
    
    for ( uint32_t i = 0; i < memProps.memoryHeapCount; ++i )
    {
        stats.heapSize[i] = memProps.memoryHeaps[i].size;
    }
    
    // If VK_EXT_memory_budget is enabled, query budget information
    if ( g_vhMemoryBudgetEnabled )
    {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps = { 
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT 
        };
        VkPhysicalDeviceMemoryProperties2 memProps2 = { 
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 
        };
        memProps2.pNext = &budgetProps;
        
        vkGetPhysicalDeviceMemoryProperties2( g_vulkanPhysicalDevice, &memProps2 );
        
        for ( uint32_t i = 0; i < stats.heapCount; ++i )
        {
            stats.heapBudget[i] = budgetProps.heapBudget[i];
            stats.heapUsage[i] = budgetProps.heapUsage[i];
        }
        stats.supported = true;
    }
    
    return stats;
}

vhRenderStats vhGetStats()
{
    return g_vhLastFrameStats;
}
 
uint64_t vhGetFrameNumber()
{
    return g_vhFrameCount.load();
}

bool vhQueryFeatureSupport_Internal( nvrhi::Feature feature, void* pInfo, size_t infoSize )
{
    // WARNING: g_nvRHIStateMutex must be held before calling this
    if ( !g_vhDevice )
    {
        VRHI_ERR( "vhQueryFeatureSupport_Internal(): Device not initialised.\n" );
        return false;
    }
    return g_vhDevice->queryFeatureSupport( feature, pInfo, infoSize );
}

nvrhi::FormatSupport vhQueryFormatSupport_Internal( nvrhi::Format format )
{
    // WARNING: g_nvRHIStateMutex must be held before calling this
    if ( !g_vhDevice )
    {
        VRHI_ERR( "vhQueryFormatSupport_Internal(): Device not initialised.\n" );
        return nvrhi::FormatSupport::None;
    }
    return g_vhDevice->queryFormatSupport( format );
}

bool vhQueryFeatureSupport( nvrhi::Feature feature, void* pInfo, size_t infoSize )
{
    if ( !g_vhDevice )
    {
        VRHI_ERR( "vhQueryFeatureSupport(): Device not initialised.\n" );
        return false;
    }
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    return g_vhDevice->queryFeatureSupport( feature, pInfo, infoSize );
}

nvrhi::FormatSupport vhQueryFormatSupport( nvrhi::Format format )
{
    if ( !g_vhDevice )
    {
        VRHI_ERR( "vhQueryFormatSupport(): Device not initialised.\n" );
        return nvrhi::FormatSupport::None;
    }
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    return g_vhDevice->queryFormatSupport( format );
}

void vhDispatch( vhStateId stateID, glm::uvec3 workGroupCount )
{
    g_vhDispatchCallsAccumulator.fetch_add( 1, std::memory_order_relaxed );
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
    g_vhDispatchCallsAccumulator.fetch_add( 1, std::memory_order_relaxed );
    VIDL_vhDispatchIndirect* cmd = vhCmdAlloc<VIDL_vhDispatchIndirect>( stateID, indirectBuffer, byteOffset );
    vhCmdEnqueue( cmd );
}

void vhDraw( vhStateId state, uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation )
{
    g_vhDrawCallsAccumulator.fetch_add( instanceCount, std::memory_order_relaxed );
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
    g_vhDrawCallsAccumulator.fetch_add( instanceCount, std::memory_order_relaxed );
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
    g_vhDrawCallsAccumulator.fetch_add( 1, std::memory_order_relaxed );
    vhDrawCommonInternal(
        state,
        VRHI_DRAW_INDIRECT,
        0, 0, 0, 0, 0, // unused direct args
        drawCount
    );
}

void vhDrawIndexedIndirect( vhStateId state, uint32_t drawCount )
{
    g_vhDrawCallsAccumulator.fetch_add( 1, std::memory_order_relaxed );
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

void vhPrecompilePSO(
    vhProgram program,
    uint64_t stateFlags,
    const vhVertexLayout& vertexLayout,
    const std::vector< nvrhi::Format >& colorFormats,
    nvrhi::Format depthFormat,
    uint32_t sampleCount
)
{
    if ( program.empty() )
        return;
    if ( sampleCount == 0 )
    {
        VRHI_ERR( "vhPrecompilePSO(): sampleCount must be > 0\n" );
        return;
    }

    VIDL_vhPrecompilePSO* cmd = vhCmdAlloc< VIDL_vhPrecompilePSO >(
        std::move( program ),
        stateFlags,
        vertexLayout,
        colorFormats,
        depthFormat,
        sampleCount
    );
    vhCmdEnqueue( cmd, false );
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
    vhCmdEnqueue( cmd, false );
}

void vhFlush( bool wait )
{
    VRHI_PROFILE_FUNCTION();
    std::atomic<bool> fence = false;
    vhFlushInternal( wait ? &fence : nullptr, false );

    if ( wait )
    {
        vhProfile( "vhFlush_Wait", true );
        fence.wait( false, std::memory_order_acquire );
        vhProfile( "vhFlush_Wait", false );
    }
}

void vhFinish()
{
    VRHI_PROFILE_FUNCTION();
    std::atomic<bool> fence = false;
    vhFlushInternal( &fence, true );

    vhProfile( "vhFinish_Wait", true );
    fence.wait( false, std::memory_order_acquire );
    vhProfile( "vhFinish_Wait", false );
}

void vhExecuteNative( vhNativeExecuteFn fn, void* user, uint32_t frameIndex, const std::vector< vhNativeResource >& resources )
{
    vhCmdEnqueue( vhCmdAlloc< VIDL_vhExecuteNative >( fn, user, frameIndex, vhArenaCopySpan( resources ) ) );
}

VkDevice vhGetVkDevice()
{
    return g_vulkanDevice;
}

VkPhysicalDevice vhGetVkPhysicalDevice()
{
    return g_vulkanPhysicalDevice;
}

VkInstance vhGetVkInstance()
{
    return g_vulkanInstance;
}

vhNativeDeviceLock::vhNativeDeviceLock()
{
    g_nvRHIStateMutex.lock();
}

vhNativeDeviceLock::~vhNativeDeviceLock()
{
    g_nvRHIStateMutex.unlock();
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

bool vhFrame()
{
    // Reset per-frame statistics
    g_vhLastFrameStats.drawCalls = g_vhDrawCallsAccumulator.exchange( 0 );
    g_vhLastFrameStats.dispatchCalls = g_vhDispatchCallsAccumulator.exchange( 0 );

    if ( g_vhInit.headless )
    {
        vhFlush();
        g_vhFrameCount++;
        return true;
    }

    if ( g_vhSurface == VK_NULL_HANDLE )
        return false;

    // Drain backend so the main thread can use the same nvrhi command list for swapchain semaphore ops below.
    vhFlush( true );

    // Ensure we have an active command list for semaphore operations.
    auto cmdList = vhCmdListGet( nvrhi::CommandQueue::Graphics );
    nvrhi::vulkan::IDevice* nvrhiDevice = g_vhVulkanDevice;
    {
        // Transition swapchain image to Present layout if it hasn't been touched.
        // This handles the "empty frame" case where nothing was rendered.
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        if ( !g_vhSwapchainNVRHIHandles.empty() && g_vhCurrentSwapchainIndex < g_vhSwapchainNVRHIHandles.size() )
        {
            nvrhi::TextureHandle backbufferHandle = g_vhSwapchainNVRHIHandles[g_vhCurrentSwapchainIndex];
            cmdList->setTextureState( backbufferHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::Present );
            cmdList->commitBarriers();
        }
        nvrhiDevice->queueWaitForSemaphore( nvrhi::CommandQueue::Graphics, g_vhAcquireSemaphores[g_vhFrameIndex], 0 );
        nvrhiDevice->queueSignalSemaphore( nvrhi::CommandQueue::Graphics, g_vhPresentSemaphores[g_vhCurrentSwapchainIndex], 0 );
    }

    // Flush this command list on the main thread to capture the instance ID for frame pacing.
    g_vhFrameInstances[g_vhFrameIndex] = vhCmdListFlush( nvrhi::CommandQueue::Graphics );

    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &g_vhPresentSemaphores[g_vhCurrentSwapchainIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &g_vhSwapchain;
    presentInfo.pImageIndices = &g_vhCurrentSwapchainIndex;

    VkResult res;
    vhProfile( "vhFrame_Present", true );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        res = vkQueuePresentKHR( g_vulkanGraphicsQueue, &presentInfo );
    }
    vhProfile( "vhFrame_Present", false );

    if ( res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR )
    {
        // Handle resize if needed
    }
    else if ( res != VK_SUCCESS )
    {
        return false;
    }

    // Advance Frame
    g_vhFrameIndex = ( uint32_t ) ( ( g_vhFrameIndex + 1 ) % g_vhFramesInFlight );

    // Wait for next frame's previous work via Vulkan timeline semaphore — kernel-blocks, no polling.
    uint64_t nextFrameInstance = g_vhFrameInstances[g_vhFrameIndex];
    if ( nextFrameInstance != 0 )
    {
        VkSemaphore queueSem;
        {
            std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
            queueSem = nvrhiDevice->getQueueSemaphore( nvrhi::CommandQueue::Graphics );
        }
        VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &queueSem;
        waitInfo.pValues = &nextFrameInstance;
        vhProfile( "vhFrame_WaitSemaphore", true );
        vkWaitSemaphores( g_vulkanDevice, &waitInfo, UINT64_MAX );
        vhProfile( "vhFrame_WaitSemaphore", false );
    }

    // Acquire Next Image
    // We use a frame-indexed acquire semaphore because we don't know the image index yet.
    // The present semaphore is indexed by image because we know that after rendering.
    uint32_t idx = 0;
    vhProfile( "vhFrame_AcquireNextImage", true );
    {
        std::lock_guard< std::mutex > lock( g_nvRHIStateMutex );
        res = vkAcquireNextImageKHR( g_vulkanDevice, g_vhSwapchain, UINT64_MAX, 
            g_vhAcquireSemaphores[g_vhFrameIndex], VK_NULL_HANDLE, &idx );
    }
    vhProfile( "vhFrame_AcquireNextImage", false );
    if ( res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR )
    {
        return false;
    }

    g_vhCurrentSwapchainIndex = idx;
    g_vhFrameCount++;
    return true;
}

vhTexture vhGetBackbuffer()
{
    if ( g_vhSurface == VK_NULL_HANDLE )
        return g_vhHeadlessBackbuffer;
    if ( g_vhSwapchainTextures.empty() ) return VRHI_INVALID_HANDLE;
    return g_vhSwapchainTextures[g_vhCurrentSwapchainIndex];
}

glm::uvec2 vhGetWindowSize()
{
    return g_vhWindowSize;
}

void vhSwapchainCreate_Internal( int width, int height )
{
    // Destroy old image views (if any)
    for ( auto iv : g_vhSwapchainImageViews )
    {
        vkDestroyImageView( g_vulkanDevice, iv, nullptr );
    }
    g_vhSwapchainImageViews.clear();

    // Clear old NVRHI handles
    g_vhSwapchainNVRHIHandles.clear();

    // Build swapchain via vkb::SwapchainBuilder
    vkb::SwapchainBuilder builder( *g_vulkanBDevice, g_vhSurface );
    builder
        .set_desired_extent( width, height )
        .set_desired_format( { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
        .set_desired_present_mode( g_vhInit.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR )
        .add_image_usage_flags( VK_IMAGE_USAGE_TRANSFER_DST_BIT )
        .set_old_swapchain( g_vhSwapchain );

    auto swapRet = builder.build();
    if ( !swapRet )
    {
        VRHI_LOG( "Failed to create swapchain: %s\n", swapRet.error().message().c_str() );
        return;
    }
    vkb::Swapchain vkbSwapchain = swapRet.value();

    // Clean up old swapchain (after creating new one)
    if ( g_vhSwapchain != VK_NULL_HANDLE )
    {
        vkDestroySwapchainKHR( g_vulkanDevice, g_vhSwapchain, nullptr );
    }

    g_vhSwapchain = vkbSwapchain.swapchain;
    g_vhSwapchainImages = vkbSwapchain.get_images().value();
    g_vhSwapchainImageViews = vkbSwapchain.get_image_views().value();

    // Wrap images into nvrhi::TextureHandles
    g_vhSwapchainTextures.clear();
    g_vhSwapchainNVRHIHandles.clear();
    for ( size_t i = 0; i < g_vhSwapchainImages.size(); ++i )
    {
        vhTexture texID = vhAllocTexture();
        g_vhSwapchainTextures.push_back( texID );

        nvrhi::TextureDesc tDesc;
        tDesc.width = vkbSwapchain.extent.width;
        tDesc.height = vkbSwapchain.extent.height;
        tDesc.format = nvrhi::Format::BGRA8_UNORM;
        tDesc.initialState = nvrhi::ResourceStates::Present;
        tDesc.keepInitialState = true;
        tDesc.setIsRenderTarget( true );
        nvrhi::TextureHandle nvrhiHandle = g_vhDevice->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image,
            nvrhi::Object( g_vhSwapchainImages[i] ),
            tDesc
        );

        g_vhSwapchainNVRHIHandles.push_back( nvrhiHandle );
        g_vhCmdBackendState.RegisterInternalTexture( texID, nvrhiHandle, tDesc );
    }

    // Update window size
    g_vhWindowSize = glm::uvec2( vkbSwapchain.extent.width, vkbSwapchain.extent.height );

    // Recreate semaphores
    for ( auto sem : g_vhAcquireSemaphores )
    {
        if ( sem != VK_NULL_HANDLE )
            vkDestroySemaphore( g_vulkanDevice, sem, nullptr );
    }
    for ( auto sem : g_vhPresentSemaphores )
    {
        if ( sem != VK_NULL_HANDLE )
            vkDestroySemaphore( g_vulkanDevice, sem, nullptr );
    }

    int newImageCount = ( int ) g_vhSwapchainImages.size();
    g_vhAcquireSemaphores.resize( newImageCount );
    g_vhPresentSemaphores.resize( newImageCount );

    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for ( int i = 0; i < newImageCount; ++i )
    {
        vkCreateSemaphore( g_vulkanDevice, &sci, nullptr, &g_vhAcquireSemaphores[i] );
        vkCreateSemaphore( g_vulkanDevice, &sci, nullptr, &g_vhPresentSemaphores[i] );
    }

    // Update frames in flight for non-vsync
    if ( !g_vhInit.vsync )
    {
        g_vhFramesInFlight = newImageCount;
        if ( g_vhFramesInFlight > VRHI_MAX_FRAMES_INFLIGHT ) g_vhFramesInFlight = VRHI_MAX_FRAMES_INFLIGHT;
        g_vhFrameInstances.resize( g_vhFramesInFlight, 0 );
    }

    // Initial vkAcquireNextImageKHR
    g_vhFrameIndex = 0;
    g_vhCurrentSwapchainIndex = 0;
    VkResult acqResult = vkAcquireNextImageKHR( g_vulkanDevice, g_vhSwapchain, UINT64_MAX, g_vhAcquireSemaphores[0], VK_NULL_HANDLE, &g_vhCurrentSwapchainIndex );
    if ( acqResult != VK_SUCCESS && acqResult != VK_SUBOPTIMAL_KHR )
    {
        VRHI_LOG( "Warning: Initial acquire returned %d.\n", acqResult );
    }
}

static void vhResizeHeadlessBackbuffer( int width, int height )
{
    g_vhHeadlessBackbufferHandle = nullptr;

    nvrhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = nvrhi::Format::BGRA8_UNORM;
    desc.initialState = nvrhi::ResourceStates::RenderTarget;
    desc.keepInitialState = true;
    desc.setIsRenderTarget( true );
    desc.debugName = "HeadlessBackbuffer";

    g_vhHeadlessBackbufferHandle = g_vhDevice->createTexture( desc );

    if ( g_vhHeadlessBackbuffer == VRHI_INVALID_HANDLE )
        g_vhHeadlessBackbuffer = vhAllocTexture();

    g_vhCmdBackendState.RegisterInternalTexture( g_vhHeadlessBackbuffer, g_vhHeadlessBackbufferHandle, desc );
    g_vhWindowSize = glm::uvec2( width, height );
}

void vhResize( int width, int height )
{
    if ( !g_vhDevice )
    {
        VRHI_ERR( "vhResize(): Device not initialised.\n" );
        return;
    }

    if ( width <= 0 || height <= 0 )
    {
        VRHI_ERR( "vhResize(): Invalid dimensions %dx%d. Dimensions must be positive.\n", width, height );
        return;
    }

    if ( g_vhNullMode )
    {
        g_vhWindowSize = glm::uvec2( width, height );
        return;
    }

    if ( g_vhSurface == VK_NULL_HANDLE )
    {
        vhResizeHeadlessBackbuffer( width, height );
        return;
    }

    VRHI_LOG( "Resizing swapchain to %dx%d\n", width, height );

    // Flush all commands and wait for GPU idle for safety
    vhFinish();

    // Recreate swapchain using shared helper
    vhSwapchainCreate_Internal( width, height );

    // Clear FBO cache to force framebuffer recreation with new dimensions
    vhResizeCleanup();

    VRHI_LOG( "Swapchain resized successfully to %dx%d\n", width, height );
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

    if ( layoutItem.type == ResourceType::RayTracingAccelStruct )
        return BindingSetItem::None( layoutItem.slot );

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

// Make komihash work faster in debug mode.
#if defined(_MSC_VER)
#pragma runtime_checks("", off)
#endif // #if defined(_MSC_VER)

uint64_t vhHashBindingLayout( const nvrhi::BindingLayoutDesc& desc )
{
    // static_assert( sizeof( nvrhi::BindingLayoutDesc ) == 64, "nvrhi::BindingLayoutDesc size mismatch" );
    static_assert( sizeof( nvrhi::BindingLayoutItem ) == 8, "nvrhi::BindingLayoutItem size mismatch" );

    uint64_t h = 0;
    h = komihash( &desc.visibility,                  sizeof( desc.visibility ),                  h );
    h = komihash( &desc.registerSpace,               sizeof( desc.registerSpace ),               h );
    h = komihash( &desc.registerSpaceIsDescriptorSet, sizeof( desc.registerSpaceIsDescriptorSet ), h );
    h = komihash( &desc.bindingOffsets,               sizeof( desc.bindingOffsets ),               h );

    // Bulk-hash all binding items in one call. BindingLayoutItem is an 8-byte POD with no
    // pointer members, so the whole array is safe to treat as a flat byte span.
    if ( !desc.bindings.empty() )
        h = komihash( desc.bindings.data(), desc.bindings.size() * sizeof( nvrhi::BindingLayoutItem ), h );

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

uint64_t vhHashVertexAttributeDesc( int location, const nvrhi::VertexAttributeDesc& attr, uint64_t h )
{
    h = komihash( &location,           sizeof( location ),           h );
    h = komihash( &attr.format,        sizeof( attr.format ),        h );
    h = komihash( &attr.arraySize,     sizeof( attr.arraySize ),     h );
    h = komihash( &attr.bufferIndex,   sizeof( attr.bufferIndex ),   h );
    h = komihash( &attr.offset,        sizeof( attr.offset ),        h );
    h = komihash( &attr.elementStride, sizeof( attr.elementStride ), h );
    h = komihash( &attr.isInstanced,   sizeof( attr.isInstanced ),   h );
    return h;
}

static uint64_t vhHashRenderState( const nvrhi::RenderState& rs )
{
    static_assert( sizeof( nvrhi::RenderState ) == 144, "nvrhi::RenderState size mismatch" );

    uint64_t h = 0;
    h = komihash( &rs.blendState, sizeof( rs.blendState ), h );
    h = komihash( &rs.depthStencilState, sizeof( rs.depthStencilState ), h );
    h = komihash( &rs.rasterState, sizeof( rs.rasterState ), h );
    h = komihash( &rs.singlePassStereo, sizeof( rs.singlePassStereo ), h );
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

#if defined(_MSC_VER)
#pragma runtime_checks("", restore)
#endif

uint64_t vhHashComputePipeline( const nvrhi::ComputePipelineDesc& desc )
{
    static_assert( sizeof( nvrhi::ComputePipelineDesc ) == 80, "nvrhi::ComputePipelineDesc size mismatch" );
    uint64_t h = vhHashShaderDebugName( desc.CS );

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

uint64_t vhHashBindingSet( const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout )
{
    static_assert( sizeof( nvrhi::BindingSetItem ) == 40, "nvrhi::BindingSetItem size mismatch" );

    // We expect desc to be zero init.
    void* pLayout = layout;
    uint64_t h = komihash( &pLayout, sizeof( pLayout ), 0 );
    h = komihash( &desc.trackLiveness, sizeof( desc.trackLiveness ), h );

    if ( !desc.bindings.empty() )
    {
        constexpr size_t kStackMax = 16;
        nvrhi::BindingSetItem stackBuf[kStackMax];
        std::vector< nvrhi::BindingSetItem > heapBuf;

        nvrhi::BindingSetItem* canonical = stackBuf;
        const size_t count = desc.bindings.size();
        if ( count > kStackMax )
        {
            heapBuf.resize( count );
            canonical = heapBuf.data();
        }

        for ( size_t i = 0; i < count; ++i )
        {
            canonical[i] = desc.bindings[i];

            // Deterministic hash requires deterministic padding.
            assert( canonical[i].unused == 0 );
            assert( canonical[i].unused2 == 0 );

            // For null handles, view parameters are meaningless — zero them so
            // they don't pollute the hash.
            if ( !canonical[i].resourceHandle )
            {
                canonical[i].format = nvrhi::Format::UNKNOWN;
                canonical[i].dimension = nvrhi::TextureDimension::Unknown;
                canonical[i].rawData[0] = 0;
                canonical[i].rawData[1] = 0;
            }
        }

        h = komihash( canonical, count * sizeof( nvrhi::BindingSetItem ), h );
    }

    return h;
}

static std::unordered_map< uint64_t, nvrhi::BindingSetHandle > s_bindingSetCache;

void vhBindingSetCacheClear()
{
    std::lock_guard<std::mutex> lock( g_nvRHIStateMutex );
    s_bindingSetCache.clear();
}

nvrhi::BindingSetHandle vhGetBindingSet( const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout )
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
    h = komihash( &desc.compareFunc, sizeof( desc.compareFunc ), h );

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

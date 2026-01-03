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
#include <komihash/komihash.h>

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

    // Create VkInstance (via vk-bootstrap)

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

    // Physical Device Selection (via vk-bootstrap)

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

    // Device Creation & Queues (via vk-bootstrap)

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
            s_vhDummyTextures[( int )dim][i] = handle;

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
        nvrhi::TextureHandle tex = s_vhDummyTextures[( int )expectedDim][mode];
        if ( !tex ) tex = s_vhDummyTextures[( int )TextureDimension::Texture2D][mode]; // Fallback

        if ( layoutItem.type == ResourceType::Texture_SRV )
            return BindingSetItem::Texture_SRV( layoutItem.slot, tex, expectedFormat ); 
        else
            return BindingSetItem::Texture_UAV( layoutItem.slot, tex, expectedFormat );
    }

    return BindingSetItem::None( layoutItem.slot );
}

// -------------------------------------------------------- Hashing --------------------------------------------------------

static uint64_t vhHashBindingLayout( const nvrhi::BindingLayoutDesc& desc )
{
    // TODO: implement HashBindingLayout here
    // Fields to hash: `visibility`, `registerSpace`, `registerSpaceIsDescriptorSet`, `bindings` (iterate: slot, type, size).
    uint64_t h = 0;
    return h;
}

static uint64_t vhHashShaderBytecode( nvrhi::IShader* shader )
{
    // TODO: implement GetShaderBytecodeHash here
    // Action: Call `shader->getBytecode` and hash the buffer.
    uint64_t h = 0;
    return h;
}

static uint64_t vhHashInputLayout( nvrhi::IInputLayout* layout )
{
    // TODO: implement GetAttributesHash here
    // Action: Call `layout->getAttributeDesc`, iterate and hash: `name`, `format`, `arraySize`, `bufferIndex`, `offset`, `elementStride`, `isInstanced`.
    uint64_t h = 0;
    return h;
}

static uint64_t vhHashRenderState( const nvrhi::RenderState& rs )
{
    uint64_t h = 0;

    // --- Blend State ---
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

    // --- Depth Stencil State ---
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

    // --- Raster State ---
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

    // --- Single Pass Stereo ---
    h = komihash( &rs.singlePassStereo.enabled, sizeof( rs.singlePassStereo.enabled ), h );
    h = komihash( &rs.singlePassStereo.independentViewportMask, sizeof( rs.singlePassStereo.independentViewportMask ), h );
    h = komihash( &rs.singlePassStereo.renderTargetIndexOffset, sizeof( rs.singlePassStereo.renderTargetIndexOffset ), h );

    return h;
}

static uint64_t vhHashFramebufferInfo( const nvrhi::FramebufferInfo& fb )
{
    uint64_t h = 0;
    for ( auto fmt : fb.colorFormats ) h = komihash( &fmt, sizeof( fmt ), h );
    h = komihash( &fb.depthFormat, sizeof( fb.depthFormat ), h );
    h = komihash( &fb.sampleCount, sizeof( fb.sampleCount ), h );
    h = komihash( &fb.sampleQuality, sizeof( fb.sampleQuality ), h );
    return h;
}

uint64_t vhHashGraphicsPipeline( const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& fbInfo )
{
    uint64_t h = 0;

    // 1. Core State
    h = komihash( &desc.primType, sizeof( desc.primType ), h );
    h = komihash( &desc.patchControlPoints, sizeof( desc.patchControlPoints ), h );

    // 2. Input Layout (HOT)
    uint64_t hInput = vhHashInputLayout( desc.inputLayout );
    h = komihash( &hInput, sizeof( hInput ), h );

    // 3. Shaders (HOT)
    uint64_t hVS = vhHashShaderBytecode( desc.VS ); h = komihash( &hVS, sizeof( hVS ), h );
    uint64_t hHS = vhHashShaderBytecode( desc.HS ); h = komihash( &hHS, sizeof( hHS ), h );
    uint64_t hDS = vhHashShaderBytecode( desc.DS ); h = komihash( &hDS, sizeof( hDS ), h );
    uint64_t hGS = vhHashShaderBytecode( desc.GS ); h = komihash( &hGS, sizeof( hGS ), h );
    uint64_t hPS = vhHashShaderBytecode( desc.PS ); h = komihash( &hPS, sizeof( hPS ), h );

    // 4. Render State (COLD)
    uint64_t hRS = vhHashRenderState( desc.renderState );
    h = komihash( &hRS, sizeof( hRS ), h );

    // 5. Variable Rate Shading (COLD)
    h = komihash( &desc.shadingRateState.enabled, sizeof( desc.shadingRateState.enabled ), h );
    h = komihash( &desc.shadingRateState.shadingRate, sizeof( desc.shadingRateState.shadingRate ), h );
    h = komihash( &desc.shadingRateState.pipelinePrimitiveCombiner, sizeof( desc.shadingRateState.pipelinePrimitiveCombiner ), h );
    h = komihash( &desc.shadingRateState.imageCombiner, sizeof( desc.shadingRateState.imageCombiner ), h );

    // 6. Binding Layouts (HOT)
    for ( const auto& layoutHandle : desc.bindingLayouts )
    {
        const nvrhi::BindingLayoutDesc* layoutDesc = layoutHandle->getDesc();
        if ( layoutDesc )
        {
            uint64_t hLayout = vhHashBindingLayout( *layoutDesc );
            h = komihash( &hLayout, sizeof( hLayout ), h );
        }
    }

    // 7. Framebuffer Compatibility (COLD)
    uint64_t hFB = vhHashFramebufferInfo( fbInfo );
    h = komihash( &hFB, sizeof( hFB ), h );

    return h;
}

uint64_t vhHashComputePipeline( const nvrhi::ComputePipelineDesc& desc )
{
    uint64_t h = 0;

    // Shader
    uint64_t hCS = vhHashShaderBytecode( desc.CS );
    h = komihash( &hCS, sizeof( hCS ), h );

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

    return h;
}

// -------------------------------------------------------- PSO Cache --------------------------------------------------------


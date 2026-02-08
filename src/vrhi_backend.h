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

#include "vrhi_internal.h"
#include "vrhi_utils.h"
#include <komihash/komihash.h>

// --------------------------------------------------------------------------
// Backend Types
// --------------------------------------------------------------------------

struct vhBackendTexture
{
    std::string name;
    nvrhi::TextureHandle handle;
    vhTexInfo info;
    int64_t pitchSize;
    int64_t arraySize;
    std::vector< vhTextureMipInfo > mipInfo;
    uint64_t flags = 0;
};

struct vhBackendBuffer
{
    std::string name;
    nvrhi::BufferHandle handle;
    nvrhi::BufferDesc desc;
    uint32_t stride = 0;
    uint64_t flags = 0;
    std::vector< vhVertexLayoutDef > layout;
};

struct vhBackendHeap
{
    nvrhi::HeapHandle handle;
    nvrhi::HeapDesc desc;
    void* allocator = nullptr;
    std::unordered_map< uint64_t, std::pair< uint32_t, uint32_t > > allocations;
};

struct vhBackendShader
{
    std::string name;
    nvrhi::ShaderHandle handle;
    uint64_t flags;
    std::string entry;

    // Reflection Data
    std::vector< vhShaderReflectionResource > reflection;
    std::vector< vhVertexLayoutDef > inputLayout;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BindingLayoutDesc layoutDesc = { .bindingOffsets = { 0, 0, 0, 0 } };

    // Metadata
    glm::uvec3 threadGroupSize = { 0, 0, 0 };
    std::vector< vhPushConstantRange > pushConstants;
    std::vector< vhSpecConstant > specConstants;
};

struct vhBackendAccelStruct;
struct vhStateResolveCache
{
    bool init = false;

    std::vector< vhBackendTexture* > btex;
    std::vector< vhBackendBuffer* > bbuf;
    std::vector< vhBackendShader* > bshaders;
    std::vector< vhBackendAccelStruct* > baccel;

    struct ResolvedTexture
    {
        nvrhi::TextureHandle handle = nullptr;
        const vhState::TextureBinding* binding = nullptr;
    };

    struct ResolvedBuffer
    {
        nvrhi::BufferHandle handle = nullptr;
        const vhState::BufferBinding* binding = nullptr;
    };

    struct ResolvedAccelStruct
    {
        nvrhi::rt::AccelStructHandle handle = nullptr;
        const vhState::AccelStructBinding* binding = nullptr;
    };

    // We work with ShaderMake defaults, which define bindings into 4 ranges.
    //
    // * Samplers (s registers): Default shift is 100.
    // * Textures (t registers): Default shift is 200.
    // * Constant Buffers (b registers): Default shift is 300.
    // * UAVs (u registers): Default shift is 400.
    //
    // The range values can be customised, the separation pattern cannot.
    // Must be s < t < b < u shifts, with uRegShift having highest value.
    //
    struct ShaderStageBindingSlotState
    {
        std::unordered_map< uint32_t, nvrhi::SamplerHandle > samplerTable; // slot -> sampler
        std::unordered_map< uint32_t, ResolvedTexture > textureTable; // slot -> texture
        std::unordered_map< uint32_t, ResolvedBuffer > bufferTable; // slot -> buffer
        std::unordered_map< uint32_t, std::pair< ResolvedTexture, ResolvedBuffer > > uavTable; // slot -> UAV. Either texture or buffer, but not both.
        uint32_t userGlobalsSlot = UINT32_MAX;  // UINT32_MAX = no User Globals
        uint64_t userGlobalsHash = 0;           // Hash of reflection members
    };
    std::unordered_map< uint32_t, std::unique_ptr< ShaderStageBindingSlotState > > stageBinding; // stage flag -> binding slot state

    struct UserGlobalUniformsBufferInfo
    {
        nvrhi::BufferHandle buffer;
        nvrhi::BufferRange range;
    };
    std::unordered_map< uint64_t, UserGlobalUniformsBufferInfo > userGlobalUniformsBufferCache; // hash -> buffer

    inline void Clear()
    {
        init = false;
        btex.clear();
        bbuf.clear();
        bshaders.clear();
        baccel.clear();
        stageBinding.clear();
        userGlobalUniformsBufferCache.clear();
    }
};

struct vhBackendTimerQuery
{
    nvrhi::TimerQueryHandle handles[VRHI_MAX_FRAMES_INFLIGHT];
    int currentFrameIndex = 0;
    float lastQueryTime = 0.0f; // Cached result in seconds
    bool initialised = false;
};

struct vhBackendAccelStruct
{
    std::string name;
    nvrhi::rt::AccelStructHandle handle;
    nvrhi::rt::AccelStructDesc desc;
};

struct vhBackendRTPipeline
{
    std::string name;
    nvrhi::rt::PipelineHandle handle;
    nvrhi::rt::PipelineDesc desc;
};

struct vhBackendShaderTable
{
    std::string name;
    nvrhi::rt::ShaderTableHandle handle;
    vhRTPipeline pipeline;
};

// --------------------------------------------------------------------------
// Main Backend State
// --------------------------------------------------------------------------

class vhCmdBackendState : public VIDLHandler
{
    friend class vhCmdBackendStateTest;
    
    std::mutex backendMutex;
    char temps[1024];
    std::map< vhTexture, std::unique_ptr< vhBackendTexture > > backendTextures;
    std::map< vhBuffer, std::unique_ptr< vhBackendBuffer > > backendBuffers;
    std::map< vhShader, std::unique_ptr< vhBackendShader > > backendShaders;
    std::map< vhAccelStruct, std::unique_ptr< vhBackendAccelStruct > > backendAccelStructs;
    std::map< vhRTPipeline, std::unique_ptr< vhBackendRTPipeline > > backendRTPipelines;
    std::map< vhShaderTable, std::unique_ptr< vhBackendShaderTable > > backendShaderTables;
    std::map< vhTimerID, std::unique_ptr< vhBackendTimerQuery > > backendTimerQueries;
    std::map< vhStateId, vhState > backendStates;
    std::map< vhHeap, std::unique_ptr< vhBackendHeap > > backendHeaps;
    vhTransientBuffer m_globalUniformBuffer;
    uint64_t m_globalUniformBufferLastHash = 0;
    vhTransientBuffer m_worldUniformBuffer;
    uint64_t m_worldUniformBufferLastHash = 0;
    vhTransientBuffer m_userUniformBuffer;
    nvrhi::BindingLayoutHandle m_emptyLayout;
    nvrhi::BindingSetHandle m_emptySet;

    // RAII for vhMem, takes ownership of the pointer and auto-destructs it.
    inline std::unique_ptr< vhMem > BE_MemRAII( const vhMem* mem )
    {
        return std::unique_ptr< vhMem >( const_cast< vhMem* >( mem ) );
    }

    // --------------------------------------------------------------------------
    // Backend :: Utils & Helpers
    // --------------------------------------------------------------------------

    // RAII helper for the VIDL_* commands. Helps with syntax.
    // Usage: BE_CmdRAII cmdRAII( cmd );
    template< typename T >
    struct BE_CmdRAII
    {
        T* ptr = nullptr;
        BE_CmdRAII( T* ptr ) : ptr( ptr ) {}
        ~BE_CmdRAII() { if ( ptr ) vhCmdRelease( ptr ); }

        // Non-copyable (prevents double-free)
        BE_CmdRAII( const BE_CmdRAII& ) = delete;
        BE_CmdRAII& operator=( const BE_CmdRAII& ) = delete;

        // Movable
        BE_CmdRAII( BE_CmdRAII&& other ) noexcept : ptr( other.ptr ) { other.ptr = nullptr; }
        BE_CmdRAII& operator=( BE_CmdRAII&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( ptr ) vhCmdRelease( ptr );
                ptr = other.ptr;
                other.ptr = nullptr;
            }
            return *this;
        }
    };

    // Deduction guide (usually implicit in C++17, but being explicit helps some compilers)
    template< typename T > BE_CmdRAII( T* ) -> BE_CmdRAII<T>;

    int32_t BE_Util_ResolveBindingSlot( const char* name, nvrhi::ResourceType type, vhBackendShader& shader, bool debugLog = false );

    bool BE_Util_ShaderStageMatches( uint64_t flags, bool useCompute, bool useGraphics );



    // --------------------------------------------------------------------------
    // Backend :: Complex BE Low Level NVRHI Device Functions
    // --------------------------------------------------------------------------

    void BE_UpdateTexture( vhBackendTexture& btex, const vhMem* data, glm::ivec4 arrayMipUpdateRange = glm::ivec4( 0, INT_MAX, 0, INT_MAX ) );

    void BE_BlitTexture( vhBackendTexture& bdst, vhBackendTexture& bsrc, int dstMip, int srcMip, int dstLayer, int srcLayer, glm::ivec3 dstOffset, glm::ivec3 srcOffset, glm::ivec3 extent );

    void BE_ReadTextureSlow( vhBackendTexture& btex, vhMem* outData, int mip, int layer );

    void BE_ResizeBuffer( vhBackendBuffer& bbuf, uint64_t size );

    void BE_UpdateBuffer( vhBackendBuffer& bbuf, uint64_t offset, const vhMem* data );

    nvrhi::FramebufferHandle BE_GetFrameBuffer( const std::vector< vhState::RenderTarget >& colourAttachment, const vhState::RenderTarget& depthAttachment, vhTexture shadingRateImage );

    int64_t BE_Util_WriteGlobalUniform( const vhState& state, vhTransientBuffer& tbuf, uint64_t& lastHash );
    int64_t BE_Util_WriteWorldUniform( const vhState& state, vhTransientBuffer& tbuf, uint64_t& lastHash );

    bool BE_PresubmitCommon_PipelineDesc(
        vhState& state,
        vhBackendShader* shaders,
        int shaderCount,
        nvrhi::ComputePipelineDesc* computePipelineDesc, // set to nullptr if not using compute.
        nvrhi::GraphicsPipelineDesc* graphicsPipelineDesc // set to nullptr if not using graphics.
    );

    void BE_PreSubmitCommon_ResolveStateCache(
        const vhState& state,
        vhBackendShader* shaders,
        int shaderCount,
        vhStateResolveCache& scache
    );

    bool BE_PreSubmitCommon_FindResource(
        const vhState& state,
        const uint32_t stage,
        const vhStateResolveCache& scache,
        const nvrhi::BindingLayoutItem& item,
        nvrhi::BindingSetItem& outItem,
        const char* name = nullptr
    );

    bool BE_PreSubmitCommon_State(
        nvrhi::CommandListHandle cmdList,
        vhState& state,
        vhBackendShader* shaders,
        int shaderCount,
        nvrhi::ComputeState* computeState, // set to nullptr if not using compute.
        nvrhi::GraphicsState* graphicsState, // set to nullptr if not using graphics.
        nvrhi::rt::State* rtState = nullptr,
        const nvrhi::BindingLayoutVector* layoutOverride = nullptr,
        nvrhi::FramebufferHandle fb = nullptr
    );

    void BE_Dispatch( vhState& state, vhBackendShader& computeShader, glm::uvec3 workGroupCount );

    void BE_DispatchIndirect( vhState& state, vhBackendShader& computeShader, vhBackendBuffer& indirectBuffer, uint64_t byteOffset );

    void BE_Submit( vhState& state, vhBackendShader* shaders, int shaderCount, uint32_t flags, const nvrhi::DrawArguments& args, uint32_t drawCount );

    void BE_BlitBuffer( vhBackendBuffer& dst, vhBackendBuffer& src, uint64_t dstOffset, uint64_t srcOffset, uint64_t size );

    void BE_DispatchRays( vhState& state, vhBackendRTPipeline& pipeline, vhBackendShaderTable& shaderTable, const nvrhi::rt::DispatchRaysArguments& args );

protected:
    // Static caches to avoid reallocation overhead, explicitly cleared in shutdown()
    static std::unordered_map< nvrhi::BindingLayoutHandle, vhBackendShader* > s_layoutToShader;
    static vhStateResolveCache s_resolveCache;
    static std::unordered_map< uint32_t, vhShaderReflectionResource* > s_slotToReflection;
    static std::unordered_map< uint64_t, const vhVertexLayoutDef* > s_layoutLocationTable;
    static std::vector< nvrhi::VertexAttributeDesc > s_attributes;
    static std::vector< vhBackendShader > s_shaders;

public:

    void init();

    void shutdown();

    void HandleLogFunction( const char* str ) override;

    void RegisterInternalTexture( vhTexture id, const nvrhi::TextureHandle& handle, const nvrhi::TextureDesc& desc );

    // --------------------------------------------------------------------------
    // Backend :: VIDL Command Handlers
    // --------------------------------------------------------------------------

    void Handle_vhResizeCleanup( VIDL_vhResizeCleanup* cmd ) override;
    void Handle_vhBeginTimerQuery( VIDL_vhBeginTimerQuery* cmd ) override;
    void Handle_vhEndTimerQuery( VIDL_vhEndTimerQuery* cmd ) override;
    void Handle_vhBeginMarker( VIDL_vhBeginMarker* cmd ) override;
    void Handle_vhEndMarker( VIDL_vhEndMarker* cmd ) override;
    void Handle_vhCaptureStart( VIDL_vhCaptureStart* cmd ) override;
    void Handle_vhCaptureEnd( VIDL_vhCaptureEnd* cmd ) override;
    void Handle_vhResetTexture( VIDL_vhResetTexture* cmd ) override;
    void Handle_vhResetBuffer( VIDL_vhResetBuffer* cmd ) override;
    void Handle_vhDestroyTexture( VIDL_vhDestroyTexture* cmd ) override;
    void Handle_vhCreateTexture( VIDL_vhCreateTexture* cmd ) override;
    void Handle_vhUpdateTexture( VIDL_vhUpdateTexture* cmd ) override;
    void Handle_vhReadTextureSlow( VIDL_vhReadTextureSlow* cmd ) override;
    void Handle_vhBlitTexture( VIDL_vhBlitTexture* cmd ) override;
    void Handle_vhCreateVertexBuffer( VIDL_vhCreateVertexBuffer* cmd ) override;
    void Handle_vhUpdateVertexBuffer( VIDL_vhUpdateVertexBuffer* cmd ) override;
    void Handle_vhCreateIndexBuffer( VIDL_vhCreateIndexBuffer* cmd ) override;
    void Handle_vhUpdateIndexBuffer( VIDL_vhUpdateIndexBuffer* cmd ) override;
    void Handle_vhCreateUniformBuffer( VIDL_vhCreateUniformBuffer* cmd ) override;
    void Handle_vhUpdateUniformBuffer( VIDL_vhUpdateUniformBuffer* cmd ) override;
    void Handle_vhCreateStorageBuffer( VIDL_vhCreateStorageBuffer* cmd ) override;
    void Handle_vhUpdateStorageBuffer( VIDL_vhUpdateStorageBuffer* cmd ) override;
    void Handle_vhBlitBuffer( VIDL_vhBlitBuffer* cmd ) override;
    void Handle_vhDestroyBuffer( VIDL_vhDestroyBuffer* cmd ) override;
    void Handle_vhCreateHeap( VIDL_vhCreateHeap* cmd ) override;
    void Handle_vhDestroyHeap( VIDL_vhDestroyHeap* cmd ) override;
    void Handle_vhBindTextureMemory( VIDL_vhBindTextureMemory* cmd ) override;
    void Handle_vhBindBufferMemory( VIDL_vhBindBufferMemory* cmd ) override;
    void Handle_vhCreateAS( VIDL_vhCreateAS* cmd ) override;
    void Handle_vhDestroyAS( VIDL_vhDestroyAS* cmd ) override;
    void Handle_vhBuildBLAS( VIDL_vhBuildBLAS* cmd ) override;
    void Handle_vhBuildTLAS( VIDL_vhBuildTLAS* cmd ) override;
    void Handle_vhCreateRTPipeline( VIDL_vhCreateRTPipeline* cmd ) override;
    void Handle_vhDestroyRTPipeline( VIDL_vhDestroyRTPipeline* cmd ) override;
    void Handle_vhCreateShaderTable( VIDL_vhCreateShaderTable* cmd ) override;
    void Handle_vhDestroyShaderTable( VIDL_vhDestroyShaderTable* cmd ) override;
    void Handle_vhShaderTableSetRayGen( VIDL_vhShaderTableSetRayGen* cmd ) override;
    void Handle_vhShaderTableAddMiss( VIDL_vhShaderTableAddMiss* cmd ) override;
    void Handle_vhShaderTableAddHitGroup( VIDL_vhShaderTableAddHitGroup* cmd ) override;
    void Handle_vhDispatchRays( VIDL_vhDispatchRays* cmd ) override;
    void Handle_vhCreateShader( VIDL_vhCreateShader* cmd ) override;
    void Handle_vhDestroyShader( VIDL_vhDestroyShader* cmd ) override;
    void Handle_vhDispatch( VIDL_vhDispatch* cmd ) override;
    void Handle_vhDispatchIndirect( VIDL_vhDispatchIndirect* cmd ) override;
    void Handle_vhClear( VIDL_vhClear* cmd ) override;
    void Handle_vhFlushInternal( VIDL_vhFlushInternal* cmd ) override;
    void Handle_vhCmdSetStateViewRect( VIDL_vhCmdSetStateViewRect* cmd ) override;
    void Handle_vhCmdSetStateViewScissor( VIDL_vhCmdSetStateViewScissor* cmd ) override;
    void Handle_vhCmdSetStateViewClear( VIDL_vhCmdSetStateViewClear* cmd ) override;
    void Handle_vhCmdSetStateProgram( VIDL_vhCmdSetStateProgram* cmd ) override;
    void Handle_vhCmdSetStateViewTransform( VIDL_vhCmdSetStateViewTransform* cmd ) override;
    void Handle_vhCmdSetStateWorldTransform( VIDL_vhCmdSetStateWorldTransform* cmd ) override;
    void Handle_vhCmdSetStateFlags( VIDL_vhCmdSetStateFlags* cmd ) override;
    void Handle_vhCmdSetStateDebugFlags( VIDL_vhCmdSetStateDebugFlags* cmd ) override;
    void Handle_vhCmdSetStateStencil( VIDL_vhCmdSetStateStencil* cmd ) override;
    void Handle_vhCmdSetStateDepthBias( VIDL_vhCmdSetStateDepthBias* cmd ) override;
    void Handle_vhCmdSetStateVertexBuffer( VIDL_vhCmdSetStateVertexBuffer* cmd ) override;
    void Handle_vhCmdSetStateIndexBuffer( VIDL_vhCmdSetStateIndexBuffer* cmd ) override;
    void Handle_vhCmdSetStateTextures( VIDL_vhCmdSetStateTextures* cmd ) override;
    void Handle_vhCmdSetStateSamplers( VIDL_vhCmdSetStateSamplers* cmd ) override;
    void Handle_vhCmdSetStateBuffers( VIDL_vhCmdSetStateBuffers* cmd ) override;
    void Handle_vhCmdSetStateConstants( VIDL_vhCmdSetStateConstants* cmd ) override;
    void Handle_vhCmdSetStatePushConstants( VIDL_vhCmdSetStatePushConstants* cmd ) override;
    void Handle_vhCmdSetStateUniforms( VIDL_vhCmdSetStateUniforms* cmd ) override;
    void Handle_vhCmdSetStateAttachments( VIDL_vhCmdSetStateAttachments* cmd ) override;
    void Handle_vhCmdSetStateBlendConstants( VIDL_vhCmdSetStateBlendConstants* cmd ) override;
    void Handle_vhCmdSetStateViewDepthRange( VIDL_vhCmdSetStateViewDepthRange* cmd ) override;
    void Handle_vhCmdSetStateShadingRate( VIDL_vhCmdSetStateShadingRate* cmd ) override;
    void Handle_vhCmdSetStateIndirectParams( VIDL_vhCmdSetStateIndirectParams* cmd ) override;
    void Handle_vhCmdSetStateAccelStructs( VIDL_vhCmdSetStateAccelStructs* cmd ) override;
    void Handle_vhDrawCommonInternal( VIDL_vhDrawCommonInternal* cmd ) override;

    vhBackendBuffer* Handle_vhCreateBufferCommon_Internal( const char* fn, vhBuffer buffer, nvrhi::BufferDesc& desc, const char* name, const char* autoname,
        const vhMem* data, uint64_t count, uint64_t stride, uint64_t flags );

    void Handle_vhUpdateBufferCommon_Internal( const char* fn, vhBuffer buffer, uint64_t offsetElements, const vhMem* data, uint64_t count, bool isVertexBuffer );

    // --------------------------------------------------------------------------
    // Backend :: RHIThreadEntry
    // --------------------------------------------------------------------------

    void RHIThreadEntry( std::function<void()> initCallback );

    // --------------------------------------------------------------------------
    // Backend :: Query
    // --------------------------------------------------------------------------

    // The query functions are a fastpath for getting info about objects from the main-thread. They directly lock the backend mutex and access the backend maps, rather than 
    // sending a command to the backend thread and then waiting for a response. Object info queries are usually extremely short and fast, so this is OK. Query functions should never do 
    // significant work or take a long time to complete, because that would bubble the hell out of the command thread.

    vhTexInfo QueryTextureInfo( vhTexture handle, std::vector< vhTextureMipInfo >* outMipInfo );

    nvrhi::TextureHandle QueryTextureHandle( vhTexture handle );

    uint64_t QueryBufferInfo( vhBuffer handle, uint32_t* outStride, uint64_t* outFlags );

    nvrhi::BufferHandle QueryBufferHandle( vhBuffer handle );

    const std::vector< vhVertexLayoutDef >* QueryBufferLayout( vhBuffer handle );

    void QueryShaderInfo(
        vhShader handle,
        glm::uvec3* outGroupSize,
        std::vector< vhShaderReflectionResource >* outResources,
        std::vector< vhPushConstantRange >* outPushConstants,
        std::vector< vhSpecConstant >* outSpecConstants
    );

    nvrhi::ShaderHandle QueryShaderHandle( vhShader handle );

    bool QueryState( vhStateId id, vhState& outState );

    float QueryTimer( vhTimerID timerID );

    nvrhi::rt::AccelStructHandle QueryAccelStructHandle( vhAccelStruct as );
    
    nvrhi::rt::PipelineHandle QueryRTPipelineHandle( vhRTPipeline pipeline );

    nvrhi::rt::ShaderTableHandle QueryShaderTableHandle( vhShaderTable table );
    
    glm::u64vec2 QueryTextureMemoryRequirements( vhTexture texture );
    glm::u64vec2 AllocTextureMemory( vhHeap heap, uint64_t size, uint64_t alignment );
    void FreeTextureMemory( vhHeap heap, uint64_t offset );
    glm::u64vec2 QueryBufferMemoryRequirements( vhBuffer buffer );
};
